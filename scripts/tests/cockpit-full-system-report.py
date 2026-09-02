#!/usr/bin/env python3
# flake8: noqa: E501
"""Summarize Cockpit full-system soak JSON into a compact text report."""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics


def process_value(sample: dict, comm: str, field: str) -> int:
    return sum(int(row[field]) for row in sample.get("processes", []) if row.get("comm") == comm)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    data = json.loads(args.input.read_text(encoding="utf-8"))
    samples = data.get("samples", [])
    if not samples:
        raise SystemExit("soak report contains no samples")

    rss = [int(sample["process_tree_rss_kib"]) for sample in samples]
    available = [int(sample["system"]["MemAvailable"]) for sample in samples]
    swap_used = [
        int(sample["system"]["SwapTotal"]) - int(sample["system"]["SwapFree"])
        for sample in samples
    ]
    temperatures = [float(sample["max_temperature_c"]) for sample in samples]
    last_detailed = next((sample for sample in reversed(samples) if "modules" in sample), {})
    modules = last_detailed.get("modules", {})
    camera = last_detailed.get("camera", {})
    voice = last_detailed.get("voice", {})
    roles = ["cockpit-ui", "agent", "llama-server", "camera_driver", "audio_driver"]
    role_lines = []
    for role in roles:
        values = [process_value(sample, role, "rss_kib") for sample in samples]
        role_lines.append(
            f"{role}_rss_kib: start={values[0]} end={values[-1]} max={max(values)}"
        )
    unexpected = [
        name
        for name, fields in modules.items()
        if fields.get("state") != "running" and name in {
            "transfer", "vehicle_driver", "audio_driver", "camera_driver", "agent", "hmi",
            "recording", "media", "sentinel"
        }
    ]
    restart_summary = ", ".join(
        f"{name}={fields.get('restarts', '0')}" for name, fields in sorted(modules.items())
        if fields.get("restarts", "0") != "0"
    ) or "none"
    voice_events = [event for event in data.get("events", []) if event.get("event") == "voice_request"]
    successful_voice = sum(event.get("returncode") == 0 for event in voice_events)
    camera_frames = int(camera.get("frames_received", 0) or 0) if isinstance(camera, dict) else 0
    camera_drops = int(camera.get("frames_dropped", 0) or 0) if isinstance(camera, dict) else 0
    voice_metrics = voice.get("metrics", {}) if isinstance(voice, dict) else {}
    lines = [
        "Cockpit V1 Full-System Soak Summary",
        f"duration_requested_s: {data.get('duration_requested_s')}",
        f"duration_observed_s: {data.get('duration_observed_s')}",
        f"sample_count: {len(samples)}",
        f"process_tree_rss_kib: start={rss[0]} end={rss[-1]} max={max(rss)}",
        f"process_tree_rss_last_quarter_mean_kib: {round(statistics.mean(rss[-max(1, len(rss)//4):]))}",
        f"system_mem_available_min_kib: {min(available)}",
        f"system_swap_used_max_kib: {max(swap_used)}",
        f"temperature_max_c: {max(temperatures):.1f}",
        *role_lines,
        f"unexpected_module_states: {','.join(unexpected) if unexpected else 'none'}",
        f"module_restarts: {restart_summary}",
        f"camera_frames_received: {camera_frames}",
        f"camera_frames_dropped: {camera_drops}",
        f"voice_requests: {len(voice_events)} successful={successful_voice}",
        f"voice_processing_errors: {voice_metrics.get('processing_errors', 'missing')}",
        f"voice_tts_timeouts: {voice_metrics.get('tts_timeouts', 'missing')}",
        f"kws_status: {data.get('kws_status')}",
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
