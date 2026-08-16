<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> OBS WhisperBleep

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/) [![GitHub release](https://img.shields.io/github/v/release/Sythos/OBS_WhisperBleep?display_name=tag&sort=semver)](https://github.com/Sythos/OBS_WhisperBleep/releases/latest) [![Last commit](https://img.shields.io/github/last-commit/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/commits/main/) [![Open issues](https://img.shields.io/github/issues/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/issues)<br>
[![License](https://img.shields.io/github/license/Sythos/OBS_WhisperBleep)](https://github.com/Sythos/OBS_WhisperBleep/blob/main/LICENSE) [![Git](https://img.shields.io/badge/Git-Repository-F05032?logo=git&logoColor=white)](https://git-scm.com/) [![GitHub](https://img.shields.io/badge/GitHub-Sythos%2FOBS_WhisperBleep-181717?logo=github&logoColor=white)](https://github.com/Sythos/OBS_WhisperBleep)

OBS WhisperBleep is a native OBS Studio audio filter designed to censor
configured words and phrases in near real time.

The idea is simple: the filter receives the audio that normally flows through
OBS, keeps a small buffer long enough to recognize speech with Whisper and,
when a match is found, replaces the affected interval with a sound selected by
the user. The replacement can be a beep, a duck sound, a bark or a custom audio
file.

The configuration is intended to live entirely in the OBS Properties panel.
The plugin will handle buffering, timestamps and expensive work on dedicated
workers so the OBS audio callback is not blocked by downloads, disk I/O or
inference.

=> Practical synchronization note

To make audio/video synchronization easier to maintain, the recommended setup
is three consecutive audio delays of **500 msec (0.5 seconds) each**, for a
total delay of approximately **1.5 seconds**. Verify the final value against
the actual scene and OBS chain: the required delay depends on the Whisper model,
hardware and total system latency.

=> Project status

This repository starts from an architectural scaffold. The Whisper runtime,
model catalog and downloads, GPU backends, packaging and multi-platform
releases will be defined and verified in later milestones.

=> M0: local scaffold

The first reference version is `0.1.0`. This phase contains the initial
separation between core, model/runtime, OBS and platform code, a compilable
plugin stub, deterministic tests, documentation and the GitHub Actions layout.

With CMake 3.20 or newer and a C++20 compiler, verify the scaffold with:

```text
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

The M0 stub is not yet an operational OBS filter: it does not load the OBS API,
run inference or download models. Those capabilities belong to later
milestones.

=> Technologies

The project is built around the following technologies:

- C++20 for the native OBS Studio plugin and real-time audio processing.
- CMake for cross-platform configuration, builds and packaging.
- [OpenAI Whisper](https://github.com/openai/whisper) for speech-recognition
  models and the AI processing structure.
- Python for model tooling and supporting automation.
- [PyTorch](https://pytorch.org/) for CPU execution and GPU acceleration through
  CUDA.

=> Inspiration and disclaimer

OBS WhisperBleep is original code and is developed without forking CleanStream.
The project is inspired by Royshil's CleanStream idea; at the time of writing,
the related Git repository appeared abandoned and had not been updated for more
than nine months. I was unable to establish contact or receive a response by
email, repository messages or social media.

This project claim to NOT incorporate CleanStream code, commits, assets or model weights.
Any third-party component will be added only after its origin,
license and attribution have been verified.
