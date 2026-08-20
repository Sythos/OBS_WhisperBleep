# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OBS_WHISPERBLEEP_VERSION)
string(STRIP "${OBS_WHISPERBLEEP_VERSION}" OBS_WHISPERBLEEP_VERSION)
if(NOT OBS_WHISPERBLEEP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain MAJOR.MINOR.PATCH")
endif()

project(OBSWhisperBleep VERSION "${OBS_WHISPERBLEEP_VERSION}" LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(OBS_WHISPERBLEEP_BUILD_TESTS "Build deterministic core and OBS-boundary tests" ON)
option(OBS_WHISPERBLEEP_BUILD_PLUGIN_STUB "Build the non-OBS plugin stub" ON)
option(OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE
  "Build the native OBS filter module when an OBS SDK is supplied"
  OFF
)
option(OBS_WHISPERBLEEP_ENABLE_CUDA "Reserve the CUDA backend option for a validated implementation" OFF)
set(OBS_SDK_DIR "" CACHE PATH "OBS SDK directory containing libobs headers and libraries")
set(OBS_LIB "" CACHE FILEPATH "Optional explicit libobs library for the native module")
set(WHISPER_RUNTIME "stub" CACHE STRING "Whisper runtime selection")
set_property(CACHE WHISPER_RUNTIME PROPERTY STRINGS stub openai-whisper native)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(OBS_WHISPERBLEEP_CORE_SOURCES
  src/core/audio_pipeline.cpp
  src/core/audio_frame_queue.cpp
  src/core/audio_processing_worker.cpp
  src/core/censor_scheduler.cpp
  src/core/audio_timeline.cpp
  src/core/match_planner.cpp
  src/core/phrase_matcher.cpp
  src/core/replacement_renderer.cpp
  src/core/synthetic_replacement.cpp
  src/core/timestamp_coordinator.cpp
  src/diagnostics/debug_log.cpp
  src/diagnostics/latency.cpp
  src/model/model_catalog.cpp
  src/model/model_downloader.cpp
  src/model/model_manager.cpp
  src/replacement/replacement_catalog.cpp
  src/replacement/wav_audio_loader.cpp
  src/runtime/backend_selector.cpp
  src/runtime/whisper_runtime.cpp
  src/platform/platform_info.cpp
  src/ui/localization.cpp
  src/ui/plugin_menu.cpp
  src/updates/update_checker.cpp
)

add_library(obs_whisperbleep_core STATIC ${OBS_WHISPERBLEEP_CORE_SOURCES})
target_include_directories(obs_whisperbleep_core PUBLIC include)
target_link_libraries(obs_whisperbleep_core PUBLIC Threads::Threads)
target_compile_features(obs_whisperbleep_core PUBLIC cxx_std_20)
target_compile_definitions(obs_whisperbleep_core PRIVATE
  OBS_WHISPERBLEEP_VERSION="${PROJECT_VERSION}"
  OBS_WHISPERBLEEP_WHISPER_RUNTIME="${WHISPER_RUNTIME}"
)
if(OBS_WHISPERBLEEP_ENABLE_CUDA)
  target_compile_definitions(obs_whisperbleep_core PRIVATE OBS_WHISPERBLEEP_CUDA_REQUESTED=1)
endif()

if(MSVC)
  target_compile_options(obs_whisperbleep_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(obs_whisperbleep_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OBS_WHISPERBLEEP_BUILD_PLUGIN_STUB)
  add_library(obs_whisperbleep_plugin_stub MODULE
    src/obs/plugin_main.cpp
    src/obs/obs_filter.cpp
    src/obs/obs_properties.cpp
  )
  target_include_directories(obs_whisperbleep_plugin_stub PRIVATE include)
  target_link_libraries(obs_whisperbleep_plugin_stub PRIVATE obs_whisperbleep_core)
  target_compile_features(obs_whisperbleep_plugin_stub PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(obs_whisperbleep_plugin_stub PRIVATE /W4 /permissive-)
  else()
    target_compile_options(obs_whisperbleep_plugin_stub PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

if(OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE)
  if(NOT OBS_SDK_DIR)
    message(FATAL_ERROR
      "OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE requires OBS_SDK_DIR."
    )
  endif()

  find_path(OBS_INCLUDE_DIR
    NAMES obs.h
    PATHS
      "${OBS_SDK_DIR}/libobs"
      "${OBS_SDK_DIR}/include"
      "${OBS_SDK_DIR}/include/obs"
    NO_DEFAULT_PATH
  )
  find_path(OBS_MODULE_INCLUDE_DIR
    NAMES obs-module.h
    PATHS
      "${OBS_SDK_DIR}/libobs"
      "${OBS_SDK_DIR}/include"
      "${OBS_SDK_DIR}/include/obs"
    NO_DEFAULT_PATH
  )

  if(NOT OBS_INCLUDE_DIR OR NOT OBS_MODULE_INCLUDE_DIR)
    message(FATAL_ERROR
      "OBS_SDK_DIR must contain obs.h and obs-module.h."
    )
  endif()

  if(NOT OBS_LIB)
    find_library(OBS_LIB
      NAMES obs libobs
      PATHS
        "${OBS_SDK_DIR}/build/libobs"
        "${OBS_SDK_DIR}/build/lib"
        "${OBS_SDK_DIR}/lib"
        "${OBS_SDK_DIR}/bin"
      NO_DEFAULT_PATH
    )
  endif()

  if(NOT OBS_LIB)
    message(FATAL_ERROR
      "Set OBS_LIB to the libobs library required by the native module."
    )
  endif()

  add_library(obs_whisperbleep_plugin MODULE
    src/obs/obs_module.cpp
    src/obs/obs_filter.cpp
    src/obs/obs_properties.cpp
  )
  target_include_directories(obs_whisperbleep_plugin PRIVATE
    include
    "${OBS_INCLUDE_DIR}"
    "${OBS_MODULE_INCLUDE_DIR}"
  )
  target_link_libraries(obs_whisperbleep_plugin PRIVATE
    obs_whisperbleep_core
    "${OBS_LIB}"
  )
  target_compile_definitions(obs_whisperbleep_plugin PRIVATE
    OBS_WHISPERBLEEP_WITH_OBS=1
  )
  target_compile_features(obs_whisperbleep_plugin PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(obs_whisperbleep_plugin PRIVATE /W4 /permissive-)
  else()
    target_compile_options(obs_whisperbleep_plugin PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set_target_properties(obs_whisperbleep_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-whisperbleep"
  )
endif()

if(OBS_WHISPERBLEEP_BUILD_TESTS)
  enable_testing()

  add_executable(obs_whisperbleep_core_tests tests/core/test_core.cpp)
  target_link_libraries(obs_whisperbleep_core_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_core_tests PRIVATE include)
  add_test(NAME core COMMAND obs_whisperbleep_core_tests)

  add_executable(obs_whisperbleep_model_tests tests/model/test_model.cpp)
  target_link_libraries(obs_whisperbleep_model_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_model_tests PRIVATE include)
  add_test(NAME model COMMAND obs_whisperbleep_model_tests)

  add_executable(obs_whisperbleep_integration_tests tests/integration/test_integration.cpp)
  target_link_libraries(obs_whisperbleep_integration_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_integration_tests PRIVATE include)
  add_test(NAME integration COMMAND obs_whisperbleep_integration_tests)

  add_executable(obs_whisperbleep_obs_tests
    tests/obs/test_obs.cpp
    src/obs/obs_filter.cpp
    src/obs/obs_properties.cpp
  )
  target_link_libraries(obs_whisperbleep_obs_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_obs_tests PRIVATE include)
  add_test(NAME obs COMMAND obs_whisperbleep_obs_tests)

  add_executable(obs_whisperbleep_m2_tests tests/m2/test_m2.cpp)
  target_link_libraries(obs_whisperbleep_m2_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m2_tests PRIVATE include)
  add_test(NAME m2 COMMAND obs_whisperbleep_m2_tests)

  add_executable(obs_whisperbleep_m3_catalog_tests
    tests/m3/test_m3_catalog.cpp
  )
  target_link_libraries(obs_whisperbleep_m3_catalog_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m3_catalog_tests PRIVATE include)
  add_test(NAME m3_catalog COMMAND obs_whisperbleep_m3_catalog_tests)

  add_executable(obs_whisperbleep_m3_downloader_tests
    tests/m3/test_m3_downloader.cpp
  )
  target_link_libraries(obs_whisperbleep_m3_downloader_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m3_downloader_tests PRIVATE include)
  add_test(NAME m3_downloader COMMAND obs_whisperbleep_m3_downloader_tests)

  add_executable(obs_whisperbleep_m3_tests tests/m3/test_m3.cpp)
  target_link_libraries(obs_whisperbleep_m3_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m3_tests PRIVATE include)
  add_test(NAME m3 COMMAND obs_whisperbleep_m3_tests)

  add_executable(obs_whisperbleep_m4_replacement_tests
    tests/m4/test_m4_replacements.cpp
  )
  target_link_libraries(obs_whisperbleep_m4_replacement_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m4_replacement_tests PRIVATE include)
  add_test(NAME m4_replacements COMMAND obs_whisperbleep_m4_replacement_tests)

  add_executable(obs_whisperbleep_m4_matching_tests
    tests/m4/test_m4_matching.cpp
  )
  target_link_libraries(obs_whisperbleep_m4_matching_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m4_matching_tests PRIVATE include)
  add_test(NAME m4_matching COMMAND obs_whisperbleep_m4_matching_tests)

  add_executable(obs_whisperbleep_m4_ui_tests tests/m4/test_m4_ui.cpp)
  target_link_libraries(obs_whisperbleep_m4_ui_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m4_ui_tests PRIVATE include)
  add_test(NAME m4_ui COMMAND obs_whisperbleep_m4_ui_tests)

  add_executable(obs_whisperbleep_m4_update_tests
    tests/m4/test_m4_updates.cpp
  )
  target_link_libraries(obs_whisperbleep_m4_update_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m4_update_tests PRIVATE include)
  add_test(NAME m4_updates COMMAND obs_whisperbleep_m4_update_tests)

  add_executable(obs_whisperbleep_m5_runtime_tests
    tests/m5/test_m5_runtime.cpp
  )
  target_link_libraries(obs_whisperbleep_m5_runtime_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m5_runtime_tests PRIVATE include)
  add_test(NAME m5_runtime COMMAND obs_whisperbleep_m5_runtime_tests)

  add_executable(obs_whisperbleep_m5_audio_loader_tests
    tests/m5/test_m5_audio_loader.cpp
  )
  target_link_libraries(obs_whisperbleep_m5_audio_loader_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m5_audio_loader_tests PRIVATE include)
  add_test(NAME m5_audio_loader COMMAND obs_whisperbleep_m5_audio_loader_tests)

  add_executable(obs_whisperbleep_m5_timeline_tests
    tests/m5/test_m5_timeline.cpp
  )
  target_link_libraries(obs_whisperbleep_m5_timeline_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m5_timeline_tests PRIVATE include)
  add_test(NAME m5_timeline COMMAND obs_whisperbleep_m5_timeline_tests)

  add_executable(obs_whisperbleep_m6_i18n_tests
    tests/m6/test_m6_i18n.cpp
    src/obs/obs_properties.cpp
  )
  target_link_libraries(obs_whisperbleep_m6_i18n_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m6_i18n_tests PRIVATE include)
  add_test(NAME m6_i18n COMMAND obs_whisperbleep_m6_i18n_tests)

  add_executable(obs_whisperbleep_m6_debug_log_tests
    tests/m6/test_m6_debug_log.cpp
  )
  target_link_libraries(obs_whisperbleep_m6_debug_log_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m6_debug_log_tests PRIVATE include)
  add_test(NAME m6_debug_log COMMAND obs_whisperbleep_m6_debug_log_tests)

  add_executable(obs_whisperbleep_m6_latency_tests
    tests/m6/test_m6_latency.cpp
  )
  target_link_libraries(obs_whisperbleep_m6_latency_tests PRIVATE obs_whisperbleep_core)
  target_include_directories(obs_whisperbleep_m6_latency_tests PRIVATE include)
  add_test(NAME m6_latency COMMAND obs_whisperbleep_m6_latency_tests)
endif()

install(TARGETS obs_whisperbleep_core
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
if(OBS_WHISPERBLEEP_BUILD_PLUGIN_STUB)
  install(TARGETS obs_whisperbleep_plugin_stub
    LIBRARY DESTINATION obs-plugins
    RUNTIME DESTINATION obs-plugins
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )
endif()
if(OBS_WHISPERBLEEP_BUILD_NATIVE_MODULE)
  install(TARGETS obs_whisperbleep_plugin
    LIBRARY DESTINATION obs-plugins
    RUNTIME DESTINATION obs-plugins
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )
endif()
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES README.md LICENSE NOTICE VERSION DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)
install(DIRECTORY assets/ DESTINATION assets)
install(DIRECTORY runtime/ DESTINATION runtime
  FILES_MATCHING
    PATTERN "*.py"
)

set(CPACK_PACKAGE_NAME "OBS-WhisperBleep")
set(CPACK_PACKAGE_VENDOR "Sythos")
set(CPACK_PACKAGE_CONTACT "https://www.sythos.net")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)
