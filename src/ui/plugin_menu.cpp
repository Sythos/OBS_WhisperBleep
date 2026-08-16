// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#include "obs_whisperbleep/ui/plugin_menu.hpp"

#include <algorithm>
#include <utility>

namespace obs_whisperbleep::ui {
namespace {

constexpr std::string_view kGitHubReleasesUrl =
    "https://github.com/Sythos/OBS_WhisperBleep/releases";

ContextualPane pane_for(const MenuItem& item) {
  ContextualPane pane;
  pane.section = item.section;
  pane.title = item.label;

  switch (item.section) {
    case MenuSection::general:
      pane.description = "General plugin settings and runtime status.";
      break;
    case MenuSection::audio:
      pane.description = "Replacement audio and synchronization settings.";
      break;
    case MenuSection::models:
      pane.description =
          "Whisper model selection, download and cache status. When GPU is "
          "selected, VRAM must hold both the Whisper model and the OBS game "
          "or application. Avoid models that are too large; when a recent "
          "mid-range CPU is available, prefer CPU to leave VRAM for the game "
          "or application.";
      break;
    case MenuSection::matching:
      pane.description = "Word and phrase matching settings.";
      break;
    case MenuSection::about:
      pane.description = "Plugin information and release update actions.";
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

std::string_view github_releases_url() noexcept { return kGitHubReleasesUrl; }

std::vector<MenuItem> default_menu_items() {
  return {
      {MenuSection::general, "general", "General"},
      {MenuSection::audio, "audio", "Audio"},
      {MenuSection::models, "models", "Models"},
      {MenuSection::matching, "matching", "Matching"},
      {MenuSection::about, "about", "About"},
  };
}

PluginMenu::PluginMenu(std::vector<MenuItem> items)
    : items_(std::move(items)) {}

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
  return pane_for(*item);
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
