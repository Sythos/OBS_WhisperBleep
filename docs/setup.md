<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> M0 development setup

Minimum requirements:

- CMake 3.20 or newer;
- a C++20 compiler supported by the platform;
- Git.

Configure and build:

```text
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

The Whisper runtime, OBS SDK, CUDA and model weights are not required for M0
verification.
