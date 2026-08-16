// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#pragma once

namespace obs_whisperbleep::obs {

[[nodiscard]] const char* plugin_description() noexcept;
[[nodiscard]] bool plugin_load() noexcept;

}  // namespace obs_whisperbleep::obs
