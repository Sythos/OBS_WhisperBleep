// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#include "obs_whisperbleep/ui/plugin_menu.hpp"

#include <algorithm>
#include <utility>

namespace obs_whisperbleep::ui {
namespace {

constexpr std::string_view kGitHubReleasesUrl =
    "https://github.com/Sythos/OBS_WhisperBleep/releases";

ContextualPane pane_for(const MenuItem& item, const std::string_view locale) {
  ContextualPane pane;
  pane.section = item.section;
  pane.title = item.label;

  switch (item.section) {
    case MenuSection::general:
      pane.description =
          std::string(translate(locale, keys::menu_general_description));
      break;
    case MenuSection::audio:
      pane.description =
          std::string(translate(locale, keys::menu_audio_description));
      break;
    case MenuSection::models:
      pane.description =
          std::string(translate(locale, keys::menu_models_description));
      break;
    case MenuSection::matching:
      pane.description =
          std::string(translate(locale, keys::menu_matching_description));
      break;
    case MenuSection::about:
      pane.description =
          std::string(translate(locale, keys::menu_about_description));
      pane.actions.push_back(MenuAction::check_updates);
      break;
  }

  return pane;
}

}  // namespace

const char* menu_section_name(const MenuSection section) noexcept {
  switch (section) {
    case MenuSection::general:
      return "general";
    case MenuSection::audio:
      return "audio";
    case MenuSection::models:
      return "models";
    case MenuSection::matching:
      return "matching";
    case MenuSection::about:
      return "about";
  }
  return "unknown";
}

const char* menu_action_name(const MenuAction action) noexcept {
  switch (action) {
    case MenuAction::check_updates:
      return "Check Updates";
  }
  return "Unknown Action";
}

std::string_view localized_menu_action_name(const MenuAction action,
                                            const std::string_view locale) noexcept {
  switch (action) {
    case MenuAction::check_updates:
      return translate(locale, keys::action_check_updates);
  }
  return translate(locale, keys::action_unknown);
}

std::string_view github_releases_url() noexcept { return kGitHubReleasesUrl; }

std::vector<MenuItem> default_menu_items(const std::string_view locale) {
  return {
      {MenuSection::general, "general",
       std::string(translate(locale, keys::menu_general))},
      {MenuSection::audio, "audio", std::string(translate(locale, keys::menu_audio))},
      {MenuSection::models, "models", std::string(translate(locale, keys::menu_models))},
      {MenuSection::matching, "matching",
       std::string(translate(locale, keys::menu_matching))},
      {MenuSection::about, "about", std::string(translate(locale, keys::menu_about))},
  };
}

PluginMenu::PluginMenu(std::vector<MenuItem> items, const std::string_view locale)
    : items_(std::move(items)), locale_(resolve_locale(locale)) {}

void PluginMenu::open() noexcept {
  open_ = true;
  selected_index_ = items_.empty() ? std::nullopt : std::optional<std::size_t>(0);
}

void PluginMenu::close() noexcept {
  open_ = false;
  selected_index_.reset();
}

bool PluginMenu::is_open() const noexcept { return open_; }

const std::vector<MenuItem>& PluginMenu::navigation_items() const noexcept {
  return items_;
}

std::optional<std::size_t> PluginMenu::selected_index() const noexcept {
  return selected_index_;
}

const MenuItem* PluginMenu::selected_item() const noexcept {
  if (!open_ || !selected_index_.has_value() ||
      *selected_index_ >= items_.size()) {
    return nullptr;
  }
  return &items_[*selected_index_];
}

std::optional<ContextualPane> PluginMenu::contextual_pane() const {
  const auto* item = selected_item();
  if (item == nullptr) {
    return std::nullopt;
  }
  return pane_for(*item, locale_);
}

bool PluginMenu::select(const std::size_t index) noexcept {
  if (!open_ || index >= items_.size()) {
    return false;
  }
  selected_index_ = index;
  return true;
}

bool PluginMenu::action_available(const MenuAction action) const noexcept {
  const auto pane = contextual_pane();
  if (!pane.has_value()) {
    return false;
  }
  return std::find(pane->actions.begin(), pane->actions.end(), action) !=
         pane->actions.end();
}

std::optional<MenuCommand> PluginMenu::activate(
    const MenuAction action, const std::string_view installed_version) const {
  if (!action_available(action)) {
    return std::nullopt;
  }

  return MenuCommand{action, std::string(installed_version),
                     std::string(github_releases_url())};
}

}  // namespace obs_whisperbleep::ui
