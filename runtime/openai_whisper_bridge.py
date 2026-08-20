#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

"""Optional JSON-lines bridge for the OpenAI Whisper Python package.

The C++ core never launches this script itself. A platform host starts it,
sends one JSON object per stdin line, and reads one JSON object per stdout
line. Dependencies are imported only after an initialize request, so the
portable build and its tests do not require Python, PyTorch, NumPy, or Whisper.
"""

from __future__ import annotations

import json
import math
import sys
from typing import Any


TARGET_SAMPLE_RATE = 16_000


def response(status: str, **fields: Any) -> None:
    """Write exactly one protocol response and flush it for a waiting host."""
    print(json.dumps({"status": status, **fields}, ensure_ascii=False), flush=True)


def validate_request(payload: object) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ValueError("request must be a JSON object")
    operation = payload.get("op")
    if operation not in {"initialize", "transcribe"}:
        raise ValueError("op must be 'initialize' or 'transcribe'")
    return payload


def prepare_audio(samples: object, sample_rate: object, numpy: Any) -> Any:
    if not isinstance(sample_rate, int) or isinstance(sample_rate, bool) or sample_rate <= 0:
        raise ValueError("sample_rate must be a positive integer")
    if not isinstance(samples, list):
        raise ValueError("samples must be a JSON array")
    if any(not isinstance(sample, (int, float)) or isinstance(sample, bool) or not math.isfinite(sample)
           for sample in samples):
        raise ValueError("samples must contain finite numeric values")

    audio = numpy.asarray(samples, dtype=numpy.float32)
    if sample_rate == TARGET_SAMPLE_RATE or audio.size == 0:
        return audio

    target_size = max(1, round(audio.size * TARGET_SAMPLE_RATE / sample_rate))
    source_positions = numpy.arange(audio.size, dtype=numpy.float64)
    target_positions = numpy.linspace(0, max(audio.size - 1, 0), target_size)
    return numpy.interp(target_positions, source_positions, audio).astype(numpy.float32)


def normalize_segments(result: object) -> list[dict[str, object]]:
    if not isinstance(result, dict):
        raise ValueError("Whisper returned an invalid transcription result")
    raw_segments = result.get("segments", [])
    if not isinstance(raw_segments, list):
        raise ValueError("Whisper returned invalid transcript segments")

    segments: list[dict[str, object]] = []
    for segment in raw_segments:
        if not isinstance(segment, dict):
            raise ValueError("Whisper returned a non-object transcript segment")
        start = segment.get("start")
        end = segment.get("end")
        text = segment.get("text")
        if (not isinstance(start, (int, float)) or isinstance(start, bool) or
                not isinstance(end, (int, float)) or isinstance(end, bool) or
                not isinstance(text, str) or not math.isfinite(start) or
                not math.isfinite(end) or start < 0 or end < start):
            raise ValueError("Whisper returned an invalid transcript segment")
        segments.append({
            "start_seconds": float(start),
            "end_seconds": float(end),
            "text": text,
        })
    return segments


def main() -> int:
    model: Any = None
    language = "auto"
    numpy: Any = None

    for raw_line in sys.stdin:
        try:
            payload = validate_request(json.loads(raw_line))
            if payload["op"] == "initialize":
                model_path = payload.get("model_path")
                requested_language = payload.get("language")
                if not isinstance(model_path, str) or not model_path:
                    raise ValueError("model_path must be a non-empty string")
                if not isinstance(requested_language, str) or not requested_language:
                    raise ValueError("language must be a non-empty string")
                try:
                    import numpy as imported_numpy
                    import whisper
                except ImportError as error:
                    response("unavailable", message=f"OpenAI Whisper dependency is unavailable: {error}")
                    continue

                numpy = imported_numpy
                model = whisper.load_model(model_path)
                language = requested_language
                response("ready")
                continue

            if model is None or numpy is None:
                response("error", message="bridge has not been initialized")
                continue

            audio = prepare_audio(payload.get("samples"), payload.get("sample_rate"), numpy)
            options: dict[str, object] = {"fp16": False, "verbose": False}
            if language != "auto":
                options["language"] = language
            transcript = model.transcribe(audio, **options)
            response("ready", segments=normalize_segments(transcript))
        except (TypeError, ValueError, json.JSONDecodeError) as error:
            response("error", message=str(error))
        except Exception as error:  # Runtime package errors must not break JSON-lines framing.
            response("error", message=f"OpenAI Whisper bridge failed: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
