CC = cc
PKG_CONFIG = pkg-config
INSTALL = install
CFLAGS = -g -O2 -Wall -Wextra
LDFLAGS =
LIBS =

VLC_PLUGIN_CFLAGS := $(shell $(PKG_CONFIG) --cflags vlc-plugin)
VLC_PLUGIN_LIBS := $(shell $(PKG_CONFIG) --libs vlc-plugin)
VLC_PLUGIN_DIR := $(shell $(PKG_CONFIG) --variable=pluginsdir vlc-plugin)

plugindir := $(VLC_PLUGIN_DIR)/control

override CC += -std=gnu11
override CPPFLAGS += -DPIC -I. -Isrc -DMODULE_STRING=\"suboffline\"
override CFLAGS += -fPIC $(VLC_PLUGIN_CFLAGS)
override LIBS += $(VLC_PLUGIN_LIBS)

ifeq ($(strip $(OS)),)
  OS=$(shell uname -s)
endif

CC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)

ifeq ($(OS),Windows_NT)
  SUFFIX := dll
  ifeq ($(findstring x86_64,$(CC_MACHINE)),)
    $(error Windows builds require a 64-bit MinGW compiler, e.g. x86_64-w64-mingw32-gcc)
  endif
  override LDFLAGS += -Wl,-no-undefined
else ifeq ($(OS),Linux)
  SUFFIX := so
  ifneq ($(shell uname -m),x86_64)
    $(error Linux builds are supported only on x86_64)
  endif
  ifneq ($(findstring -m32,$(CC)),)
    $(error Linux 32-bit builds are not supported)
  endif
  override LDFLAGS += -Wl,-no-undefined
else
  $(error Unsupported OS '$(OS)'. vlc-subtitle supports Linux 64-bit and Windows 64-bit)
endif

TARGET = libsuboffline_plugin.$(SUFFIX)
SOURCES = subtitle.c
OBJECTS = $(SOURCES:%.c=src/%.o)

all: $(TARGET)

install: all
	mkdir -p -- $(DESTDIR)$(plugindir)
	$(INSTALL) -m 0755 $(TARGET) $(DESTDIR)$(plugindir)

install-strip:
	$(MAKE) install INSTALL="$(INSTALL) -s"

uninstall:
	rm -f -- $(DESTDIR)$(plugindir)/$(TARGET)

clean:
	rm -f -- $(TARGET) libvlcsubtitle_plugin.so libvlcsubtitle_plugin.dll libsubtitle_plugin.so libsubtitle_plugin.dll src/*.o

mostlyclean: clean

$(OBJECTS): src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

.PHONY: all install install-strip uninstall clean mostlyclean
