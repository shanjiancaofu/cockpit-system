#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
capture_date="${CAPTURE_DATE:-$(date +%F)}"
dataset_dir="${CALIBRATION_DATASET_DIR:-${root_dir}/_output/datasets/camera-calibration/q12-imx219-${capture_date}}"
sensor_id="${CAMERA_SENSOR_ID:-0}"
width=1920
height=1080
fps=30
bitrate_kbps=30000
preview_sink=""

usage() {
  cat <<'EOF'
usage: scripts/run/capture-camera-calibration-dataset.sh [--no-preview]

Interactively records the five Q12/IMX219 calibration videos with Argus at
1920x1080@30. Environment overrides:

  CALIBRATION_DATASET_DIR  Output directory
  CAPTURE_DATE             Dataset date (YYYY-MM-DD)
  CAMERA_SENSOR_ID         Argus sensor id (default: 0)
EOF
}

no_preview=false
while (($# > 0)); do
  case "$1" in
    --no-preview)
      no_preview=true
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if [[ ! -t 0 ]]; then
  echo "this script is interactive and requires a terminal" >&2
  exit 2
fi

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing command: $1" >&2
    exit 2
  fi
}

require_element() {
  if ! gst-inspect-1.0 "$1" >/dev/null 2>&1; then
    echo "missing GStreamer element: $1" >&2
    exit 2
  fi
}

require_command gst-launch-1.0
require_command gst-inspect-1.0

for element in nvarguscamerasrc nvvidconv x264enc h264parse qtmux; do
  require_element "${element}"
done

if [[ "${no_preview}" != true ]]; then
  if gst-inspect-1.0 nv3dsink >/dev/null 2>&1; then
    preview_sink="nv3dsink"
  elif gst-inspect-1.0 nveglglessink >/dev/null 2>&1; then
    preview_sink="nveglglessink"
  else
    echo "no nv3dsink or nveglglessink; rerun with --no-preview if intentional" >&2
    exit 2
  fi
fi

mkdir -p "${dataset_dir}"

echo "Q12 + IMX219 标定视频数据集录制"
echo "输出目录：${dataset_dir}"
echo "视频源：  nvargus://${sensor_id}"
echo "视频格式：${width}x${height}@${fps}, H.264 ${bitrate_kbps} kbit/s"
if [[ -n "${preview_sink}" ]]; then
  echo "实时预览：${preview_sink}"
else
  echo "实时预览：已禁用"
fi
echo
echo "继续之前，请关闭 cockpit-ui、camera-calibrator 和其他占用相机的程序。"
read -r -p "确认相机空闲后按 Enter... "

record_clip() {
  local name="$1"
  local seconds="$2"
  local guidance="$3"
  local frames=$((seconds * fps))
  local output="${dataset_dir}/${name}.mp4"
  local partial="${dataset_dir}/.${name}.${BASHPID}.partial.mp4"
  local choice=""
  local status=0

  if [[ -s "${output}" ]]; then
    echo
    echo "文件已经存在：${output}"
    read -r -p "保留 [K]、重新录制 [r]、退出 [q]？ " choice
    case "${choice}" in
      r|R)
        ;;
      q|Q)
        exit 0
        ;;
      *)
        echo "保留已有视频。"
        return
        ;;
    esac
  fi

  echo
  echo "============================================================"
  echo "${name}.mp4 — ${seconds} 秒"
  echo "============================================================"
  printf '%s\n' "${guidance}"
  echo
  echo "棋盘必须完整可见；缓慢移动，到达每个姿态后停顿 0.5～1 秒。"
  read -r -p "拿稳棋盘后按 Enter 开始录制... "

  if [[ -n "${preview_sink}" ]]; then
    gst-launch-1.0 -e \
      nvarguscamerasrc sensor-id="${sensor_id}" num-buffers="${frames}" ! \
      "video/x-raw(memory:NVMM),format=NV12,width=${width},height=${height},framerate=${fps}/1" ! \
      tee name=t \
      t. ! queue ! \
        nvvidconv ! \
        "video/x-raw,format=I420,width=${width},height=${height},framerate=${fps}/1" ! \
        x264enc bitrate="${bitrate_kbps}" speed-preset=ultrafast tune=zerolatency \
          key-int-max="${fps}" ! \
        h264parse ! qtmux ! filesink location="${partial}" \
      t. ! queue leaky=downstream max-size-buffers=2 ! \
        "${preview_sink}" sync=false || status=$?
  else
    gst-launch-1.0 -e \
      nvarguscamerasrc sensor-id="${sensor_id}" num-buffers="${frames}" ! \
      "video/x-raw(memory:NVMM),format=NV12,width=${width},height=${height},framerate=${fps}/1" ! \
      nvvidconv ! \
      "video/x-raw,format=I420,width=${width},height=${height},framerate=${fps}/1" ! \
      x264enc bitrate="${bitrate_kbps}" speed-preset=ultrafast tune=zerolatency \
        key-int-max="${fps}" ! \
      h264parse ! qtmux ! filesink location="${partial}" || status=$?
  fi

  if ((status != 0)) || [[ ! -s "${partial}" ]]; then
    echo "录制失败，没有写入正式视频。" >&2
    if [[ -e "${partial}" ]]; then
      echo "为便于诊断，已保留临时文件：${partial}" >&2
    fi
    exit 1
  fi

  mv -f "${partial}" "${output}"
  echo "录制成功：${output}"
  ls -lh "${output}"
}

record_clip "01_spatial" 30 $'中心 -> 左 -> 右 -> 上 -> 下\n然后：左上 -> 右上 -> 左下 -> 右下 -> 回到中心'

record_clip "02_yaw" 30 $'棋盘先正对相机，然后绕竖直轴旋转：\n正视 -> 左倾 -> 正视 -> 右倾 -> 正视\n依次使用小、中、大倾角；不要把棋盘在画面平面内转成“歪头”。'

record_clip "03_pitch" 30 $'棋盘先正对相机，然后绕水平轴倾斜：\n正视 -> 上沿远离相机 -> 正视 -> 下沿远离相机 -> 正视\n依次使用小、中、大倾角。'

record_clip "04_scale" 30 $'中距离 -> 移到近距离并停顿 -> 回到中距离\n然后移到远距离并停顿 -> 回到中距离\n近距离时棋盘也必须完整留在画面内。'

record_clip "05_mixed" 40 $'综合位置、左右倾角、上下倾角和距离：\n左上 + 左右倾；右上 + 左右倾；左下 + 上下倾；右下 + 上下倾\n然后补充中心近距离、中心远距离，以及前四段覆盖不足的姿态。'

cat >"${dataset_dir}/metadata.yaml" <<EOF
camera: IMX219
source: nvargus://${sensor_id}
sensor_id: ${sensor_id}
resolution: ${width}x${height}
fps: ${fps}
container: mp4
codec: h264
encoder: x264enc
bitrate_bps: $((bitrate_kbps * 1000))
board: Q12-70-5
squares: 12x9
corners: 11x8
square_size_m: 0.005
capture_date: ${capture_date}
EOF

echo
echo "数据集录制完成："
ls -lh "${dataset_dir}"

if command -v ffprobe >/dev/null 2>&1; then
  echo
  echo "视频流检查结果："
  for video in "${dataset_dir}"/*.mp4; do
    echo "--- ${video##*/}"
    ffprobe -v error -select_streams v:0 \
      -show_entries stream=codec_name,width,height,avg_frame_rate,duration,nb_frames \
      -of default=noprint_wrappers=1 "${video}"
  done
else
  echo "未安装 ffprobe，跳过最终视频流检查。"
fi
