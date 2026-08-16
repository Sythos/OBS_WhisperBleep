// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (https://www.sythos.net/)

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "obs_whisperbleep/ui/plugin_menu.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "M4 UI test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using namespace obs_whisperbleep::ui;

  PluginMenu menu;
  expect(!menu.is_open(), "menu starts closed");
  expect(menu.navigation_items().size() == 5,
         "default menu exposes the required sections");
  expect(menu.navigation_items().front().section == MenuSection::general,
         "general is the first navigation section");

  menu.open();
  expect(menu.is_open(), "opening marks the menu open");
  expect(menu.selected_index().has_value() && *menu.selected_index() == 0,
         "opening selects the first left-hand item");
  expect(menu.selected_item() != nullptr &&
             menu.selected_item()->section == MenuSection::general,
         "the first item is selected in the left navigation");
  const auto first_pane = menu.contextual_pane();
  expect(first_pane.has_value() && first_pane->section == MenuSection::general,
         "the right pane follows the first selected item");

  expect(menu.select(2), "models can be selected from the left navigation");
  const auto models_pane = menu.contextual_pane();
  expect(models_pane.has_value() && models_pane->section == MenuSection::models,
         "the right pane follows the selected models item");
  expect(models_pane->description.find("VRAM") != std::string::npos,
         "the models pane warns that GPU selection consumes VRAM");
  expect(models_pane->description.find("Whisper model") != std::string::npos,
         "the models pane accounts for the Whisper model in VRAM");
  expect(models_pane->description.find("OBS game or application") !=
             std::string::npos,
         "the models pane accounts for the OBS game or application in VRAM");
  expect(models_pane->description.find("prefer CPU") != std::string::npos,
         "the models pane recommends CPU when it can preserve VRAM");

  expect(menu.select(4), "about can be selected from the left navigation");
  const auto about_pane = menu.contextual_pane();
  expect(about_pane.has_value() && about_pane->section == MenuSection::about,
         "the right pane follows the selected about item");
  expect(about_pane->actions.size() == 1 &&
             about_pane->actions.front() == MenuAction::check_updates,
         "about exposes the Check Updates action");

  const auto command = menu.activate(MenuAction::check_updates, "0.1.0");
  expect(command.has_value(), "Check Updates emits a host command");
  expect(command->action == MenuAction::check_updates,
         "the emitted command identifies Check Updates");
  expect(command->installed_version == "0.1.0",
         "the emitted command carries the installed version");
  expect(command->target_url == github_releases_url(),
         "the emitted command carries the GitHub Releases URL");

  expect(menu.select(0), "general can be selected from the left navigation");
  expect(!menu.activate(MenuAction::check_updates, "0.1.0").has_value(),
         "an action not exposed by the selected pane is not emitted");
  expect(!menu.select(99), "an invalid navigation index is rejected");

  menu.close();
  expect(!menu.contextual_pane().has_value(),
         "closed menus do not expose a contextual pane");
  menu.open();
  expect(menu.selected_index().has_value() && *menu.selected_index() == 0,
         "reopening resets selection to the first item");

  PluginMenu empty_menu(std::vector<MenuItem>{});
  empty_menu.open();
  expect(!empty_menu.selected_index().has_value(),
         "an empty custom menu has no selected item");
  expect(!empty_menu.contextual_pane().has_value(),
         "an empty custom menu has no contextual pane");

  return EXIT_SUCCESS;
}
