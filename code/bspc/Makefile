#
# Makefile for the BSPC tool for the Gladiator Bot
#
# GNU Make required
#

COMPILE_PLATFORM=$(shell uname | sed -e 's/_.*//' | tr '[:upper:]' '[:lower:]' | sed -e 's/\//_/g')
COMPILE_ARCH=$(shell uname -m | sed -e 's/i.86/x86/' | sed -e 's/^arm.*/arm/')

#arm64 hack!
ifeq ($(shell uname -m), arm64)
  COMPILE_ARCH=arm64
endif
ifeq ($(shell uname -m), aarch64)
  COMPILE_ARCH=arm64
endif

ifeq ($(COMPILE_PLATFORM),sunos)
  # Solaris uname and GNU uname differ
  COMPILE_ARCH=$(shell uname -p | sed -e 's/i.86/x86/')
endif

ifneq ($(findstring mingw,$(COMPILE_PLATFORM)),)
  # MSYS2 environments differ from uname
  ifeq ($(MSYSTEM_CHOST),i686-w64-mingw32)
    COMPILE_PLATFORM=mingw32
    COMPILE_ARCH=x86
  endif
  ifeq ($(MSYSTEM_CHOST),x86_64-w64-mingw32)
    COMPILE_PLATFORM=mingw32
    COMPILE_ARCH=x86_64
  endif
endif


#############################################################################
#
# If you require a different configuration from the defaults below, create a
# new file named "Makefile.local" in the same directory as this file and define
# your parameters there. This allows you to change configuration without
# causing problems with keeping up to date with the repository.
#
#############################################################################
-include Makefile.local

# cygwin environment isn't supported but mingw packages can be used if installed
ifeq ($(COMPILE_PLATFORM),cygwin)
  PLATFORM=mingw32
endif
# MSYS2 msys environment is cygwin without mingw packages
ifeq ($(COMPILE_PLATFORM),msys)
  $(error MSYS2 MSYS environment is not supported, use MSYS2 MinGW 32-bit or 64-bit instead)
endif

ifndef PLATFORM
PLATFORM=$(COMPILE_PLATFORM)
endif
export PLATFORM

ifeq ($(PLATFORM),mingw32)
  MINGW=1
endif
ifeq ($(PLATFORM),mingw64)
  MINGW=1
endif

ifeq ($(COMPILE_ARCH),i86pc)
  COMPILE_ARCH=x86
endif

ifeq ($(COMPILE_ARCH),amd64)
  COMPILE_ARCH=x86_64
endif
ifeq ($(COMPILE_ARCH),x64)
  COMPILE_ARCH=x86_64
endif

ifeq ($(COMPILE_ARCH),powerpc)
  COMPILE_ARCH=ppc
endif
ifeq ($(COMPILE_ARCH),powerpc64)
  COMPILE_ARCH=ppc64
endif

ifeq ($(COMPILE_ARCH),axp)
  COMPILE_ARCH=alpha
endif

ifndef ARCH
ARCH=$(COMPILE_ARCH)
endif
export ARCH

ifneq ($(PLATFORM),$(COMPILE_PLATFORM))
  CROSS_COMPILING=1
else
  CROSS_COMPILING=0

  ifneq ($(ARCH),$(COMPILE_ARCH))
    CROSS_COMPILING=1
  endif
endif
export CROSS_COMPILING

ifndef MOUNT_DIR
MOUNT_DIR=..
endif

ifndef BUILD_DIR
BUILD_DIR=../../build
endif

ifndef USE_INTERNAL_ZLIB
USE_INTERNAL_ZLIB=1
endif

ifndef USE_LOCAL_HEADERS
USE_LOCAL_HEADERS=1
endif

ifndef DEBUG_CFLAGS
DEBUG_CFLAGS=-g -O0
endif

ifndef RELEASE_CFLAGS
RELEASE_CFLAGS=-O3
endif

#############################################################################

BD=$(BUILD_DIR)/bspc-debug-$(PLATFORM)-$(ARCH)
BR=$(BUILD_DIR)/bspc-release-$(PLATFORM)-$(ARCH)

BLIBDIR=$(MOUNT_DIR)/botlib
BSPCDIR=$(MOUNT_DIR)/bspc
CMDIR=$(MOUNT_DIR)/qcommon
ZDIR=$(MOUNT_DIR)/zlib

bin_path=$(shell which $(1) 2> /dev/null)

ifeq ($(V),1)
echo_cmd=@:
Q=
else
echo_cmd=@echo
Q=@
endif

define DO_CC
$(echo_cmd) "CC $<"
$(Q)$(CC) $(CFLAGS) -o $@ -c $<
endef

ifeq ($(USE_INTERNAL_ZLIB),1)
  BASE_CFLAGS += -DNO_GZIP
  BASE_CFLAGS += -I$(ZDIR)
else
  LIBS += -lz
endif

ifeq ($(USE_LOCAL_HEADERS),1)
  BASE_CFLAGS += -DUSE_LOCAL_HEADERS
endif

TARGETS=$(B)/bspc.$(ARCH)$(BINEXT)

BASE_CFLAGS+=-DCom_Memcpy=memcpy -DCom_Memset=memset

#############################################################################
# SETUP AND BUILD -- Linux
#############################################################################

INSTALL=install
MKDIR=mkdir

ifneq (,$(findstring "$(PLATFORM)", "linux" "gnu_kfreebsd" "kfreebsd-gnu" "gnu"))
  BASE_CFLAGS += -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
    -DARCH_STRING=\\\"$(ARCH)\\\"

  BASE_CFLAGS += -DBSPC -Dstricmp=strcasecmp

  ifeq ($(ARCH),x86)
    # linux32 make ...
    BASE_CFLAGS += -m32
  else
  ifeq ($(ARCH),ppc64)
    BASE_CFLAGS += -m64
  endif
  endif

  CFLAGS = $(BASE_CFLAGS)

  LIBS=-lm -lpthread

else # ifeq Linux

#############################################################################
# SETUP AND BUILD -- MAC OS X
#############################################################################

ifeq ($(PLATFORM),darwin)

  CC=gcc

  BINEXT=

  BASE_CFLAGS+= -DBSPC -Wall -Wimplicit -Wstrict-prototypes -Dstricmp=strcasecmp
  ifeq ($(ARCH),x86)
    # build 32bit
    BASE_CFLAGS += -m32
  else
    BASE_CFLAGS += -m64
  endif

  CFLAGS=$(BASE_CFLAGS)

  LDFLAGS=-lm

else # ifeq darwin


#############################################################################
# SETUP AND BUILD -- MINGW32
#############################################################################

ifdef MINGW

  ifeq ($(CROSS_COMPILING),1)
    # If CC is already set to something generic, we probably want to use
    # something more specific
    ifneq ($(findstring $(strip $(CC)),cc gcc),)
      CC=
    endif

    # We need to figure out the correct gcc and windres
    ifeq ($(ARCH),x86_64)
      MINGW_PREFIXES=x86_64-w64-mingw32 amd64-mingw32msvc
    endif
    ifeq ($(ARCH),x86)
      MINGW_PREFIXES=i686-w64-mingw32 i586-mingw32msvc i686-pc-mingw32
    endif

    ifndef CC
      CC=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
         $(call bin_path, $(MINGW_PREFIX)-gcc))))
    endif

    ifndef WINDRES
      WINDRES=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
         $(call bin_path, $(MINGW_PREFIX)-windres))))
    endif
  else
    # Some MinGW installations define CC to cc, but don't actually provide cc,
    # so check that CC points to a real binary and use gcc if it doesn't
    ifeq ($(call bin_path, $(CC)),)
      CC=gcc
    endif

    ifndef WINDRES
      WINDRES=windres
    endif
  endif

  BINEXT=.exe

  BASE_CFLAGS+= -DBSPC -Wall -Wimplicit -Wstrict-prototypes
  ifeq ($(ARCH),x86)
    # build 32bit
    BASE_CFLAGS += -m32
  else
    BASE_CFLAGS += -m64
  endif

  # In the absence of wspiapi.h, require Windows XP or later
  ifeq ($(shell test -e $(CMDIR)/wspiapi.h; echo $$?),1)
    BASE_CFLAGS += -DWINVER=0x501
  endif

  CFLAGS=$(BASE_CFLAGS)

  LDFLAGS=-lws2_32 -lwinmm -mwindows

else # ifdef MINGW

#############################################################################
# SETUP AND BUILD -- FREEBSD
#############################################################################

ifeq ($(PLATFORM),freebsd)

  $(error platform $(PLATFORM) is currently not supported)

else # ifeq freebsd

#############################################################################
# SETUP AND BUILD -- OPENBSD
#############################################################################

ifeq ($(PLATFORM),openbsd)

  $(error platform $(PLATFORM) is currently not supported)

else # ifeq openbsd

#############################################################################
# SETUP AND BUILD -- NETBSD
#############################################################################

ifeq ($(PLATFORM),netbsd)

  $(error platform $(PLATFORM) is currently not supported)

else # ifeq netbsd

#############################################################################
# SETUP AND BUILD -- IRIX
#############################################################################

ifeq ($(PLATFORM),irix64)

  $(error platform $(PLATFORM) is currently not supported)

  ARCH=mips  #default to MIPS

  CC = c99
  MKDIR = mkdir -p

else # ifeq IRIX

#############################################################################
# SETUP AND BUILD -- SunOS
#############################################################################

ifeq ($(PLATFORM),sunos)

  $(error platform $(PLATFORM) is currently not supported)

  CC=gcc
  INSTALL=ginstall
  MKDIR=gmkdir

  ifneq ($(ARCH),x86)
    ifneq ($(ARCH),sparc)
      $(error arch $(ARCH) is currently not supported)
    endif
  endif

else # ifeq sunos

#############################################################################
# SETUP AND BUILD -- GENERIC
#############################################################################

  $(error platform $(PLATFORM) is currently not supported)

endif #Linux
endif #darwin
endif #mingw32
endif #FreeBSD
endif #OpenBSD
endif #NetBSD
endif #IRIX
endif #SunOS

#############################################################################
# MAIN TARGETS
#############################################################################

default: release
all: debug release

debug:
	@$(MAKE) targets B=$(BD) CFLAGS="$(CFLAGS) $(DEPEND_CFLAGS) \
		$(DEBUG_CFLAGS)" V=$(V)

release:
	@$(MAKE) targets B=$(BR) CFLAGS="$(CFLAGS) $(DEPEND_CFLAGS) \
		$(RELEASE_CFLAGS)" V=$(V)

ifneq ($(call bin_path, tput),)
  TERM_COLUMNS=$(shell echo $$((`tput cols`-4)))
else
  TERM_COLUMNS=76
endif

NAKED_TARGETS=$(shell echo $(TARGETS) | sed -e "s!$(B)/!!g")

print_list=@for i in $(1); \
     do \
             echo "    $$i"; \
     done

ifneq ($(call bin_path, fmt),)
  print_wrapped=@echo $(1) | fmt -w $(TERM_COLUMNS) | sed -e "s/^\(.*\)$$/    \1/"
else
  print_wrapped=$(print_list)
endif

# Create the build directories, check libraries and print out
# an informational message, then start building
targets: makedirs
	@echo ""
	@echo "Building bspc in $(B):"
	@echo "  PLATFORM: $(PLATFORM)"
	@echo "  ARCH: $(ARCH)"
	@echo "  COMPILE_PLATFORM: $(COMPILE_PLATFORM)"
	@echo "  COMPILE_ARCH: $(COMPILE_ARCH)"
	@echo "  CC: $(CC)"
	@echo ""
	@echo "  CFLAGS:"
	$(call print_wrapped, $(CFLAGS))
	@echo ""
	@echo "  LDFLAGS:"
	$(call print_wrapped, $(LDFLAGS))
	@echo ""
	@echo "  LIBS:"
	$(call print_wrapped, $(LIBS))
	@echo ""
	@echo "  Output:"
	$(call print_list, $(NAKED_TARGETS))
	@echo ""
ifneq ($(TARGETS),)
	@$(MAKE) $(TARGETS) V=$(V)
endif

makedirs:
	@if [ ! -d $(BUILD_DIR) ];then $(MKDIR) $(BUILD_DIR);fi
	@if [ ! -d $(B) ];then $(MKDIR) $(B);fi
	@if [ ! -d $(B)/bspc ];then $(MKDIR) $(B)/bspc;fi
	@if [ ! -d $(B)/botlib ];then $(MKDIR) $(B)/botlib;fi
	@if [ ! -d $(B)/qcommon ];then $(MKDIR) $(B)/qcommon;fi
	@if [ ! -d $(B)/zlib ];then $(MKDIR) $(B)/zlib;fi

#############################################################################
# BUILD BSPC
#############################################################################

$(B)/botlib/%.o: $(BLIBDIR)/%.c
	$(DO_CC)
$(B)/bspc/%.o: $(BSPCDIR)/%.c
	$(DO_CC)
$(B)/qcommon/%.o: $(CMDIR)/%.c
	$(DO_CC)
$(B)/zlib/%.o: $(ZDIR)/%.c
	$(DO_CC)

BSPC_OBJS = \
	$(B)/bspc/aas_areamerging.o\
	$(B)/bspc/aas_cfg.o\
	$(B)/bspc/aas_create.o\
	$(B)/bspc/aas_edgemelting.o\
	$(B)/bspc/aas_facemerging.o\
	$(B)/bspc/aas_file.o\
	$(B)/bspc/aas_gsubdiv.o\
	$(B)/bspc/aas_map.o\
	$(B)/bspc/aas_prunenodes.o\
	$(B)/bspc/aas_store.o\
	$(B)/bspc/be_aas_bspc.o\
	$(B)/botlib/be_aas_bspq3.o\
	$(B)/botlib/be_aas_cluster.o\
	$(B)/botlib/be_aas_move.o\
	$(B)/botlib/be_aas_optimize.o\
	$(B)/botlib/be_aas_reach.o\
	$(B)/botlib/be_aas_sample.o\
	$(B)/bspc/brushbsp.o\
	$(B)/bspc/bspc.o\
	$(B)/qcommon/bsp.o\
	$(B)/qcommon/bsp_q3.o\
	$(B)/qcommon/bsp_q3ihv.o\
	$(B)/qcommon/bsp_q3test103.o\
	$(B)/qcommon/bsp_q3test106.o\
	$(B)/qcommon/bsp_fakk.o\
	$(B)/qcommon/bsp_sof2.o\
	$(B)/qcommon/bsp_ef2.o\
	$(B)/qcommon/bsp_mohaa.o\
	$(B)/qcommon/cm_load.o\
	$(B)/qcommon/cm_patch.o\
	$(B)/qcommon/cm_test.o\
	$(B)/qcommon/cm_trace.o\
	$(B)/qcommon/q_math.o\
	$(B)/qcommon/q_shared.o\
	$(B)/bspc/csg.o\
	$(B)/bspc/l_bsp_ent.o\
	$(B)/bspc/l_bsp_q3.o\
	$(B)/bspc/l_cmd.o\
	$(B)/botlib/l_libvar.o\
	$(B)/bspc/l_log.o\
	$(B)/bspc/l_math.o\
	$(B)/bspc/l_mem.o\
	$(B)/bspc/l_poly.o\
	$(B)/botlib/l_precomp.o\
	$(B)/bspc/l_qfiles.o\
	$(B)/botlib/l_script.o\
	$(B)/botlib/l_struct.o\
	$(B)/bspc/l_threads.o\
	$(B)/bspc/l_utils.o\
	$(B)/bspc/leakfile.o\
	$(B)/bspc/map.o\
	$(B)/bspc/map_q3.o\
	$(B)/qcommon/md4.o\
	$(B)/bspc/portals.o\
	$(B)/bspc/tree.o\
	$(B)/qcommon/ioapi.o\
	$(B)/qcommon/unzip.o

ifeq ($(USE_INTERNAL_ZLIB),1)
BSPC_OBJS += \
  $(B)/zlib/adler32.o \
  $(B)/zlib/crc32.o \
  $(B)/zlib/inffast.o \
  $(B)/zlib/inflate.o \
  $(B)/zlib/inftrees.o \
  $(B)/zlib/zutil.o
endif

$(B)/bspc.$(ARCH)$(BINEXT) : $(BSPC_OBJS)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BSPC_OBJS) $(LIBS)

#############################################################################
# MISC
#############################################################################

OBJ=$(BSPC_OBJS)
OBJ_D_FILES=$(filter %.d,$(OBJ:%.o=%.d))

clean: clean-debug clean-release

clean-debug:
	@$(MAKE) clean2 B=$(BD)

clean-release:
	@$(MAKE) clean2 B=$(BR)

clean2:
	@echo "CLEAN $(B)"
	@rm -f $(OBJ)
	@rm -f $(OBJ_D_FILES)
	@rm -f $(TARGETS)

depend:
	$(echo_cmd) "DEPEND $(B)"
	$(Q)$(CC) -MM $(BSPC_OBJS:.o=.c)

# Copy to main directory
install:
	cp $(BR)/bspc.$(ARCH)$(BINEXT) $(BSPCDIR)/../../../

