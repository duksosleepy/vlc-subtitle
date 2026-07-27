CMAKE ?= cmake
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build/native
BUILD_TYPE ?= Release
WHISPER_SOURCE_DIR ?= $(CURDIR)/runtime/whisper.cpp
VOXTRAL_SOURCE_DIR ?= $(CURDIR)/runtime/voxtral.c
PARAKEET_SOURCE_DIR ?= $(CURDIR)/runtime/parakeet.cpp
MOONSHINE_SOURCE_DIR ?= $(CURDIR)/runtime/moonshine
VLC_SUBTITLE_VULKAN ?= OFF

ifeq ($(strip $(OS)),)
  OS := $(shell uname -s)
endif

ifeq ($(OS),Windows_NT)
  SUFFIX := dll
  VLC_SUBTITLE_VOXTRAL ?= OFF
  VLC_SUBTITLE_PARAKEET ?= ON
  VLC_SUBTITLE_MOONSHINE ?= ON
  ONNXRUNTIME_TARGET := onnxruntime.dll
  CMAKE_PLATFORM_FLAGS := -DCMAKE_SYSTEM_NAME=Windows
  ifneq ($(strip $(CC)),)
    CMAKE_PLATFORM_FLAGS += -DCMAKE_C_COMPILER=$(firstword $(CC))
  endif
  ifneq ($(strip $(CXX)),)
    CMAKE_PLATFORM_FLAGS += -DCMAKE_CXX_COMPILER=$(firstword $(CXX))
  endif
else ifeq ($(OS),Linux)
  SUFFIX := so
  VLC_SUBTITLE_VOXTRAL ?= ON
  VLC_SUBTITLE_PARAKEET ?= ON
  VLC_SUBTITLE_MOONSHINE ?= ON
  ONNXRUNTIME_TARGET := libonnxruntime.so.1
else
  $(error Unsupported OS '$(OS)'. vlc-subtitle supports Linux and Windows)
endif

TARGET := libsuboffline_plugin.$(SUFFIX)
PARAKEET_TARGET := libparakeet.$(SUFFIX)
MOONSHINE_TARGET := libmoonshine.$(SUFFIX)
BUILT_TARGET := $(BUILD_DIR)/plugin/$(TARGET)
BUILT_PARAKEET_TARGET := $(BUILD_DIR)/plugin/$(PARAKEET_TARGET)
BUILT_MOONSHINE_TARGET := $(BUILD_DIR)/plugin/$(MOONSHINE_TARGET)
BUILT_ONNXRUNTIME_TARGET := $(BUILD_DIR)/plugin/$(ONNXRUNTIME_TARGET)
VLC_PLUGIN_DIR := $(shell $(PKG_CONFIG) --variable=pluginsdir vlc-plugin 2>/dev/null)

all: $(TARGET)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DWHISPER_SOURCE_DIR=$(WHISPER_SOURCE_DIR) \
		-DVOXTRAL_SOURCE_DIR=$(VOXTRAL_SOURCE_DIR) \
		-DPARAKEET_SOURCE_DIR=$(PARAKEET_SOURCE_DIR) \
		-DMOONSHINE_SOURCE_DIR=$(MOONSHINE_SOURCE_DIR) \
		-DVLC_SUBTITLE_VULKAN=$(VLC_SUBTITLE_VULKAN) \
		-DVLC_SUBTITLE_VOXTRAL=$(VLC_SUBTITLE_VOXTRAL) \
		-DVLC_SUBTITLE_PARAKEET=$(VLC_SUBTITLE_PARAKEET) \
		-DVLC_SUBTITLE_MOONSHINE=$(VLC_SUBTITLE_MOONSHINE) \
		$(CMAKE_PLATFORM_FLAGS)

$(TARGET): configure
	$(CMAKE) --build $(BUILD_DIR) --target suboffline_plugin --parallel
	$(CMAKE) -E copy $(BUILT_TARGET) $@
ifeq ($(VLC_SUBTITLE_PARAKEET),ON)
	$(CMAKE) -E copy $(BUILT_PARAKEET_TARGET) $(PARAKEET_TARGET)
endif
ifeq ($(VLC_SUBTITLE_MOONSHINE),ON)
	$(CMAKE) -E copy $(BUILT_MOONSHINE_TARGET) $(MOONSHINE_TARGET)
	$(CMAKE) -E copy $(BUILT_ONNXRUNTIME_TARGET) $(ONNXRUNTIME_TARGET)
endif

install: $(TARGET)
	$(CMAKE) --install $(BUILD_DIR)

install-strip: $(TARGET)
	$(CMAKE) --install $(BUILD_DIR) --strip

uninstall:
	@test -n "$(VLC_PLUGIN_DIR)" || (echo "VLC plugin directory was not found"; exit 1)
	$(CMAKE) -E rm -f "$(DESTDIR)$(VLC_PLUGIN_DIR)/control/$(TARGET)"
ifeq ($(VLC_SUBTITLE_PARAKEET),ON)
	$(CMAKE) -E rm -f "$(DESTDIR)$(VLC_PLUGIN_DIR)/control/$(PARAKEET_TARGET)"
endif
ifeq ($(VLC_SUBTITLE_MOONSHINE),ON)
	$(CMAKE) -E rm -f "$(DESTDIR)$(VLC_PLUGIN_DIR)/control/$(MOONSHINE_TARGET)"
	$(CMAKE) -E rm -f "$(DESTDIR)$(VLC_PLUGIN_DIR)/control/$(ONNXRUNTIME_TARGET)"
endif

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
	$(CMAKE) -E rm -f libsuboffline_plugin.so libsuboffline_plugin.dll \
		libparakeet.so libparakeet.dll libmoonshine.so libmoonshine.dll \
		libonnxruntime.so.1 onnxruntime.dll

.PHONY: all configure install install-strip uninstall clean
