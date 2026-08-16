<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Development setup

=> M0 prerequisites

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

The M0 build uses the OBS-independent plugin stub. The Whisper runtime, OBS SDK,
CUDA and model weights are not required for M0 verification.

=> M1 native OBS setup

M1 requires an OBS SDK that matches the target OBS installation and exposes the
native filter, properties and audio callback interfaces. The CMake cache option
for the SDK location is:

```text
-DOBS_SDK_DIR=C:\path\to\obs-sdk
```

For example, an M1 configure command is:

```text
cmake -S . -B build/m1 -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBS_SDK_DIR=C:\path\to\obs-sdk -DOBS_LIB=C:\path\to\libobs.lib -DOBS_WHISPERBLEEP_BUILD_NATIVE_MODULE=ON -DOBS_WHISPERBLEEP_BUILD_PLUGIN_STUB=OFF
cmake --build build/m1
ctest --test-dir build/m1 --output-on-failure
```

`OBS_SDK_DIR` supplies the OBS headers and `OBS_LIB` supplies the `libobs`
library when CMake cannot locate it automatically. The native target is built
only when `OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE=ON`; the default M0/M1 CI build
does not require an OBS SDK.

The M1 verification must cover filter registration, instance lifecycle,
pass-through audio, initial Properties defaults, settings updates and settings
persistence. Keep these tests independent from Whisper weights, network access,
Python, PyTorch and CUDA; those dependencies belong to later milestones.
