#####  DP_MAKE_TARGET autodetection and arch specific variables ##### 

ifndef DP_MAKE_TARGET

# Win32
ifdef windir
	DP_MAKE_TARGET=mingw
else

# UNIXes
DP_ARCH:=$(shell uname)
ifneq ($(filter %BSD,$(DP_ARCH)),)
	DP_MAKE_TARGET=bsd
else
ifeq ($(DP_ARCH), Darwin)
	DP_MAKE_TARGET=macosx
else
	DP_MAKE_TARGET=linux

endif  # ifeq ($(DP_ARCH), Darwin)
endif  # ifneq ($(filter %BSD,$(DP_ARCH)),)
endif  # ifdef windir
endif  # ifndef DP_MAKE_TARGET

# If we're not on compiling for Win32, we need additional information
ifneq ($(DP_MAKE_TARGET), mingw)
	DP_ARCH:=$(shell uname)
	DP_MACHINE:=$(shell uname -m)
endif


# Command used to delete files
ifdef windir
	CMD_RM=del
else
	CMD_RM=$(CMD_UNIXRM)
endif

# 64bits AMD CPUs use another lib directory
ifeq ($(DP_MACHINE),x86_64)
	UNIX_X11LIBPATH:=-L/usr/X11R6/lib64
else
	UNIX_X11LIBPATH:=-L/usr/X11R6/lib
endif


# Linux configuration
ifeq ($(DP_MAKE_TARGET), linux)
	OBJ_SOUND=$(OBJ_LINUXSOUND)
	LIB_SOUND=$(LIB_LINUXSOUND)
	OBJ_CD=$(OBJ_LINUXCD)

	OBJ_CL=$(OBJ_GLX)

	LDFLAGS_CL=$(LDFLAGS_LINUXCL)
	LDFLAGS_SV=$(LDFLAGS_LINUXSV)
	LDFLAGS_SDL=$(LDFLAGS_LINUXSDL)

	EXE_CL=$(EXE_UNIXCL)
	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
endif

# Mac OS X configuration
ifeq ($(DP_MAKE_TARGET), macosx)
	OBJ_SOUND=$(OBJ_MACOSXSOUND)
	LIB_SOUND=$(LIB_MACOSXSOUND)
	OBJ_CD=$(OBJ_MACOSXCD)

	OBJ_CL=$(OBJ_AGL)

	LDFLAGS_CL=$(LDFLAGS_MACOSXCL)
	LDFLAGS_SV=$(LDFLAGS_MACOSXSV)
	LDFLAGS_SDL=$(LDFLAGS_MACOSXSDL)

	EXE_CL=$(EXE_UNIXCL)
	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
endif

# BSD configuration
ifeq ($(DP_MAKE_TARGET), bsd)
ifeq ($(DP_ARCH),FreeBSD)
	OBJ_SOUND=$(OBJ_OSSSOUND)
	LIB_SOUND=$(LIB_OSSSOUND)
else
	OBJ_SOUND=$(OBJ_BSDSOUND)
	LIB_SOUND=$(LIB_BSDSOUND)
endif
	OBJ_CD=$(OBJ_BSDCD)

	OBJ_CL=$(OBJ_GLX)

	LDFLAGS_CL=$(LDFLAGS_BSDCL)
	LDFLAGS_SV=$(LDFLAGS_BSDSV)
	LDFLAGS_SDL=$(LDFLAGS_BSDSDL)

	EXE_CL=$(EXE_UNIXCL)
	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
endif

# Win32 configuration
ifeq ($(DP_MAKE_TARGET), mingw)
	OBJ_SOUND=$(OBJ_WINSOUND)
	LIB_SOUND=$(LIB_WINSOUND)
	OBJ_CD=$(OBJ_WINCD)

	OBJ_CL=$(OBJ_WGL)

	LDFLAGS_CL=$(LDFLAGS_WINCL)
	LDFLAGS_SV=$(LDFLAGS_WINSV)
	LDFLAGS_SDL=$(LDFLAGS_WINSDL)

	EXE_CL=$(EXE_WINCL)
	EXE_SV=$(EXE_WINSV)
	EXE_SDL=$(EXE_WINSDL)
endif


##### GNU Make specific definitions #####

DO_LD=$(CC) -o $@ $^ $(LDFLAGS)


##### Definitions shared by all makefiles #####
include makefile.inc


##### Dependency files #####

-include *.d
