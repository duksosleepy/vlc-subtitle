#[=======================================================================[.rst:
FindVLC
-------

Finds the libVLC plugin SDK headers and libraries for building VLC plugins.

Imported Targets
^^^^^^^^^^^^^^^^
``VLC::Plugin``
  Imported target representing the libVLC plugin interface.

Result Variables
^^^^^^^^^^^^^^^^
``VLC_FOUND``
  True if the VLC plugin SDK was found.
``VLC_INCLUDE_DIRS``
  Include directories needed to build VLC plugins.
``VLC_LIBRARIES``
  Libraries needed to link VLC plugins.
``VLC_PLUGINS_DIR``
  Path to the VLC plugins directory (e.g. for installation).

Hints
^^^^^
Set ``VLC_SDK_DIR`` to the root of the extracted VLC SDK directory.
#]=======================================================================]

include(FindPackageHandleStandardArgs)

if(WIN32)
    set(VLC_SDK_DIR "$ENV{VLC_SDK_DIR}" CACHE PATH "Path to extracted VLC Windows SDK")

    if(NOT VLC_SDK_DIR OR NOT EXISTS "${VLC_SDK_DIR}")
        set(VLC_AUTO_SDK_DIR "${CMAKE_BINARY_DIR}/vlc-sdk")
        
        # Check if already provisioned in build directory
        file(GLOB_RECURSE EXISTING_SDK_INC LIST_DIRECTORIES true
             "${VLC_AUTO_SDK_DIR}/*/include/vlc/plugins/vlc_plugin.h"
             "${VLC_AUTO_SDK_DIR}/include/vlc/plugins/vlc_plugin.h")

        if(EXISTING_SDK_INC)
            list(GET EXISTING_SDK_INC 0 EXISTING_INC_FILE)
            get_filename_component(VLC_PLUGINS_INC_DIR "${EXISTING_INC_FILE}" DIRECTORY)
            get_filename_component(VLC_VLC_INC_DIR "${VLC_PLUGINS_INC_DIR}" DIRECTORY)
            get_filename_component(VLC_INC_ROOT "${VLC_VLC_INC_DIR}" DIRECTORY)
            get_filename_component(VLC_SDK_DETECTED "${VLC_INC_ROOT}" DIRECTORY)
            set(VLC_SDK_DIR "${VLC_SDK_DETECTED}" CACHE PATH "Path to extracted VLC Windows SDK" FORCE)
        else()
            message(STATUS "VLC_SDK_DIR not specified. Auto-downloading VideoLAN Windows SDK...")
            
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(VLC_ARCH "win64")
            else()
                set(VLC_ARCH "win32")
            endif()
            
            set(VLC_VERSION "3.0.21")
            set(VLC_ARCHIVE_NAME "vlc-${VLC_VERSION}-${VLC_ARCH}.7z")
            set(VLC_PRIMARY_URL "https://get.videolan.org/vlc/${VLC_VERSION}/${VLC_ARCH}/${VLC_ARCHIVE_NAME}")
            set(VLC_FALLBACK_URL "https://download.videolan.org/pub/videolan/vlc/${VLC_VERSION}/${VLC_ARCH}/${VLC_ARCHIVE_NAME}")
            set(VLC_ARCHIVE_PATH "${CMAKE_BINARY_DIR}/${VLC_ARCHIVE_NAME}")

            if(NOT EXISTS "${VLC_ARCHIVE_PATH}")
                message(STATUS "Downloading VLC SDK from ${VLC_PRIMARY_URL}...")
                file(DOWNLOAD "${VLC_PRIMARY_URL}" "${VLC_ARCHIVE_PATH}"
                     STATUS DOWNLOAD_STATUS
                     SHOW_PROGRESS
                     TLS_VERIFY ON)
                list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
                if(NOT STATUS_CODE EQUAL 0)
                    message(STATUS "Primary download failed. Trying fallback ${VLC_FALLBACK_URL}...")
                    file(DOWNLOAD "${VLC_FALLBACK_URL}" "${VLC_ARCHIVE_PATH}"
                         STATUS FALLBACK_STATUS
                         SHOW_PROGRESS
                         TLS_VERIFY ON)
                    list(GET FALLBACK_STATUS 0 FB_STATUS_CODE)
                    if(NOT FB_STATUS_CODE EQUAL 0)
                        message(FATAL_ERROR "Failed to download VideoLAN Windows SDK. Please download ${VLC_ARCHIVE_NAME} manually and set -DVLC_SDK_DIR=<path-to-sdk>.")
                    endif()
                endif()
            endif()

            message(STATUS "Extracting ${VLC_ARCHIVE_NAME}...")
            file(MAKE_DIRECTORY "${VLC_AUTO_SDK_DIR}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E tar xvf "${VLC_ARCHIVE_PATH}"
                WORKING_DIRECTORY "${VLC_AUTO_SDK_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
                OUTPUT_QUIET
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract VLC SDK archive: ${VLC_ARCHIVE_PATH}")
            endif()

            file(GLOB_RECURSE FOUND_SDK_INC LIST_DIRECTORIES true
                 "${VLC_AUTO_SDK_DIR}/*/include/vlc/plugins/vlc_plugin.h"
                 "${VLC_AUTO_SDK_DIR}/include/vlc/plugins/vlc_plugin.h")

            if(FOUND_SDK_INC)
                list(GET FOUND_SDK_INC 0 FOUND_INC_FILE)
                get_filename_component(VLC_PLUGINS_INC_DIR "${FOUND_INC_FILE}" DIRECTORY)
                get_filename_component(VLC_VLC_INC_DIR "${VLC_PLUGINS_INC_DIR}" DIRECTORY)
                get_filename_component(VLC_INC_ROOT "${VLC_VLC_INC_DIR}" DIRECTORY)
                get_filename_component(VLC_SDK_DETECTED "${VLC_INC_ROOT}" DIRECTORY)
                set(VLC_SDK_DIR "${VLC_SDK_DETECTED}" CACHE PATH "Path to extracted VLC Windows SDK" FORCE)
                message(STATUS "Auto-detected VLC SDK at: ${VLC_SDK_DIR}")
            else()
                message(FATAL_ERROR "Extracted SDK did not contain include/vlc/plugins/vlc_plugin.h.")
            endif()
        endif()
    endif()

    find_path(VLC_INCLUDE_DIR
        NAMES vlc/vlc.h
        PATHS "${VLC_SDK_DIR}/include"
        NO_DEFAULT_PATH
    )
    find_path(VLC_PLUGINS_INCLUDE_DIR
        NAMES vlc_plugin.h
        PATHS "${VLC_SDK_DIR}/include/vlc/plugins"
        NO_DEFAULT_PATH
    )
    find_library(VLCCORE_LIBRARY
        NAMES libvlccore vlccore
        PATHS "${VLC_SDK_DIR}/lib"
        NO_DEFAULT_PATH
    )
    find_library(VLC_LIBRARY
        NAMES libvlc vlc
        PATHS "${VLC_SDK_DIR}/lib"
        NO_DEFAULT_PATH
    )

    set(VLC_INCLUDE_DIRS ${VLC_INCLUDE_DIR} ${VLC_PLUGINS_INCLUDE_DIR})
    set(VLC_LIBRARIES ${VLCCORE_LIBRARY})
    if(VLC_LIBRARY)
        list(APPEND VLC_LIBRARIES ${VLC_LIBRARY})
    endif()

    if(NOT VLC_PLUGINS_DIR)
        set(VLC_PLUGINS_DIR "C:/Program Files/VideoLAN/VLC/plugins" CACHE PATH "VLC plugins directory")
    endif()

    find_package_handle_standard_args(VLC
        REQUIRED_VARS VLC_INCLUDE_DIRS VLC_LIBRARIES
    )

    if(VLC_FOUND AND NOT TARGET VLC::Plugin)
        add_library(VLC::Plugin UNKNOWN IMPORTED)
        set_target_properties(VLC::Plugin PROPERTIES
            IMPORTED_LOCATION "${VLCCORE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VLC_INCLUDE_DIRS}"
            INTERFACE_COMPILE_DEFINITIONS "__PLUGIN__;_FILE_OFFSET_BITS=64;_REENTRANT;_THREAD_SAFE"
        )
    endif()

elseif(APPLE)
    # macOS: plugins resolve libvlccore symbols dynamically at runtime via dynamic_lookup
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(VLC_PKG QUIET vlc-plugin)
    endif()

    set(VLC_SEARCH_PATHS
        "$ENV{VLC_SDK_DIR}"
        "${VLC_SDK_DIR}"
        "/opt/homebrew/include"
        "/usr/local/include"
        "/Applications/VLC.app/Contents/MacOS/include"
    )

    find_path(VLC_PLUGINS_INCLUDE_DIR
        NAMES vlc_plugin.h
        PATHS ${VLC_SEARCH_PATHS}
        PATH_SUFFIXES vlc/plugins plugins vlc
    )

    find_path(VLC_INCLUDE_DIR
        NAMES vlc/vlc.h vlc_common.h
        PATHS ${VLC_SEARCH_PATHS}
        PATH_SUFFIXES vlc/plugins vlc include
    )

    if(NOT VLC_PLUGINS_INCLUDE_DIR)
        # Auto-download VLC 3.0 headers if not found on machine
        set(VLC_AUTO_HDR_DIR "${CMAKE_BINARY_DIR}/vlc-headers")
        if(NOT EXISTS "${VLC_AUTO_HDR_DIR}/include/vlc/plugins/vlc_plugin.h")
            message(STATUS "VLC headers not found on macOS. Downloading VLC 3.0 headers...")
            set(VLC_HDR_URL "https://code.videolan.org/videolan/vlc/-/archive/3.0.21/vlc-3.0.21.tar.gz")
            set(VLC_HDR_TAR "${CMAKE_BINARY_DIR}/vlc-headers.tar.gz")
            file(DOWNLOAD "${VLC_HDR_URL}" "${VLC_HDR_TAR}" SHOW_PROGRESS TLS_VERIFY ON)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E tar xzf "${VLC_HDR_TAR}"
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            )
            file(GLOB VLC_SRC_DIR "${CMAKE_BINARY_DIR}/vlc-3*")
            if(VLC_SRC_DIR)
                file(MAKE_DIRECTORY "${VLC_AUTO_HDR_DIR}/include/vlc/plugins")
                file(COPY "${VLC_SRC_DIR}/include/" DESTINATION "${VLC_AUTO_HDR_DIR}/include/vlc/plugins")
            endif()
        endif()

        if(EXISTS "${VLC_AUTO_HDR_DIR}/include/vlc/plugins/vlc_plugin.h")
            set(VLC_PLUGINS_INCLUDE_DIR "${VLC_AUTO_HDR_DIR}/include/vlc/plugins")
            set(VLC_INCLUDE_DIR "${VLC_AUTO_HDR_DIR}/include")
        endif()
    endif()

    set(VLC_INCLUDE_DIRS ${VLC_INCLUDE_DIR} ${VLC_PLUGINS_INCLUDE_DIR})

    if(NOT VLC_PLUGINS_DIR)
        set(VLC_PLUGINS_DIR "$ENV{HOME}/Library/Application Support/org.videolan.vlc/plugins"
            CACHE PATH "VLC plugins directory on macOS")
    endif()

    find_package_handle_standard_args(VLC
        REQUIRED_VARS VLC_PLUGINS_INCLUDE_DIR
    )

    if(VLC_FOUND AND NOT TARGET VLC::Plugin)
        add_library(VLC::Plugin INTERFACE IMPORTED)
        set_target_properties(VLC::Plugin PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VLC_INCLUDE_DIRS}"
            INTERFACE_COMPILE_DEFINITIONS "__PLUGIN__;_FILE_OFFSET_BITS=64;_REENTRANT;_THREAD_SAFE"
            INTERFACE_LINK_OPTIONS "-Wl,-undefined,dynamic_lookup"
        )
    endif()

else()
    # Linux / Unix: use pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VLC_PKG REQUIRED IMPORTED_TARGET vlc-plugin)

    pkg_get_variable(VLC_PLUGINS_DIR vlc-plugin pluginsdir)

    set(VLC_INCLUDE_DIRS ${VLC_PKG_INCLUDE_DIRS})
    set(VLC_LIBRARIES ${VLC_PKG_LIBRARIES})

    find_package_handle_standard_args(VLC
        REQUIRED_VARS VLC_PKG_FOUND
    )

    if(VLC_FOUND AND NOT TARGET VLC::Plugin)
        add_library(VLC::Plugin INTERFACE IMPORTED)
        target_link_libraries(VLC::Plugin INTERFACE PkgConfig::VLC_PKG)
    endif()
endif()
