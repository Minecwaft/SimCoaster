cmake_minimum_required(VERSION 3.25)
project(SimCoaster LANGUAGES CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The reconstructed runtime targets native x64.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
  message(FATAL_ERROR "Select the private GCC 17 toolchain with C++29 reflection support.")
endif()

option(SIMCOASTER_GAME_RELEASE "Build the optimized standalone game" ON)
if(SIMCOASTER_GAME_RELEASE)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "SIMCOASTER_GAME_RELEASE requires a Release build.")
  endif()
endif()

add_library(simcoaster_options INTERFACE)
target_compile_options(simcoaster_options INTERFACE
  "$<$<COMPILE_LANGUAGE:CXX>:-std=c++29;-freflection;-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>")
# Private revisions share a version number. Replacing either frontend must
# invalidate serialized declarations and objects, including no-op builds.
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-prog-name=cc1plus
  OUTPUT_VARIABLE compiler_frontend OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
set(compiler_identity "")
foreach(compiler_part IN ITEMS "${CMAKE_CXX_COMPILER}" "${compiler_frontend}")
  file(TIMESTAMP "${compiler_part}" compiler_time "%s.%f" UTC)
  file(SIZE "${compiler_part}" compiler_bytes)
  string(APPEND compiler_identity "${compiler_part}:${compiler_time}:${compiler_bytes}\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${compiler_part}")
endforeach()
string(SHA256 compiler_stamp "${compiler_identity}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compiler_stamp.hpp"
  CONTENT "// Private GCC inputs: @compiler_stamp@\n" @ONLY)
if(SIMCOASTER_GAME_RELEASE)
  # Explicit LTO options retain the requested twelve-worker link rather than
  # CMake's automatic IPO job selection. RTTI remains enabled for dynamic_cast.
  target_compile_options(simcoaster_options INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:-O3;-flto=12;-fno-semantic-interposition;-ffunction-sections;-fdata-sections>")
  target_link_options(simcoaster_options INTERFACE
    -O3 -flto=12 -fno-semantic-interposition
    "LINKER:--gc-sections,--print-gc-sections"
    "LINKER:--major-image-version,0,--minor-image-version,0")
endif()

set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)

include(cmake/sources.cmake)
# Compile operating-system providers once. The production archive owns them;
# focused tests can link those providers without pulling in a window host.
set(SIMCOASTER_PLATFORM_SOURCES
  src/clock_win32.cpp src/sound_output_win32.cpp src/sign_font_raster_win32.cpp
  src/file_platform_win32.cpp src/campaign_profile_write_win32.cpp
  src/application_session_files.cpp src/application_postcard_jpeg.cpp
  src/application_startup_image_win32.cpp)
list(REMOVE_ITEM SIMCOASTER_WIN32_SOURCES ${SIMCOASTER_PLATFORM_SOURCES})
add_library(simcoaster_platform_objects OBJECT ${SIMCOASTER_PLATFORM_SOURCES})
target_include_directories(simcoaster_platform_objects PRIVATE include)
target_link_libraries(simcoaster_platform_objects PRIVATE simcoaster_options)
target_compile_definitions(simcoaster_platform_objects PRIVATE UNICODE _UNICODE)
foreach(layer IN ITEMS core content simulation presentation application win32)
  string(TOUPPER "${layer}" layer_key)
  add_library(simcoaster_${layer} STATIC ${SIMCOASTER_${layer_key}_SOURCES})
  target_include_directories(simcoaster_${layer} PUBLIC include)
  target_link_libraries(simcoaster_${layer} PUBLIC simcoaster_options)
endforeach()
target_sources(simcoaster_win32 PRIVATE $<TARGET_OBJECTS:simcoaster_platform_objects>)
target_link_libraries(simcoaster_content PUBLIC simcoaster_core PRIVATE ZLIB::ZLIB)
target_link_libraries(simcoaster_simulation PUBLIC simcoaster_content)
target_link_libraries(simcoaster_presentation PUBLIC simcoaster_simulation)
target_link_libraries(simcoaster_application PUBLIC simcoaster_presentation)
target_link_libraries(simcoaster_win32 PUBLIC simcoaster_application PRIVATE
  d3d11 dxgi d3dcompiler windowscodecs ole32 uuid user32 gdi32 usp10)
target_compile_definitions(simcoaster_win32 PRIVATE UNICODE _UNICODE)
set_source_files_properties(src/sound_decode.cpp PROPERTIES
  COMPILE_OPTIONS "-isystem;${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp3")

# Native providers implement the lower interfaces at final static linkage.
# GNU archive rescanning resolves provider/consumer order without target cycles.
set(CMAKE_CXX_LINK_GROUP_USING_RESCAN "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE)
set(SIMCOASTER_NATIVE_LINK
  "$<LINK_GROUP:RESCAN,simcoaster_win32,simcoaster_application,simcoaster_presentation,simcoaster_simulation,simcoaster_content,simcoaster_core>")

# Preserve the original per-source Windows API selection after the move.
set_source_files_properties(
  src/ui_preferences.cpp
  src/sound_output_win32.cpp
  src/sign_font_raster_win32.cpp
  src/world_calendar.cpp
  src/campaign_profile.cpp
  src/application_startup.cpp
  src/application_profiles.cpp
  src/application_session_files.cpp
  src/application_session_coaster.cpp
  src/application_postcard_jpeg.cpp
  PROPERTIES COMPILE_OPTIONS "-UUNICODE;-U_UNICODE")

add_executable(simcoaster_runtime src/main.cpp assets/simcoaster.rc)
target_include_directories(simcoaster_runtime PRIVATE assets)
set_source_files_properties(assets/simcoaster.rc PROPERTIES
  OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/simcoaster.ico")
set_target_properties(simcoaster_runtime PROPERTIES
  OUTPUT_NAME Game WIN32_EXECUTABLE TRUE
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Game")
target_compile_definitions(simcoaster_runtime PRIVATE SIMCOASTER_GUI_EXECUTABLE=1)
target_link_libraries(simcoaster_runtime PRIVATE "${SIMCOASTER_NATIVE_LINK}")
target_link_options(simcoaster_runtime PRIVATE -static)

# Shared declarations follow target ownership; Windows API headers stay textual.
# Release and Debug generate separate caches with their own compiler flags.
option(SIMCOASTER_PRECOMPILED_HEADERS "Share standard and world declaration parsing" ON)
if(SIMCOASTER_PRECOMPILED_HEADERS)
  foreach(declarations IN ITEMS standard world)
    foreach(api IN ITEMS ansi unicode)
      set(cache "simcoaster_${declarations}_pch")
      if(api STREQUAL "unicode")
        string(APPEND cache "_unicode")
      endif()
      add_library(${cache} OBJECT cmake/standard_pch.cpp)
      target_include_directories(${cache} PRIVATE include)
      target_link_libraries(${cache} PRIVATE simcoaster_options)
      if(api STREQUAL "unicode")
        target_compile_definitions(${cache} PRIVATE UNICODE _UNICODE)
      endif()
      target_precompile_headers(${cache} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compiler_stamp.hpp" "cmake/${declarations}_pch.hpp")
    endforeach()
  endforeach()
  get_property(compiled_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(compiled_target IN LISTS compiled_targets)
    get_target_property(kind ${compiled_target} TYPE)
    if((kind STREQUAL "EXECUTABLE" OR kind STREQUAL "STATIC_LIBRARY" OR kind STREQUAL "OBJECT_LIBRARY")
        AND NOT compiled_target MATCHES "^simcoaster_(standard|world)_pch(_unicode)?$")
      if(compiled_target MATCHES "^simcoaster_(simulation|presentation|application|win32|runtime)$")
        set(cache simcoaster_world_pch)
      else()
        set(cache simcoaster_standard_pch)
      endif()
      get_target_property(definitions ${compiled_target} COMPILE_DEFINITIONS)
      if("UNICODE" IN_LIST definitions)
        string(APPEND cache "_unicode")
        # Keep the existing ANSI overrides in the few mixed platform sources.
        get_target_property(sources ${compiled_target} SOURCES)
        foreach(source IN LISTS sources)
          get_source_file_property(options "${source}" COMPILE_OPTIONS)
          if("-UUNICODE" IN_LIST options)
            set_source_files_properties("${source}" PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
          endif()
        endforeach()
      endif()
      target_precompile_headers(${compiled_target} REUSE_FROM ${cache})
    endif()
  endforeach()
endif()

# Keep compiler changes visible even with PCH disabled. An extra forced include
# before a PCH prevents GCC from importing it, so this is a build dependency.
get_property(compiled_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(compiled_target IN LISTS compiled_targets)
  get_target_property(target_sources ${compiled_target} SOURCES)
  foreach(target_source IN LISTS target_sources)
    if(target_source MATCHES "\\.(cpp|cxx|cc)$")
      set_property(SOURCE "${target_source}" APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/compiler_stamp.hpp")
    endif()
  endforeach()
endforeach()
