CMAKE ?= cmake
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build/native
BUILD_TYPE ?= Release
WHISPER_SOURCE_DIR ?= $(CURDIR)/runtime/whisper.cpp
VLC_SUBTITLE_VULKAN ?= OFF

ifeq ($(strip $(OS)),)
  OS := $(shell uname -s)
endif

ifeq ($(OS),Windows_NT)
  SUFFIX := dll
  CMAKE_PLATFORM_FLAGS := -DCMAKE_SYSTEM_NAME=Windows
  ifneq ($(strip $(CC)),)
    CMAKE_PLATFORM_FLAGS += -DCMAKE_C_COMPILER=$(firstword $(CC))
  endif
  ifneq ($(strip $(CXX)),)
    CMAKE_PLATFORM_FLAGS += -DCMAKE_CXX_COMPILER=$(firstword $(CXX))
  endif
else ifeq ($(OS),Linux)
  SUFFIX := so
else
  $(error Unsupported OS '$(OS)'. vlc-subtitle supports Linux and Windows)
endif

TARGET := libsuboffline_plugin.$(SUFFIX)
BUILT_TARGET := $(BUILD_DIR)/plugin/$(TARGET)
VLC_PLUGIN_DIR := $(shell $(PKG_CONFIG) --variable=pluginsdir vlc-plugin 2>/dev/null)

all: $(TARGET)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DWHISPER_SOURCE_DIR=$(WHISPER_SOURCE_DIR) \
		-DVLC_SUBTITLE_VULKAN=$(VLC_SUBTITLE_VULKAN) \
		$(CMAKE_PLATFORM_FLAGS)

$(TARGET): configure
	$(CMAKE) --build $(BUILD_DIR) --target suboffline_plugin --parallel
	$(CMAKE) -E copy $(BUILT_TARGET) $@

install: $(TARGET)
	$(CMAKE) --install $(BUILD_DIR)

install-strip: $(TARGET)
	$(CMAKE) --install $(BUILD_DIR) --strip

uninstall:
	@test -n "$(VLC_PLUGIN_DIR)" || (echo "VLC plugin directory was not found"; exit 1)
	$(CMAKE) -E rm -f "$(DESTDIR)$(VLC_PLUGIN_DIR)/control/$(TARGET)"

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
	$(CMAKE) -E rm -f libsuboffline_plugin.so libsuboffline_plugin.dll

.PHONY: all configure install install-strip uninstall clean
