cmake_minimum_required(VERSION 3.22.1)

project(vlc_subtitle LANGUAGES C CXX)

include(GNUInstallDirs)
include(ExternalProject)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VLC_PLUGIN REQUIRED IMPORTED_TARGET vlc-plugin)

set(WHISPER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/whisper.cpp"
    CACHE PATH "Path to the whisper.cpp source tree")
set(VOXTRAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/voxtral.c"
    CACHE PATH "Path to the voxtral.c source tree")
set(PARAKEET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/parakeet.cpp"
    CACHE PATH "Path to the parakeet.cpp source tree")
set(MOONSHINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime/moonshine"
    CACHE PATH "Path to the Moonshine source tree")
option(VLC_SUBTITLE_VULKAN "Build whisper.cpp with its Vulkan backend" OFF)
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

set(VLC_SUBTITLE_SOURCES
    src/subtitle.c
    runtime/audio_converter.cpp
    runtime/runtime.cpp
    runtime/whisper_runtime.cpp
)

if(VLC_SUBTITLE_VOXTRAL)
    if(NOT EXISTS "${VOXTRAL_SOURCE_DIR}/voxtral.h")
        message(FATAL_ERROR
            "voxtral.c was not found at ${VOXTRAL_SOURCE_DIR}. "
            "Run: git submodule update --init --recursive")
    endif()

    # antirez/voxtral.c 134d366 accidentally frees the streaming mel context
    # twice. Generate a corrected translation unit without dirtying the pinned
    # dependency; the replacement naturally becomes a no-op after upstream fixes it.
    file(READ "${VOXTRAL_SOURCE_DIR}/voxtral.c" VOXTRAL_CORE_SOURCE)
    string(REPLACE
        "    vox_mel_free(s->mel_ctx);\n    vox_mel_free(s->mel_ctx);"
        "    vox_mel_free(s->mel_ctx);"
        VOXTRAL_CORE_SOURCE "${VOXTRAL_CORE_SOURCE}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/voxtral-generated")
    file(WRITE "${CMAKE_BINARY_DIR}/voxtral-generated/voxtral.c"
         "${VOXTRAL_CORE_SOURCE}")

    # The same revision calls CBLAS unconditionally in causal convolution.
    # Supply its scalar equivalent for dependency-free builds.
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
    if(NOT APPLE)
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
        pkg_check_modules(OPENBLAS QUIET IMPORTED_TARGET openblas)
        if(OPENBLAS_FOUND)
            set(VLC_SUBTITLE_HAS_OPENBLAS ON)
            target_compile_definitions(voxtral_runtime_c PRIVATE
                USE_BLAS USE_OPENBLAS)
            target_link_libraries(voxtral_runtime_c PUBLIC
                PkgConfig::OPENBLAS)
        else()
            message(WARNING
                "OpenBLAS was not found. Voxtral will use its portable CPU "
                "kernels, which are functional but extremely slow.")
        endif()
    endif()

    list(APPEND VLC_SUBTITLE_SOURCES runtime/voxtral_runtime.cpp)
endif()

if(VLC_SUBTITLE_PARAKEET)
    if(NOT EXISTS "${PARAKEET_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${PARAKEET_SOURCE_DIR}/third_party/ggml/CMakeLists.txt")
        message(FATAL_ERROR
            "parakeet.cpp was not found at ${PARAKEET_SOURCE_DIR}. "
            "Run: git submodule update --init --recursive")
    endif()

    # Parakeet and Whisper pin incompatible ggml revisions with the same symbol
    # names. Build Parakeet in a separate CMake tree and bind references inside
    # libparakeet to its own ggml copy so both engines can coexist safely.
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
    if(MINGW)
        # mudler/parakeet.cpp e8acc617 uses int64_t in backend.hpp without
        # including <cstdint>; MinGW does not provide that type transitively.
        string(APPEND PARAKEET_CXX_FLAGS " -include cstdint")
    endif()
    if(CMAKE_CROSSCOMPILING)
        set(PARAKEET_SYSTEM_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
        if(NOT PARAKEET_SYSTEM_PROCESSOR)
            # The supported Windows build is x86-64, but CMake leaves this
            # unset when only CMAKE_SYSTEM_NAME is provided by the Makefile.
            set(PARAKEET_SYSTEM_PROCESSOR "x86_64")
        endif()
        list(APPEND PARAKEET_PLATFORM_ARGS
            "-DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}"
            "-DCMAKE_SYSTEM_PROCESSOR:STRING=${PARAKEET_SYSTEM_PROCESSOR}")
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

if(VLC_SUBTITLE_MOONSHINE)
    if(NOT EXISTS "${MOONSHINE_SOURCE_DIR}/core/moonshine-c-api.h" OR
       NOT EXISTS "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/CMakeLists.txt")
        message(FATAL_ERROR
            "Moonshine was not found at ${MOONSHINE_SOURCE_DIR}. "
            "Run: git submodule update --init --recursive")
    endif()

    set(MOONSHINE_PREPARED_SOURCE_DIR
        "${CMAKE_BINARY_DIR}/moonshine-source")
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
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(MOONSHINE_ONNXRUNTIME_LIBRARY
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/linux/aarch64/libonnxruntime.so.1")
    else()
        set(MOONSHINE_ONNXRUNTIME_LIBRARY
            "${MOONSHINE_SOURCE_DIR}/core/third-party/onnxruntime/lib/linux/x86_64/libonnxruntime.so.1")
    endif()
    if(NOT EXISTS "${MOONSHINE_ONNXRUNTIME_LIBRARY}")
        message(FATAL_ERROR
            "Moonshine's bundled ONNX Runtime library was not found at "
            "${MOONSHINE_ONNXRUNTIME_LIBRARY}")
    endif()

    set(MOONSHINE_PLATFORM_ARGS "")
    if(CMAKE_CROSSCOMPILING)
        set(MOONSHINE_SYSTEM_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
        if(NOT MOONSHINE_SYSTEM_PROCESSOR)
            set(MOONSHINE_SYSTEM_PROCESSOR "x86_64")
        endif()
        list(APPEND MOONSHINE_PLATFORM_ARGS
            "-DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}"
            "-DCMAKE_SYSTEM_PROCESSOR:STRING=${MOONSHINE_SYSTEM_PROCESSOR}")
    endif()

    set(MOONSHINE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(APPEND MOONSHINE_CXX_FLAGS
        " -Wno-error=unused-parameter")
    if(MINGW)
        string(APPEND MOONSHINE_CXX_FLAGS " -include cstdint")
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

add_library(suboffline_plugin MODULE ${VLC_SUBTITLE_SOURCES})

target_compile_features(suboffline_plugin PRIVATE c_std_11 cxx_std_17)
target_compile_definitions(suboffline_plugin PRIVATE
    PIC
    MODULE_STRING="${VLC_SUBTITLE_MODULE_NAME}"
)
if(VLC_SUBTITLE_VOXTRAL)
    target_compile_definitions(suboffline_plugin PRIVATE
        VLC_SUBTITLE_HAVE_VOXTRAL=1)
    if(VLC_SUBTITLE_HAS_OPENBLAS)
        target_compile_definitions(suboffline_plugin PRIVATE
            VLC_SUBTITLE_VOXTRAL_OPENBLAS=1)
    endif()
endif()
if(VLC_SUBTITLE_PARAKEET)
    target_compile_definitions(suboffline_plugin PRIVATE
        VLC_SUBTITLE_HAVE_PARAKEET=1)
endif()
if(VLC_SUBTITLE_MOONSHINE)
    target_compile_definitions(suboffline_plugin PRIVATE
        VLC_SUBTITLE_HAVE_MOONSHINE=1)
endif()
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
    set_property(TARGET suboffline_plugin APPEND PROPERTY
        BUILD_RPATH "$ORIGIN")
    set_property(TARGET suboffline_plugin APPEND PROPERTY
        INSTALL_RPATH "$ORIGIN")
endif()

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
