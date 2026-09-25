cmake_minimum_required(VERSION 3.22...3.31)

project(vlc_subtitle VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(ExternalProject)

# Compiler definitions and flags
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

if(MSVC)
    add_compile_options(/utf-8 /MP /W3 /DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Automatically use sccache if available
option(VLC_SUBTITLE_ENABLE_SCCACHE "Automatically use sccache if available" ON)
if(VLC_SUBTITLE_ENABLE_SCCACHE)
    find_program(SCCACHE_PROGRAM sccache)
    if(SCCACHE_PROGRAM)
        message(STATUS "sccache found: ${SCCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    endif()
endif()

# Discover VLC plugin SDK (headers & libvlccore or dynamic lookup on macOS)
find_package(VLC REQUIRED)

# Discover package dependencies (managed via vcpkg, Homebrew, or system)
find_package(Freetype REQUIRED)

find_package(libsrt CONFIG QUIET)
if(NOT libsrt_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SRT QUIET IMPORTED_TARGET srt)
    endif()
endif()

set(WHISPER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/whisper.cpp"
    CACHE PATH "Path to the whisper.cpp source tree")
set(VOXTRAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/voxtral.c"
    CACHE PATH "Path to the voxtral.c source tree")
set(PARAKEET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/parakeet.cpp"
    CACHE PATH "Path to the parakeet.cpp source tree")
set(MOONSHINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/moonshine"
    CACHE PATH "Path to the Moonshine source tree")

option(VLC_SUBTITLE_VULKAN "Build whisper.cpp and parakeet with Vulkan backend" OFF)
if(APPLE)
    option(VLC_SUBTITLE_METAL "Build whisper.cpp and parakeet with Apple Metal" ON)
    set(GGML_METAL ${VLC_SUBTITLE_METAL} CACHE BOOL "" FORCE)
else()
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
endif()

if(UNIX AND NOT WIN32)
    set(VLC_SUBTITLE_VOXTRAL_DEFAULT ON)
else()
    set(VLC_SUBTITLE_VOXTRAL_DEFAULT OFF)
endif()
option(VLC_SUBTITLE_VOXTRAL "Build the Voxtral Realtime 4B backend"
       ${VLC_SUBTITLE_VOXTRAL_DEFAULT})
option(VLC_SUBTITLE_PARAKEET "Build the Parakeet backend" ON)
option(VLC_SUBTITLE_MOONSHINE "Build the Moonshine backend" ON)
set(VLC_SUBTITLE_MODULE_NAME "suboffline" CACHE STRING
    "Internal VLC module name (override only for isolated testing)")

# --- 1. Whisper.cpp Runtime ---
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

add_subdirectory("${WHISPER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/whisper"
                 EXCLUDE_FROM_ALL)

set(VLC_SUBTITLE_SOURCES
    src/subtitle.c
    runtime/audio_converter.cpp
    runtime/runtime.cpp
    runtime/whisper_runtime.cpp
)

# --- 2. Voxtral Runtime (POSIX mmap / Linux & macOS) ---
if(VLC_SUBTITLE_VOXTRAL)
    if(NOT EXISTS "${VOXTRAL_SOURCE_DIR}/voxtral.h")
        message(FATAL_ERROR
            "voxtral.c was not found at ${VOXTRAL_SOURCE_DIR}. "
            "Run: git submodule update --init --recursive")
    endif()

    file(READ "${VOXTRAL_SOURCE_DIR}/voxtral.c" VOXTRAL_CORE_SOURCE)
    string(REPLACE
        "    vox_mel_free(s->mel_ctx);\n    vox_mel_free(s->mel_ctx);"
        "    vox_mel_free(s->mel_ctx);"
        VOXTRAL_CORE_SOURCE "${VOXTRAL_CORE_SOURCE}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/voxtral-generated")
    file(WRITE "${CMAKE_BINARY_DIR}/voxtral-generated/voxtral.c"
         "${VOXTRAL_CORE_SOURCE}")

    file(READ "${VOXTRAL_SOURCE_DIR}/voxtral_kernels.c"
         VOXTRAL_KERNEL_SOURCE)
    set(VOXTRAL_CAUSAL_SGEMM
"    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                channels_out, out_length, K,
                1.0f,
                weight, K,
                im2col, out_length,
                0.0f,
                out, out_length);")
    set(VOXTRAL_CAUSAL_SGEMM_PORTABLE
"#ifdef USE_BLAS
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                channels_out, out_length, K,
                1.0f,
                weight, K,
                im2col, out_length,
                0.0f,
                out, out_length);
#else
    for (int oc = 0; oc < channels_out; oc++) {
        for (int ol = 0; ol < out_length; ol++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++)
                sum += weight[(size_t)oc * K + k] *
                       im2col[(size_t)k * out_length + ol];
            out[(size_t)oc * out_length + ol] = sum;
        }
    }
#endif")
    string(REPLACE "${VOXTRAL_CAUSAL_SGEMM}"
                   "${VOXTRAL_CAUSAL_SGEMM_PORTABLE}"
                   VOXTRAL_KERNEL_SOURCE "${VOXTRAL_KERNEL_SOURCE}")
    file(WRITE "${CMAKE_BINARY_DIR}/voxtral-generated/voxtral_kernels.c"
         "${VOXTRAL_KERNEL_SOURCE}")

    add_library(voxtral_runtime_c STATIC
        "${CMAKE_BINARY_DIR}/voxtral-generated/voxtral.c"
        "${CMAKE_BINARY_DIR}/voxtral-generated/voxtral_kernels.c"
        "${VOXTRAL_SOURCE_DIR}/voxtral_audio.c"
        "${VOXTRAL_SOURCE_DIR}/voxtral_encoder.c"
        "${VOXTRAL_SOURCE_DIR}/voxtral_decoder.c"
        "${VOXTRAL_SOURCE_DIR}/voxtral_tokenizer.c"
        "${VOXTRAL_SOURCE_DIR}/voxtral_safetensors.c"
    )
    target_include_directories(voxtral_runtime_c PUBLIC
        "${VOXTRAL_SOURCE_DIR}")
    if(NOT APPLE AND NOT WIN32)
        target_link_libraries(voxtral_runtime_c PUBLIC m)
    endif()
    set_target_properties(voxtral_runtime_c PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    if(APPLE)
        find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
        target_compile_definitions(voxtral_runtime_c PRIVATE USE_BLAS)
        target_link_libraries(voxtral_runtime_c PUBLIC
            "${ACCELERATE_FRAMEWORK}")
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(OPENBLAS QUIET IMPORTED_TARGET openblas)
        endif()
        if(TARGET PkgConfig::OPENBLAS)
            set(VLC_SUBTITLE_HAS_OPENBLAS ON)
            target_compile_definitions(voxtral_runtime_c PRIVATE
                USE_BLAS USE_OPENBLAS)
            target_link_libraries(voxtral_runtime_c PUBLIC
                PkgConfig::OPENBLAS)
        else()
            message(WARNING
                "OpenBLAS was not found. Voxtral will use its portable CPU "
                "kernels, which are functional but slow.")
        endif()
    endif()

    list(APPEND VLC_SUBTITLE_SOURCES runtime/voxtral_runtime.cpp)
endif()

# --- 3. Parakeet Runtime ---
if(VLC_SUBTITLE_PARAKEET)
    if(NOT EXISTS "${PARAKEET_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${PARAKEET_SOURCE_DIR}/third_party/ggml/CMakeLists.txt")
        message(FATAL_ERROR
            "parakeet.cpp was not found at ${PARAKEET_SOURCE_DIR}. "
            "Run: git submodule update --init --recursive")
    endif()

    set(PARAKEET_BINARY_DIR "${CMAKE_BINARY_DIR}/parakeet-external")
    set(PARAKEET_SHARED_LIBRARY
        "${PARAKEET_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}parakeet${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(PARAKEET_IMPORT_LIBRARY
        "${PARAKEET_BINARY_DIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}parakeet${CMAKE_IMPORT_LIBRARY_SUFFIX}")
    set(PARAKEET_LINKER_FLAGS "")
    if(UNIX AND NOT APPLE)
        set(PARAKEET_LINKER_FLAGS "-Wl,-Bsymbolic")
    endif()

    set(PARAKEET_BYPRODUCTS "${PARAKEET_SHARED_LIBRARY}")
    if(WIN32)
        list(APPEND PARAKEET_BYPRODUCTS "${PARAKEET_IMPORT_LIBRARY}")
    endif()

    set(PARAKEET_PLATFORM_ARGS "")
    set(PARAKEET_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    if(MSVC)
        string(APPEND PARAKEET_CXX_FLAGS " /utf-8 /MP")
        list(APPEND PARAKEET_PLATFORM_ARGS
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${PARAKEET_BINARY_DIR}"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG=${PARAKEET_BINARY_DIR}"
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=${PARAKEET_BINARY_DIR}"
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG=${PARAKEET_BINARY_DIR}"
        )
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND PARAKEET_PLATFORM_ARGS
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()

    ExternalProject_Add(parakeet_external
        SOURCE_DIR "${PARAKEET_SOURCE_DIR}"
        BINARY_DIR "${PARAKEET_BINARY_DIR}"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        INSTALL_COMMAND ""
        CMAKE_ARGS
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_CXX_FLAGS:STRING=${PARAKEET_CXX_FLAGS}"
            ${PARAKEET_PLATFORM_ARGS}
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${PARAKEET_BINARY_DIR}"
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${PARAKEET_BINARY_DIR}"
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${PARAKEET_BINARY_DIR}"
            "-DCMAKE_SHARED_LINKER_FLAGS=${PARAKEET_LINKER_FLAGS}"
            "-DBUILD_SHARED_LIBS=OFF"
            "-DGGML_CCACHE=OFF"
            "-DGGML_NATIVE=OFF"
            "-DPARAKEET_SHARED=ON"
            "-DPARAKEET_BUILD_CLI=OFF"
            "-DPARAKEET_BUILD_SERVER=OFF"
            "-DPARAKEET_BUILD_TESTS=OFF"
            "-DPARAKEET_GGML_VULKAN=${VLC_SUBTITLE_VULKAN}"
            "-DPARAKEET_GGML_METAL=${GGML_METAL}"
        BUILD_BYPRODUCTS ${PARAKEET_BYPRODUCTS}
    )

    add_library(parakeet_runtime SHARED IMPORTED GLOBAL)
    set_target_properties(parakeet_runtime PROPERTIES
        IMPORTED_LOCATION "${PARAKEET_SHARED_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PARAKEET_SOURCE_DIR}/include"
    )
    if(WIN32)
        set_target_properties(parakeet_runtime PROPERTIES
            IMPORTED_IMPLIB "${PARAKEET_IMPORT_LIBRARY}")
    endif()
    add_dependencies(parakeet_runtime parakeet_external)
    list(APPEND VLC_SUBTITLE_SOURCES runtime/parakeet_runtime.cpp)
endif()

# --- 4. Moonshine Runtime ---
if(VLC_SUBTITLE_MOONSHINE)
    if(NOT EXISTS "${MOONSHINE_SOURCE_DIR}/core/moonshine-c-api.h" OR
       NOT EXISTS "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/CMakeLists.txt")
        message(FATAL_ERROR
            "Moonshine was not found at ${MOONSHINE_SOURCE_DIR}. "
            "Run: git submodule update --init --recursive")
    endif()

    set(MOONSHINE_PREPARED_SOURCE_DIR "${CMAKE_BINARY_DIR}/moonshine-source")
    set(MOONSHINE_BINARY_DIR "${CMAKE_BINARY_DIR}/moonshine-external")
    set(MOONSHINE_SHARED_LIBRARY
        "${MOONSHINE_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}moonshine${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(MOONSHINE_IMPORT_LIBRARY
        "${MOONSHINE_BINARY_DIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}moonshine${CMAKE_IMPORT_LIBRARY_SUFFIX}")

    if(WIN32)
        set(MOONSHINE_ONNXRUNTIME_LIBRARY
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/windows/x86_64/onnxruntime.dll")
        set(MOONSHINE_ONNXRUNTIME_IMPLIB
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/windows/x86_64/onnxruntime.lib")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
            set(MACOS_ARCH "arm64")
        else()
            set(MACOS_ARCH "x86_64")
        endif()
        set(MOONSHINE_ONNXRUNTIME_LIBRARY
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/macos/${MACOS_ARCH}/libonnxruntime.1.23.2.dylib")
        # Fall back to Homebrew or system onnxruntime if Git LFS pointer was not fetched
        if(EXISTS "${MOONSHINE_ONNXRUNTIME_LIBRARY}")
            file(SIZE "${MOONSHINE_ONNXRUNTIME_LIBRARY}" MOONSHINE_ORT_SIZE)
            if(MOONSHINE_ORT_SIZE LESS 1024)
                find_library(BREW_ONNXRUNTIME onnxruntime PATHS /opt/homebrew/lib /usr/local/lib)
                if(BREW_ONNXRUNTIME)
                    set(MOONSHINE_ONNXRUNTIME_LIBRARY "${BREW_ONNXRUNTIME}")
                endif()
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(MOONSHINE_ONNXRUNTIME_LIBRARY
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/linux/aarch64/libonnxruntime.so.1")
    else()
        set(MOONSHINE_ONNXRUNTIME_LIBRARY
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/linux/x86_64/libonnxruntime.so.1")
    endif()
    if(NOT EXISTS "${MOONSHINE_ONNXRUNTIME_LIBRARY}")
        message(FATAL_ERROR
            "Moonshine bundled ONNX Runtime library was not found at ${MOONSHINE_ONNXRUNTIME_LIBRARY}")
    endif()

    set(MOONSHINE_PLATFORM_ARGS "")
    set(MOONSHINE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    if(MSVC)
        string(APPEND MOONSHINE_CXX_FLAGS " /utf-8 /MP")
        list(APPEND MOONSHINE_PLATFORM_ARGS
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${MOONSHINE_BINARY_DIR}"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG=${MOONSHINE_BINARY_DIR}"
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=${MOONSHINE_BINARY_DIR}"
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG=${MOONSHINE_BINARY_DIR}"
        )
    else()
        string(APPEND MOONSHINE_CXX_FLAGS " -Wno-error=unused-parameter")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND MOONSHINE_PLATFORM_ARGS
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()

    set(MOONSHINE_BYPRODUCTS "${MOONSHINE_SHARED_LIBRARY}")
    if(WIN32)
        list(APPEND MOONSHINE_BYPRODUCTS "${MOONSHINE_IMPORT_LIBRARY}")
    endif()

    ExternalProject_Add(moonshine_external
        SOURCE_DIR "${MOONSHINE_PREPARED_SOURCE_DIR}"
        BINARY_DIR "${MOONSHINE_BINARY_DIR}"
        DOWNLOAD_COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${MOONSHINE_SOURCE_DIR}"
            "-DDEST_DIR=<SOURCE_DIR>"
            "-DSPEAKER_STUB=${CMAKE_CURRENT_SOURCE_DIR}/runtime/moonshine_speaker_stub.cpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/runtime/prepare_moonshine.cmake"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        INSTALL_COMMAND ""
        CMAKE_ARGS
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_CXX_FLAGS:STRING=${MOONSHINE_CXX_FLAGS}"
            ${MOONSHINE_PLATFORM_ARGS}
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${MOONSHINE_BINARY_DIR}"
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${MOONSHINE_BINARY_DIR}"
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${MOONSHINE_BINARY_DIR}"
            "-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"
            "-DCMAKE_INSTALL_RPATH:STRING=$ORIGIN"
            "-DMOONSHINE_BUILD_SHARED=ON"
            "-DMOONSHINE_TTS_BUILD_ONNX=OFF"
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build <BINARY_DIR>
            --target moonshine --parallel
        BUILD_BYPRODUCTS ${MOONSHINE_BYPRODUCTS}
    )

    add_library(moonshine_onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(moonshine_onnxruntime PROPERTIES
        IMPORTED_LOCATION "${MOONSHINE_ONNXRUNTIME_LIBRARY}")
    if(WIN32)
        set_target_properties(moonshine_onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${MOONSHINE_ONNXRUNTIME_IMPLIB}")
    endif()

    add_library(moonshine_runtime SHARED IMPORTED GLOBAL)
    set_target_properties(moonshine_runtime PROPERTIES
        IMPORTED_LOCATION "${MOONSHINE_SHARED_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MOONSHINE_SOURCE_DIR}/core"
        INTERFACE_LINK_LIBRARIES moonshine_onnxruntime
    )
    if(WIN32)
        set_target_properties(moonshine_runtime PROPERTIES
            IMPORTED_IMPLIB "${MOONSHINE_IMPORT_LIBRARY}")
    endif()
    add_dependencies(moonshine_runtime moonshine_external)
    list(APPEND VLC_SUBTITLE_SOURCES runtime/moonshine_runtime.cpp)
endif()

# --- 5. Main Plugin Target ---
add_library(suboffline_plugin MODULE ${VLC_SUBTITLE_SOURCES})

target_compile_definitions(suboffline_plugin PRIVATE
    PIC
    MODULE_STRING="${VLC_SUBTITLE_MODULE_NAME}"
)
if(VLC_SUBTITLE_VOXTRAL)
    target_compile_definitions(suboffline_plugin PRIVATE VLC_SUBTITLE_HAVE_VOXTRAL=1)
    if(VLC_SUBTITLE_HAS_OPENBLAS)
        target_compile_definitions(suboffline_plugin PRIVATE VLC_SUBTITLE_VOXTRAL_OPENBLAS=1)
    endif()
endif()
if(VLC_SUBTITLE_PARAKEET)
    target_compile_definitions(suboffline_plugin PRIVATE VLC_SUBTITLE_HAVE_PARAKEET=1)
endif()
if(VLC_SUBTITLE_MOONSHINE)
    target_compile_definitions(suboffline_plugin PRIVATE VLC_SUBTITLE_HAVE_MOONSHINE=1)
endif()

target_include_directories(suboffline_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
)

target_link_libraries(suboffline_plugin PRIVATE
    VLC::Plugin
    whisper
    Freetype::Freetype
)

if(TARGET libsrt)
    target_link_libraries(suboffline_plugin PRIVATE libsrt)
elseif(TARGET PkgConfig::SRT)
    target_link_libraries(suboffline_plugin PRIVATE PkgConfig::SRT)
endif()

if(APPLE)
    target_link_options(suboffline_plugin PRIVATE -Wl,-undefined,dynamic_lookup)
endif()

if(VLC_SUBTITLE_VOXTRAL)
    target_link_libraries(suboffline_plugin PRIVATE voxtral_runtime_c)
endif()
if(VLC_SUBTITLE_PARAKEET)
    target_link_libraries(suboffline_plugin PRIVATE parakeet_runtime)
    add_dependencies(suboffline_plugin parakeet_external)
    add_custom_command(TARGET suboffline_plugin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PARAKEET_SHARED_LIBRARY}"
                "$<TARGET_FILE_DIR:suboffline_plugin>")
endif()
if(VLC_SUBTITLE_MOONSHINE)
    target_link_libraries(suboffline_plugin PRIVATE moonshine_runtime)
    add_dependencies(suboffline_plugin moonshine_external)
    add_custom_command(TARGET suboffline_plugin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MOONSHINE_SHARED_LIBRARY}"
                "$<TARGET_FILE_DIR:suboffline_plugin>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MOONSHINE_ONNXRUNTIME_LIBRARY}"
                "$<TARGET_FILE_DIR:suboffline_plugin>")
endif()

if(UNIX AND (VLC_SUBTITLE_PARAKEET OR VLC_SUBTITLE_MOONSHINE))
    if(APPLE)
        set_property(TARGET suboffline_plugin APPEND PROPERTY
            BUILD_RPATH "@loader_path")
        set_property(TARGET suboffline_plugin APPEND PROPERTY
            INSTALL_RPATH "@loader_path")
    else()
        set_property(TARGET suboffline_plugin APPEND PROPERTY
            BUILD_RPATH "$ORIGIN")
        set_property(TARGET suboffline_plugin APPEND PROPERTY
            INSTALL_RPATH "$ORIGIN")
    endif()
endif()

set_target_properties(suboffline_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "suboffline_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
)

# --- 6. Installation ---
if(VLC_PLUGINS_DIR)
    install(TARGETS suboffline_plugin
            LIBRARY DESTINATION "${VLC_PLUGINS_DIR}/control"
            RUNTIME DESTINATION "${VLC_PLUGINS_DIR}/control")
    if(VLC_SUBTITLE_PARAKEET)
        install(FILES "${PARAKEET_SHARED_LIBRARY}"
                DESTINATION "${VLC_PLUGINS_DIR}/control")
    endif()
    if(VLC_SUBTITLE_MOONSHINE)
        install(FILES "${MOONSHINE_SHARED_LIBRARY}"
                      "${MOONSHINE_ONNXRUNTIME_LIBRARY}"
                DESTINATION "${VLC_PLUGINS_DIR}/control")
    endif()
endif()
