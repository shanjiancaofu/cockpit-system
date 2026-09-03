#!/usr/bin/env python3
# flake8: noqa: E501
"""Monitor an already-running Cockpit UI stack during a bounded soak test."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import signal
import subprocess
import time


def run(command: list[str], timeout: float = 10.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, timeout=timeout, check=False)


def proc_info(pid: int) -> dict[str, object] | None:
    try:
        stat = pathlib.Path(f"/proc/{pid}/stat").read_text().split()
        status = pathlib.Path(f"/proc/{pid}/status").read_text().splitlines()
        cmdline = pathlib.Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode()
        rss = int(next(line.split()[1] for line in status if line.startswith("VmRSS:")))
        threads = int(next(line.split()[1] for line in status if line.startswith("Threads:")))
        fd_count = len(list(pathlib.Path(f"/proc/{pid}/fd").iterdir()))
        return {
            "pid": pid,
            "ppid": int(stat[3]),
            "comm": stat[1].strip("()"),
            "cpu_ticks": int(stat[13]) + int(stat[14]),
            "rss_kib": rss,
            "threads": threads,
            "fds": fd_count,
            "cmdline": cmdline,
        }
    except (FileNotFoundError, PermissionError, StopIteration, ValueError):
        return None


def descendants(root_pid: int) -> set[int]:
    processes: dict[int, int] = {}
    for item in pathlib.Path("/proc").iterdir():
        if not item.name.isdigit():
            continue
        info = proc_info(int(item.name))
        if info is not None:
            processes[int(info["pid"])] = int(info["ppid"])
    selected = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, ppid in processes.items():
            if ppid in selected and pid not in selected:
                selected.add(pid)
                changed = True
    return selected


def system_memory() -> dict[str, int]:
    fields: dict[str, int] = {}
    for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
        key, value = line.split(":", 1)
        fields[key] = int(value.strip().split()[0])
    return {key: fields.get(key, 0) for key in ("MemTotal", "MemAvailable", "SwapTotal", "SwapFree")}


def max_temperature_c() -> float:
    values = []
    for path in pathlib.Path("/sys/devices/virtual/thermal").glob("thermal_zone*/temp"):
        try:
            values.append(int(path.read_text().strip()) / 1000.0)
        except (OSError, ValueError):
            pass
    return max(values, default=0.0)


def runtime_status(bin_dir: pathlib.Path, socket: pathlib.Path) -> tuple[str, dict[str, dict[str, str]]]:
    completed = run([str(bin_dir / "cockpit-ctl"), "runtime", "status", "--socket", str(socket)])
    modules: dict[str, dict[str, str]] = {}
    for line in completed.stdout.splitlines():
        if not line.startswith("module="):
            continue
        fields = dict(token.split("=", 1) for token in line.split() if "=" in token)
        modules[fields["module"]] = fields
    return completed.stdout, modules


def json_command(command: list[str]) -> dict[str, object]:
    completed = run(command, timeout=20.0)
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError:
        return {"returncode": completed.returncode, "stdout": completed.stdout, "stderr": completed.stderr}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--navigator-pid", type=int, required=True)
    parser.add_argument("--navigator-socket", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--duration-seconds", type=int, default=3600)
    parser.add_argument("--interaction-interval-seconds", type=int, default=300)
    parser.add_argument("--ui-click-interval-seconds", type=int, default=120)
    parser.add_argument("--llm-fault-at-seconds", type=int, default=2400)
    parser.add_argument("--camera-fault-at-seconds", type=int, default=2700)
    parser.add_argument("--kws-status", default="ENABLED")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    bin_dir = args.build_dir / "bin"
    started = time.monotonic()
    previous_cpu: dict[int, tuple[int, float]] = {}
    samples: list[dict[str, object]] = []
    events: list[dict[str, object]] = []
    next_detail = 0.0
    next_interaction = float(args.interaction_interval_seconds)
    next_progress = 0.0
    next_ui_click = float(args.ui_click_interval_seconds)
    ui_pages = [(0, 280, 607), (1, 448, 607), (4, 726, 607), (5, 935, 607)]
    ui_page_index = 0
    llama_fault_done = False
    camera_fault_done = False
    hz = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    prompts = ["打开相机", "用一句话介绍西安"]
    prompt_index = 0
    stop_requested = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    while True:
        now = time.monotonic()
        elapsed = now - started
        if elapsed >= args.duration_seconds or stop_requested:
            break
        if not pathlib.Path(f"/proc/{args.navigator_pid}").exists():
            events.append({"elapsed_s": elapsed, "event": "navigator_exit"})
            break

        pids = descendants(args.navigator_pid)
        infos = [info for pid in pids if (info := proc_info(pid)) is not None]
        process_rows = []
        for info in infos:
            pid = int(info["pid"])
            ticks = int(info.pop("cpu_ticks"))
            prior = previous_cpu.get(pid)
            cpu_percent = 0.0 if prior is None else (ticks - prior[0]) / hz / (now - prior[1]) * 100.0
            previous_cpu[pid] = (ticks, now)
            info["cpu_percent"] = round(cpu_percent, 2)
            process_rows.append(info)

        sample: dict[str, object] = {
            "elapsed_s": round(elapsed, 1),
            "system": system_memory(),
            "loadavg": pathlib.Path("/proc/loadavg").read_text().strip(),
            "max_temperature_c": max_temperature_c(),
            "process_tree_rss_kib": sum(int(row["rss_kib"]) for row in process_rows),
            "process_tree_threads": sum(int(row["threads"]) for row in process_rows),
            "process_tree_fds": sum(int(row["fds"]) for row in process_rows),
            "processes": process_rows,
        }

        if elapsed >= next_detail:
            status_text, modules = runtime_status(bin_dir, args.navigator_socket)
            sample["runtime_status"] = status_text
            sample["modules"] = modules
            sample["camera"] = json_command(
                [str(bin_dir / "camera-ctl"), "--status", "--output", "json", "--config", str(args.config)]
            )
            sample["voice"] = json_command(
                [str(bin_dir / "voice-ctl"), "--status", "--output", "json", "--config", str(args.config)]
            )
            tegra = run(["timeout", "2", "tegrastats", "--interval", "1000"], timeout=4.0)
            sample["tegrastats"] = tegra.stdout.splitlines()[-1] if tegra.stdout.splitlines() else ""
            next_detail += 60.0

        samples.append(sample)

        if elapsed >= next_interaction:
            prompt = prompts[prompt_index % len(prompts)]
            voice_status = json_command(
                [str(bin_dir / "voice-ctl"), "--status", "--output", "json", "--config", str(args.config)]
            )
            if voice_status.get("state") != "INTERACTION_STATE_IDLE":
                events.append(
                    {
                        "elapsed_s": round(elapsed, 1),
                        "event": "voice_request_skipped_busy",
                        "prompt": prompt,
                        "state": voice_status.get("state", "unavailable"),
                    }
                )
            else:
                completed = run(
                    [str(bin_dir / "voice-ctl"), "--process", prompt, "--output", "json", "--config", str(args.config)],
                    timeout=60.0,
                )
                events.append(
                    {
                        "elapsed_s": round(elapsed, 1),
                        "event": "voice_request",
                        "prompt": prompt,
                        "returncode": completed.returncode,
                        "stdout": completed.stdout.strip(),
                        "stderr": completed.stderr.strip(),
                    }
                )
                prompt_index += 1
            next_interaction += float(args.interaction_interval_seconds)

        if elapsed >= next_ui_click:
            display = os.environ.get("DISPLAY", ":0")
            xauthority = os.environ.get("XAUTHORITY", "")
            xdotool = os.environ.get("XDO_TOOL", "xdotool")
            page, x, y = ui_pages[ui_page_index % len(ui_pages)]
            if shutil.which(xdotool) is None:
                events.append(
                    {
                        "elapsed_s": round(elapsed, 1),
                        "event": "ui_page_click_skipped",
                        "reason": f"{xdotool} not installed",
                    }
                )
            else:
                search = run([xdotool, "search", "--name", "Smart Cockpit System"], timeout=5.0)
                window_ids = [line for line in search.stdout.splitlines() if line.strip()]
                if window_ids:
                    env = os.environ.copy()
                    env["DISPLAY"] = display
                    if xauthority:
                        env["XAUTHORITY"] = xauthority
                    run([xdotool, "mousemove", "--sync", str(x), str(y)], timeout=5.0)
                    run([xdotool, "click", "1"], timeout=5.0)
                    events.append({"elapsed_s": round(elapsed, 1), "event": "ui_page_click", "page": page})
            ui_page_index += 1
            next_ui_click += float(args.ui_click_interval_seconds)

        if args.llm_fault_at_seconds >= 0 and elapsed >= args.llm_fault_at_seconds and not llama_fault_done:
            llama = next((row for row in process_rows if row["comm"] == "llama-server"), None)
            if llama is not None:
                os.kill(int(llama["pid"]), signal.SIGKILL)
                events.append({"elapsed_s": round(elapsed, 1), "event": "kill_llama", "pid": llama["pid"]})
                llama_fault_done = True

        if args.camera_fault_at_seconds >= 0 and elapsed >= args.camera_fault_at_seconds and not camera_fault_done:
            _, modules = runtime_status(bin_dir, args.navigator_socket)
            camera = modules.get("camera_driver", {})
            if camera.get("pid", "0").isdigit() and int(camera["pid"]) > 0:
                os.kill(int(camera["pid"]), signal.SIGKILL)
                events.append({"elapsed_s": round(elapsed, 1), "event": "kill_camera", "pid": camera["pid"]})
                camera_fault_done = True

        if elapsed >= next_progress:
            print(
                f"progress minute={int(elapsed // 60)} rss_kib={sample['process_tree_rss_kib']} "
                f"available_kib={sample['system']['MemAvailable']} temp_c={sample['max_temperature_c']}",
                flush=True,
            )
            next_progress += 60.0
        time.sleep(10.0)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(
            {
                "duration_requested_s": args.duration_seconds,
                "duration_observed_s": round(time.monotonic() - started, 1),
                "interrupted": stop_requested,
                "kws_status": args.kws_status,
                "samples": samples,
                "events": events,
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"wrote {args.output}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
