ENV := macOSX

ifeq ($(config_compiler),gcc)
 ifeq ($(origin CC), default)
  CC := llvm-gcc
 endif
 include $(currPath)/gcc.mk
 WARNINGS_CFLAGS += -Wno-attributes # for attributes not understood by llvm-gcc
else
 # default to clang
 config_compiler := clang
 ifeq ($(origin CC), default)
  CC := clang
 endif
 include $(currPath)/clang.mk
endif

ifdef RELEASE
	COMPILE_FLAGS += -DNS_BLOCK_ASSERTIONS
endif

OSX_SYSROOT := /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.8.sdk
OSX_FLAGS = -isysroot $(OSX_SYSROOT) -mmacosx-version-min=10.4
CPPFLAGS += $(OSX_FLAGS)
LDFLAGS += $(OSX_FLAGS)

LDFLAGS += -dead_strip -Wl,-S,-x
WHOLE_PROGRAM_CFLAGS := -fipa-pta -fwhole-program

system_externalSysroot := /opt/local
CPPFLAGS += -I$(system_externalSysroot)/include
#LDLIBS += -L$(system_externalSysroot)/lib