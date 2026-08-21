# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

FROM python:3.11-slim-bookworm

ARG OBS_WHISPERBLEEP_VERSION=development

LABEL org.opencontainers.image.title="OBS WhisperBleep runtime"
LABEL org.opencontainers.image.description="CPU OpenAI Whisper JSON-lines runtime bridge for OBS WhisperBleep"
LABEL org.opencontainers.image.version="${OBS_WHISPERBLEEP_VERSION}"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.opencontainers.image.source="https://github.com/Sythos/OBS_WhisperBleep"

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

RUN apt-get update \
    && apt-get install --yes --no-install-recommends ffmpeg ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install the CPU-only PyTorch wheel explicitly, then the Whisper package.
# Model weights are intentionally not downloaded into the image.
RUN python -m pip install --no-cache-dir --upgrade pip \
    && python -m pip install --no-cache-dir \
       --index-url https://download.pytorch.org/whl/cpu \
       "torch==2.7.1" \
    && python -m pip install --no-cache-dir \
       "numpy>=1.26,<3" \
       "openai-whisper==20250625"

COPY runtime/openai_whisper_bridge.py /opt/obs-whisperbleep/runtime/openai_whisper_bridge.py

WORKDIR /opt/obs-whisperbleep

ENTRYPOINT ["python", "/opt/obs-whisperbleep/runtime/openai_whisper_bridge.py"]
