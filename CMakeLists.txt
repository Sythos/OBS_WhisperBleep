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

option(OBS_WHISPERBLEEP_BUILD_TESTS "Build deterministic M0 tests" ON)
option(OBS_WHISPERBLEEP_BUILD_PLUGIN_STUB "Build the non-OBS plugin stub" ON)
option(OBS_WHISPERBLEEP_ENABLE_CUDA "Reserve the CUDA backend option for a validated implementation" OFF)
set(OBS_SDK_DIR "" CACHE PATH "OBS SDK directory (reserved for M1 integration)")
set(WHISPER_RUNTIME "stub" CACHE STRING "Whisper runtime selection")
set_property(CACHE WHISPER_RUNTIME PROPERTY STRINGS stub openai-whisper native)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(OBS_WHISPERBLEEP_CORE_SOURCES
  src/core/audio_pipeline.cpp
  src/core/censor_scheduler.cpp
  src/core/phrase_matcher.cpp
  src/core/replacement_renderer.cpp
  src/model/model_catalog.cpp
  src/model/model_downloader.cpp
  src/model/model_manager.cpp
  src/runtime/backend_selector.cpp
  src/runtime/whisper_runtime.cpp
  src/platform/platform_info.cpp
)

add_library(obs_whisperbleep_core STATIC ${OBS_WHISPERBLEEP_CORE_SOURCES})
target_include_directories(obs_whisperbleep_core PUBLIC include)
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
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES README.md LICENSE NOTICE VERSION DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)

set(CPACK_PACKAGE_NAME "OBS-WhisperBleep")
set(CPACK_PACKAGE_VENDOR "Sythos")
set(CPACK_PACKAGE_CONTACT "https://www.sythos.net")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)
