#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

"""Run a deterministic protocol or real-model smoke test for the Whisper bridge.

The default mode never downloads a model. It starts the bridge with an absolute
sentinel path and accepts the expected unavailable/error response. Supplying
``--model-path`` enables the real initialization and transcription check; the
file must already exist in the user's verified model cache. Audio input is
optional and is decoded from a local PCM WAV file without any network access.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path
from typing import Any


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python executable used to launch the bridge (default: current interpreter)",
    )
    parser.add_argument(
        "--bridge",
        default=str(Path(__file__).resolve().with_name("openai_whisper_bridge.py")),
        help="Path to openai_whisper_bridge.py",
    )
    parser.add_argument(
        "--model-path",
        help="Existing absolute model file; omitting it runs only the no-model smoke test",
    )
    parser.add_argument(
        "--audio-path",
        help="Existing PCM WAV file for the real-model transcription check",
    )
    parser.add_argument(
        "--language",
        default="en",
        help="Whisper language policy sent to the bridge (default: en)",
    )
    parser.add_argument(
        "--max-seconds",
        type=float,
        default=3.0,
        help="Maximum local WAV duration submitted to the bridge (default: 3 seconds)",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=120.0,
        help="Maximum bridge process duration (default: 120 seconds)",
    )
    return parser.parse_args()


def require_absolute_file(value: str, label: str) -> Path:
    path = Path(value).expanduser()
    if not path.is_absolute():
        raise ValueError(f"{label} must be an absolute path")
    if not path.is_file():
        raise ValueError(f"{label} does not exist: {path}")
    return path.resolve()


def load_pcm_wav(path: Path, max_seconds: float) -> tuple[int, list[float]]:
    if not math.isfinite(max_seconds) or max_seconds <= 0:
        raise ValueError("max-seconds must be a finite positive number")

    with wave.open(os.fspath(path), "rb") as handle:
        if handle.getcomptype() != "NONE":
            raise ValueError("audio-path must be an uncompressed PCM WAV file")
        sample_rate = handle.getframerate()
        channels = handle.getnchannels()
        sample_width = handle.getsampwidth()
        if sample_rate <= 0 or channels <= 0:
            raise ValueError("audio-path has invalid sample-rate or channel count")
        if sample_width != 2:
            raise ValueError("audio-path must contain 16-bit PCM samples")

        frame_limit = max(1, int(sample_rate * max_seconds))
        raw = handle.readframes(frame_limit)

    values = struct.iter_unpack("<h", raw)
    mono: list[float] = []
    frame: list[int] = []
    for (sample,) in values:
        frame.append(sample)
        if len(frame) == channels:
            mono.append(sum(frame) / (len(frame) * 32768.0))
            frame.clear()
    if frame:
        mono.append(sum(frame) / (len(frame) * 32768.0))
    return sample_rate, mono


def silence() -> tuple[int, list[float]]:
    return 16_000, [0.0] * 8_000


def run_bridge(
    python_executable: str,
    bridge_path: Path,
    model_path: Path,
    language: str,
    sample_rate: int,
    samples: list[float],
    timeout_seconds: float,
) -> tuple[int, list[dict[str, Any]], str, str]:
    initialize = {
        "op": "initialize",
        "model_path": os.fspath(model_path),
        "language": language,
    }
    transcribe = {
        "op": "transcribe",
        "sample_rate": sample_rate,
        "samples": samples,
    }
    payload = "\n".join(
        json.dumps(item, separators=(",", ":")) for item in (initialize, transcribe)
    ) + "\n"

    completed = subprocess.run(
        [python_executable, os.fspath(bridge_path)],
        input=payload,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
        check=False,
    )
    records: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        if not isinstance(record, dict):
            raise ValueError("bridge returned a non-object JSON response")
        records.append(record)
    return completed.returncode, records, completed.stdout, completed.stderr


def validate_segments(record: dict[str, Any]) -> None:
    segments = record.get("segments")
    if not isinstance(segments, list):
        raise ValueError("ready transcription response has no segments array")
    for segment in segments:
        if not isinstance(segment, dict):
            raise ValueError("transcription segment is not a JSON object")
        start = segment.get("start_seconds")
        end = segment.get("end_seconds")
        text = segment.get("text")
        if (
            not isinstance(start, (int, float))
            or isinstance(start, bool)
            or not isinstance(end, (int, float))
            or isinstance(end, bool)
            or not isinstance(text, str)
            or not math.isfinite(start)
            or not math.isfinite(end)
            or start < 0
            or end < start
        ):
            raise ValueError("transcription segment has an invalid shape")


def run() -> int:
    args = parse_arguments()
    bridge_path = require_absolute_file(args.bridge, "bridge")
    python_executable = os.fspath(Path(args.python).expanduser())
    if not Path(python_executable).is_file() and not args.python:
        raise ValueError("python executable was not found")

    model_path: Path
    real_model = args.model_path is not None
    if real_model:
        model_path = require_absolute_file(args.model_path, "model-path")
    else:
        model_path = Path(tempfile.gettempdir()) / (
            f"obs-whisperbleep-missing-{os.getpid()}.model"
        )
        if model_path.exists():
            raise RuntimeError(
                "the no-model smoke sentinel unexpectedly exists; remove it and retry"
            )
        sample_rate, samples = silence()
        return run_smoke(
            python_executable,
            bridge_path,
            model_path,
            args.language,
            sample_rate,
            samples,
            args.timeout_seconds,
            real_model=False,
        )

    if args.audio_path:
        audio_path = require_absolute_file(args.audio_path, "audio-path")
        sample_rate, samples = load_pcm_wav(audio_path, args.max_seconds)
    else:
        sample_rate, samples = silence()
    return run_smoke(
        python_executable,
        bridge_path,
        model_path,
        args.language,
        sample_rate,
        samples,
        args.timeout_seconds,
        real_model=True,
    )


def run_smoke(
    python_executable: str,
    bridge_path: Path,
    model_path: Path,
    language: str,
    sample_rate: int,
    samples: list[float],
    timeout_seconds: float,
    *,
    real_model: bool,
) -> int:
    returncode, records, stdout, stderr = run_bridge(
        python_executable,
        bridge_path,
        model_path,
        language,
        sample_rate,
        samples,
        timeout_seconds,
    )
    if len(records) < 2:
        raise RuntimeError(
            "bridge returned fewer than two protocol responses; "
            f"stdout={stdout!r} stderr={stderr!r}"
        )
    if records[0].get("status") != "ready":
        if real_model:
            raise RuntimeError(f"model initialization failed: {records[0]}")
        if records[0].get("status") not in {"unavailable", "error"}:
            raise RuntimeError(f"unexpected no-model initialization response: {records[0]}")
        if records[1].get("status") not in {"unavailable", "error"}:
            raise RuntimeError(f"unexpected no-model transcription response: {records[1]}")
        print("Bridge protocol smoke test passed; no model was loaded.")
        return 0

    if not real_model:
        raise RuntimeError("bridge initialized a model during the no-model smoke test")
    if records[1].get("status") != "ready":
        raise RuntimeError(f"transcription failed: {records[1]}")
    validate_segments(records[1])
    print(
        "Bridge real-model smoke test passed: "
        f"{len(records[1]['segments'])} transcript segments returned."
    )
    returncode_message = f" (process exit code {returncode})" if returncode else ""
    if returncode:
        raise RuntimeError(f"bridge exited unsuccessfully{returncode_message}: {stderr}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
