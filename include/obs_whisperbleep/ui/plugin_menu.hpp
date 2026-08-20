// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obs_whisperbleep/ui/localization.hpp"

namespace obs_whisperbleep::ui {

enum class MenuSection { general, audio, models, matching, about };

enum class MenuAction { check_updates };

struct MenuItem {
  MenuSection section = MenuSection::general;
  std::string id;
  std::string label;
};

struct ContextualPane {
  MenuSection section = MenuSection::general;
  std::string title;
  std::string description;
  std::vector<MenuAction> actions;
};

// Commands describe work that the host/UI layer must perform. The contract
// deliberately does not open a browser or perform network I/O itself.
struct MenuCommand {
  MenuAction action = MenuAction::check_updates;
  std::string installed_version;
  std::string target_url;
};

[[nodiscard]] const char* menu_section_name(MenuSection section) noexcept;
[[nodiscard]] const char* menu_action_name(MenuAction action) noexcept;
[[nodiscard]] std::string_view localized_menu_action_name(
    MenuAction action, std::string_view locale = kDefaultLocale) noexcept;
[[nodiscard]] std::string_view github_releases_url() noexcept;
[[nodiscard]] std::vector<MenuItem> default_menu_items(
    std::string_view locale = kDefaultLocale);

class PluginMenu {
 public:
  explicit PluginMenu(std::vector<MenuItem> items = default_menu_items(),
                      std::string_view locale = kDefaultLocale);

  // Opening always starts from the first navigation item. This makes a newly
  // opened Properties surface deterministic even after a previous selection.
  void open() noexcept;
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] const std::vector<MenuItem>& navigation_items() const noexcept;
  [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
  [[nodiscard]] const MenuItem* selected_item() const noexcept;
  [[nodiscard]] std::optional<ContextualPane> contextual_pane() const;

  // Returns false when the menu is closed or the requested item is outside
  // the navigation list.
  bool select(std::size_t index) noexcept;

  // Emits a host command only when the selected contextual pane exposes the
  // requested action. No network request or external browser is started here.
  [[nodiscard]] std::optional<MenuCommand> activate(
      MenuAction action, std::string_view installed_version) const;

 private:
  [[nodiscard]] bool action_available(MenuAction action) const noexcept;

  std::vector<MenuItem> items_;
  std::string locale_;
  bool open_ = false;
  std::optional<std::size_t> selected_index_;
};

}  // namespace obs_whisperbleep::ui
