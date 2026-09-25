if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DEST_DIR OR NOT DEFINED SPEAKER_STUB)
    message(FATAL_ERROR "SOURCE_DIR, DEST_DIR, and SPEAKER_STUB are required")
endif()

file(REMOVE_RECURSE "${DEST_DIR}")
file(MAKE_DIRECTORY "${DEST_DIR}")
configure_file("${SOURCE_DIR}/CMakeLists.txt"
               "${DEST_DIR}/CMakeLists.txt" COPYONLY)
file(COPY "${SOURCE_DIR}/core" DESTINATION "${DEST_DIR}"
     PATTERN "build" EXCLUDE
     PATTERN "moonshine-tts" EXCLUDE
     PATTERN "speaker-embedding-model-data.cpp" EXCLUDE)
configure_file("${SPEAKER_STUB}"
               "${DEST_DIR}/core/speaker-embedding-model-data.cpp" COPYONLY)

# Keep the private dependency build strictly STT-only. Upstream configures its
# TTS subtree even when MOONSHINE_TTS_BUILD_ONNX=OFF, and its C API source still
# compiles the TTS/G2P entry points. Remove both without modifying the submodule.
set(CORE_CMAKE_PATH "${DEST_DIR}/core/CMakeLists.txt")
file(READ "${CORE_CMAKE_PATH}" CORE_CMAKE_SOURCE)
set(TTS_SUBDIRECTORY_BLOCK
"add_subdirectory(
    \${CMAKE_CURRENT_LIST_DIR}/moonshine-tts
    \${CMAKE_CURRENT_LIST_DIR}/moonshine-tts/build
)")
string(FIND "${CORE_CMAKE_SOURCE}"
       "${TTS_SUBDIRECTORY_BLOCK}" TTS_SUBDIRECTORY_SECTION)
if(TTS_SUBDIRECTORY_SECTION LESS 0)
    message(FATAL_ERROR "Moonshine TTS subdirectory block was not found")
endif()
string(REPLACE "${TTS_SUBDIRECTORY_BLOCK}" ""
       CORE_CMAKE_SOURCE "${CORE_CMAKE_SOURCE}")
string(REPLACE "-Werror" "-Wno-error"
       CORE_CMAKE_SOURCE "${CORE_CMAKE_SOURCE}")
file(WRITE "${CORE_CMAKE_PATH}" "${CORE_CMAKE_SOURCE}")

set(C_API_PATH "${DEST_DIR}/core/moonshine-c-api.cpp")
file(READ "${C_API_PATH}" C_API_SOURCE)
string(REPLACE "#include \"moonshine-asset-catalog.h\"\n" ""
               C_API_SOURCE "${C_API_SOURCE}")
string(REPLACE "#include \"moonshine-g2p.h\"\n" ""
               C_API_SOURCE "${C_API_SOURCE}")
string(REPLACE "#include \"moonshine-tts.h\"\n" ""
               C_API_SOURCE "${C_API_SOURCE}")
string(FIND "${C_API_SOURCE}"
       "/* ------------------------------ TEXT TO SPEECH" TTS_SECTION)
if(TTS_SECTION LESS 0)
    message(FATAL_ERROR "Moonshine TTS C API section was not found")
endif()
string(SUBSTRING "${C_API_SOURCE}" 0 ${TTS_SECTION} C_API_SOURCE)
file(WRITE "${C_API_PATH}" "${C_API_SOURCE}")
