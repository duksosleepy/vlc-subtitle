cmake_minimum_required(VERSION 3.16)

project(vlc_subtitle LANGUAGES C CXX)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VLC_PLUGIN REQUIRED IMPORTED_TARGET vlc-plugin)

set(WHISPER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/whisper.cpp"
    CACHE PATH "Path to the whisper.cpp source tree")
option(VLC_SUBTITLE_VULKAN "Build whisper.cpp with its Vulkan backend" OFF)
set(VLC_SUBTITLE_MODULE_NAME "suboffline" CACHE STRING
    "Internal VLC module name (override only for isolated testing)")

if(NOT EXISTS "${WHISPER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "whisper.cpp was not found at ${WHISPER_SOURCE_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN ${VLC_SUBTITLE_VULKAN} CACHE BOOL "" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory("${WHISPER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/whisper"
                 EXCLUDE_FROM_ALL)

add_library(suboffline_plugin MODULE
    src/subtitle.c
    runtime/runtime.cpp
    runtime/whisper_runtime.cpp
)

target_compile_features(suboffline_plugin PRIVATE c_std_11 cxx_std_17)
target_compile_definitions(suboffline_plugin PRIVATE
    PIC
    MODULE_STRING="${VLC_SUBTITLE_MODULE_NAME}"
)
target_compile_options(suboffline_plugin PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
)
target_include_directories(suboffline_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
)
target_link_libraries(suboffline_plugin PRIVATE
    PkgConfig::VLC_PLUGIN
    whisper
)

set_target_properties(suboffline_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "suboffline_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
)

pkg_get_variable(VLC_PLUGINS_DIR vlc-plugin pluginsdir)
if(VLC_PLUGINS_DIR)
    install(TARGETS suboffline_plugin
            LIBRARY DESTINATION "${VLC_PLUGINS_DIR}/control"
            RUNTIME DESTINATION "${VLC_PLUGINS_DIR}/control")
endif()
