#
# Makefile for win32 burp executables
# Using MinGW cross-compiler on GNU/Linux
#
#  Written by Robert Nelson, June 2006
#
#  Absolute paths used in place of relative paths
#      Kern Sibbald, October 2008
#   Split Win32 and Win64 objects and binaries. However
#     the clean is a significant kludge -- hard coded.
#     The object directories are deleted during make clean,
#     so don't point them to any source directory.
#

# Global Configuration

ECHO_CMD = @

# Main Burp source dir
MAINDIR := /media/home/graham/keep/forkula/burp-1.1.29
#
# Build dir -- i.e. normally  src/win32
BUILDDIR := /media/home/graham/keep/forkula/burp-1.1.29/src/win32
#
# Dir above Burp where we find depkgs
TOPDIR := /media/home/graham/keep/forkula/burp-1.1.29
#
# where we find depkgs
DEPKGS := $(TOPDIR)/depkgs-mingw64
DEPKGS32 := $(TOPDIR)/depkgs-mingw32

DOCDIR := $(TOPDIR)/docs

MINGW_BIN := /media/home/graham/keep/forkula/burp-1.1.29/cross-tools-mingw64/mingw-w64/bin
MINGW_INCLUDE := /media/home/graham/keep/forkula/burp-1.1.29/cross-tools-mingw64/mingw-w64/x86_64-pc-mingw32/include
MINGW_DLLDIR := /media/home/graham/keep/forkula/burp-1.1.29/cross-tools-mingw64/mingw-w64/x86_64-pc-mingw32/bin
MINGW64PREFIX := x86_64-pc-

CC = $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-g++ $(DEFINES) $(INCLUDES)
CXX = $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-g++ $(DEFINES) $(INCLUDES)
AR := $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-ar
RANLIB := $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-ranlib
WINDRES := $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-windres
DLLTOOL := $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-dlltool
OBJCPY := $(MINGW_BIN)/$(MINGW64PREFIX)mingw32-objcopy
NSIS_DIR := $(DEPKGS32)/nsis
STAB2CV := $(DEPKGS32)/tools/bin/stab2cv

WIN64=yes

ifeq ($(WIN64),yes)
   CFLAGS := -g -Wall -mno-cygwin -mthreads -O3 -fno-strict-aliasing -DHAVE_VSS64
   LIBS_NETWORK := -lws2_32
   WIN_VERSION := 64
   BINDIR := $(BUILDDIR)/release64
   LIBDIR := $(BUILDDIR)/release64
   OBJDIR := obj64
else
   CFLAGS := -g -Wall -mno-cygwin -m32 -mwin32 -mthreads -O3 -fno-strict-aliasing
   LIBS_NETWORK := -lwsock32
   WIN_VERSION := 32
   BINDIR := $(BUILDDIR)/release32
   LIBDIR := $(BUILDDIR)/release32
   OBJDIR := obj32
endif

OBJDIRS := obj32 obj64

LDFLAGS := -g -Wall -mno-cygwin -Wl,--disable-auto-import

INCLUDE_DDK := -I$(MINGW_INCLUDE)/ddk
INCLUDE_BURP := -I$(MAINDIR)/src -I$(BUILDDIR)/compat
INCLUDE_ZLIB := -I$(DEPKGS)/include
INCLUDE_VSS := -I$(DEPKGS)/vss
INCLUDE_ICONS := -I../libwin32
INCLUDE_OPENSSL := -I$(DEPKGS)/include

LIBS_ZLIB := \
	$(DEPKGS)/lib/libz.a

LIBS_SSL := \
	$(DEPKGS)/lib/libssl.dll.a

LIBS_CRYPTO := \
	$(DEPKGS)/lib/libcrypto.dll.a

LIBS_RSYNC := \
	$(DEPKGS)/lib/librsync.a

LIBS_BURP := \
	$(LIBDIR)/libburp.a

HAVES := \
	-DHAVE_WIN32 \
	-DHAVE_MINGW \
	-DHAVE_ZLIB_H \
	-DHAVE_LIBZ \
	-DHAVE_CRYPTO \
	-DHAVE_OPENSSL \
	-DHAVE_TLS