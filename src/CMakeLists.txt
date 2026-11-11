cmake_minimum_required(VERSION 3.20)
project(PlasticUtopia LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4 /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---- libclip: the .clip reader/writer (plain C, bundles SQLite) -------------
# libclip is a standalone library (a git submodule at ../libclip); Plastic
# Paint is one of its users.  Build its tests too, against the CSP-saved
# reference document when it is present.
enable_testing()
set(LIBCLIP_BUILD_TESTS ON)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../yuzuko.clip")
  set(LIBCLIP_REFERENCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../yuzuko.clip" CACHE FILEPATH "CSP-written .clip used by the libclip tests")
endif()
add_subdirectory(../libclip ${CMAKE_BINARY_DIR}/libclip)

# ---- core painting engine (no OS dependencies; zlib/PNG from libclip) ------
add_library(pu_core STATIC
  core/zlib_lite.cpp core/blend.cpp core/document.cpp core/brush.cpp core/undo.cpp core/png.cpp)
target_include_directories(pu_core PUBLIC core)
target_link_libraries(pu_core PUBLIC libclip::clip)  # zlib + PNG

# ---- bridge between libclip and the engine's Document ------------------------
add_library(pu_clip STATIC clip/clip_bridge.cpp)
target_include_directories(pu_clip PUBLIC clip)
target_link_libraries(pu_clip PUBLIC pu_core libclip::clip)

# ---- command line tool ------------------------------------------------------
add_executable(cliptool tools/cliptool.cpp)
target_link_libraries(cliptool PRIVATE pu_clip)
if(WIN32)
  target_link_libraries(cliptool PRIVATE shell32)  # CommandLineToArgvW (UTF-8 arguments)
endif()

if(WIN32)
  # Probe for Clip Studio's own preview handler (see tools/previewprobe.cpp)
  add_executable(previewprobe tools/previewprobe.cpp)
  target_link_libraries(previewprobe PRIVATE ole32 shlwapi user32)
endif()

# ---- tests ------------------------------------------------------------------
add_executable(pu_tests tests/test_main.cpp tests/test_core.cpp tests/test_clip.cpp tests/test_app.cpp app/app_state.cpp)
target_link_libraries(pu_tests PRIVATE pu_clip)
target_include_directories(pu_tests PRIVATE app)
add_test(NAME unit COMMAND pu_tests)

# ---- Windows paint application ----------------------------------------------
if(WIN32)
  add_executable(plastic_paint WIN32 app/main_win32.cpp app/app_state.cpp)
  target_link_libraries(plastic_paint PRIVATE pu_clip user32 gdi32 comdlg32 shell32)
endif()
