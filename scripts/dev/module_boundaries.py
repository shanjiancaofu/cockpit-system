#!/usr/bin/env python3
"""Enforce V2 source ownership and declared interface dependencies."""
import argparse
from pathlib import Path
import re

MODULES = frozenset({
    'navigator', 'driver', 'voice', 'agent', 'eunomia', 'hawkeye',
    'exchange', 'transfer', 'carcloud', 'debugger', 'spider', 'geois',
    'hmi', 'shield', 'upgrader', 'vanguard', 'ros',
})
SOURCE_SUFFIXES = {'.h', '.cc', '.cu'}

# Shared headers stay alongside their implementation in functional directories.
# This list declares cross-module contracts without a separate header tree.
EXPORTED_HEADERS = frozenset({
    'cockpit/agent/llm/llama_server_local_llm_client.h',
    'cockpit/agent/llm/llama_server_process.h',
    'cockpit/agent/llm/local_llm_client.h',
    'cockpit/agent/llm/mock_local_llm_client.h',
    'cockpit/driver/audio/frames/audio_frame.h',
    'cockpit/driver/audio/frames/pcm_format.h',
    'cockpit/driver/audio/frames/spsc_ring_buffer.h',
    'cockpit/driver/audio/transport/audio_stream_protocol.h',
    'cockpit/driver/audio/wav/wav_file.h',
    'cockpit/driver/camera/capture/argus_isp_preview_source.h',
    'cockpit/driver/camera/capture/camera_preview_source.h',
    'cockpit/driver/camera/capture/gstreamer_preview_pipeline.h',
    'cockpit/driver/camera/capture/native_argus_capture.h',
    'cockpit/driver/camera/frames/camera_frame.h',
    'cockpit/driver/camera/frames/camera_frame_sink.h',
    'cockpit/driver/camera/frames/frame_broker.h',
    'cockpit/driver/camera/frames/latest_frame_buffer.h',
    'cockpit/driver/camera/frames/native_frame.h',
    'cockpit/driver/camera/photo/jpeg_encoder.h',
    'cockpit/driver/camera/shared_memory/shared_frame_buffer.h',
    'cockpit/driver/camera/transform/vic_transform.h',
    'cockpit/driver/camera/tuning/camera_tuning.h',
    'cockpit/driver/can/can_frame.h',
    'cockpit/driver/vehicle/chassis_can_codec.h',
    'cockpit/driver/vehicle/chassis_event.h',
    'cockpit/driver/vehicle/chassis_odometry_time_mapper.h',
    'cockpit/driver/vehicle/chassis_state.h',
    'cockpit/driver/vehicle/vehicle_state.h',
    'cockpit/eunomia/camera/camera_calibrator.h',
    'cockpit/exchange/abi/module_api.h',
    'cockpit/exchange/contracts/camera/camera_calibration.h',
    'cockpit/exchange/contracts/camera/camera_calibration_loader.h',
    'cockpit/exchange/contracts/camera/camera_info.h',
    'cockpit/exchange/contracts/voice/speech_transcript.h',
    'cockpit/exchange/foundation/base/macros.h',
    'cockpit/exchange/foundation/build/build_info.h',
    'cockpit/exchange/foundation/config/system_config.h',
    'cockpit/exchange/foundation/event/event_queue.h',
    'cockpit/exchange/foundation/event/message_bus.h',
    'cockpit/exchange/foundation/health/service_health.h',
    'cockpit/exchange/foundation/ipc/shared_memory_region.h',
    'cockpit/exchange/foundation/json/json.h',
    'cockpit/exchange/foundation/logging/logger.h',
    'cockpit/exchange/foundation/runtime/args.h',
    'cockpit/exchange/foundation/runtime/module.h',
    'cockpit/exchange/foundation/runtime/module_manager.h',
    'cockpit/exchange/foundation/runtime/process_runtime.h',
    'cockpit/exchange/foundation/time/time.h',
    'cockpit/shield/chassis/chassis_safety_adapter.h',
    'cockpit/spider/recording/client/recording_event_publisher.h',
})


def inspect(root):
    errors = []
    product = root / 'cockpit'
    actual = {path.name for path in product.iterdir() if path.is_dir()}
    if actual != MODULES:
        errors.append('module directories differ: '
                      f'missing={sorted(MODULES - actual)} '
                      f'unexpected={sorted(actual - MODULES)}')
    if (root / 'agent').exists():
        errors.append('legacy root agent/ must not coexist with V2 modules')
    for source in sorted(product.rglob('*')):
        if source.suffix not in SOURCE_SUFFIXES or not source.is_file():
            continue
        relative = source.relative_to(root)
        owner = relative.parts[1]
        for line, text in enumerate(source.read_text().splitlines(), 1):
            match = re.match(r'\s*#include\s+"([^"\n]+)"', text)
            if not match:
                continue
            if match[1].startswith('cockpit/'):
                target = Path(match[1])
            else:
                candidate = (source.parent / match[1]).resolve()
                if not candidate.is_file():
                    continue  # Generated or third-party include search paths.
                try:
                    target = candidate.relative_to(root)
                except ValueError:
                    continue
            if len(target.parts) < 3 or target.parts[0] != 'cockpit':
                continue
            target_owner = target.parts[1]
            label = f'{relative}:{line}: {match[1]}'
            if not (root / target).is_file():
                errors.append(label + ': missing project header')
                continue
            if (target_owner != owner
                    and target.as_posix() not in EXPORTED_HEADERS):
                errors.append(label + ': foreign private implementation')
            if (relative.as_posix() in EXPORTED_HEADERS
                    and target.as_posix() not in EXPORTED_HEADERS):
                errors.append(label + ': interface header leaks '
                              'a private header')
            platform = ('cockpit', 'driver', 'platform')
            foundation = ('cockpit', 'exchange', 'foundation')
            if (relative.parts[:3] == platform
                    and target.parts[:3] != platform):
                errors.append(label + ': hardware adapter uses project layer')
            if (relative.parts[:3] == foundation
                    and target_owner != 'exchange'):
                errors.append(label + ': foundation depends on business code')
            if (owner == 'hmi'
                    and target.parts[:2] == ('cockpit', 'driver')
                    and 'capture' in target.parts):
                errors.append(label + ': HMI must consume frames/IPC')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path,
                        default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    errors = inspect(args.root.resolve())
    for error in errors:
        print(error)
    if not errors:
        print('V2 module boundaries passed: '
              f'{len(MODULES)} modules, declared interfaces only')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
