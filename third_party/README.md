<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Third-party inventory

M0 contains no third-party source code, model weights, audio or other bundled
resources.

Every future component must be recorded with its source, version or commit,
license, copyright, redistribution terms and a corresponding `NOTICE` entry.
GitHub Actions and scripts derived from the OBS template must be inventoried
separately from the plugin's runtime dependencies.

=> Optional host runtime inventory

The following components are documented for the user-managed Python/Whisper
environment but are not bundled in the source tree, installers or release
artifacts:

- [OpenAI Whisper](https://github.com/openai/whisper): MIT;
- [PyTorch](https://pytorch.org/): BSD-style license;
- [NumPy](https://numpy.org/): BSD-3-Clause;
- [tiktoken](https://github.com/openai/tiktoken): MIT;
- [Python](https://www.python.org/psf/license/): Python Software Foundation
  License;
- [FFmpeg](https://ffmpeg.org/legal.html): host-provided; the exact binary's
  LGPL/GPL configuration governs any future redistribution.

This inventory does not grant permission to copy these components into a
release. A future bundled runtime requires exact versions, license texts,
notices, source or offer obligations and a fresh package-content review.
