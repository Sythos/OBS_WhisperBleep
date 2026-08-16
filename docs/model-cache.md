<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

> Models and cache

M0 does not download, convert or bundle Whisper weights. The code contains only
placeholder interfaces and states for the catalog, downloader and model
manager.

Future implementation must keep weights outside the repository and use a
per-user data cache resolved through platform APIs. Every download must use an
approved manifest, checksum verification, format checks, asynchronous
activation and rollback to the previous model on failure.
