#
# Makefile for a Video Disk Recorder plugin
#
# $Id$

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
#
PLUGIN = reelbox

OS=$(shell lsb_release -si)
ARCH=$(shell uname -m | sed 's/x86_//;s/i[3-6]86/32/')
VER=$(shell lsb_release -sr)

# set it if you want to compile the skin for use with the reelbox
#REELSKIN=1

# set it if you want to compile the plugin compiled in old reelbox source tree
#REELVDR=1


## Customizing since 3.1

# disable SD
HD_ONLY=1

# disable non-HDMI output related code (video+audio)
HDMI_ONLY=1


### The object files (add further files here):

OBJS = $(PLUGIN).o ac3.o AudioDecoder.o AudioDecoderIec60958.o AudioDecoderMpeg1.o \
	AudioDecoderNull.o AudioDecoderPcm.o AudioOut.o \
	AudioPacketQueue.o AudioPlayer.o AudioPlayerBsp.o AudioPlayerHd.o \
	BspCommChannel.o BspOsd.o BspOsdProvider.o BkgPicPlayer.o \
	bspchannel.o bspshmlib.o dts.o fs453settings.o iec60958.o MpegPes.o \
	hdchannel.o hdshmlib.o HdCommChannel.o \
	Reel.o ReelBoxDevice.o ReelBoxMenu.o \
	VideoPlayer.o VideoPlayerBsp.o VideoPlayerHd.o \
	VideoPlayerPip.o VideoPlayerPipBsp.o VideoPlayerPipHd.o \
	VdrXineMpIf.o HdOsd.o HdOsdProvider.o HdTrueColorOsd.o HdFbTrueColorOsd.o setupmenu.o

### The directory environment:

#VDRDIR = ../../..
#LIBDIR = ../../lib
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell pkg-config --variable=$(1) vdr || pkg-config --variable=$(1) ../../../vdr.pc))
LIBDIR = $(DESTDIR)$(call PKGCFG,libdir)
LOCDIR = $(DESTDIR)$(call PKGCFG,locdir)

ifdef HD_ONLY
  DEFINES=-DHD_ONLY=1
endif

ifdef HDMI_ONLY
  DEFINES=-DHDMI_ONLY=1
endif

ifdef REELVDR
  BSPSHM ?= ./utils/bspshm
  HDSHM ?= ./utils/hdshm3/src
else
  BSPSHM = ../../../utils/bspshm
  HDSHM = ../../../utils/hdshm3/src
endif

TMPDIR ?= /tmp

BSPINCLUDE = -I$(BSPSHM) -I$(BSPSHM)/include
HDINCLUDE = -I$(HDSHM) -I$(HDSHM)/include
ifdef REELBUILD
LIBMAD     ?= ../../../../temp/docimage/libs/libmad
LIBASOUND  ?= ../../../../temp/docimage/libs/alsa-lib
INCLUDES   += -I$(LIBASOUND)/include
INCLUDES   += -I$(LIBMAD)
LDFLAGS    += -L$(LIBASOUND)/src/.libs
LDFLAGS    += -L$(LIBMAD)/.libs
endif

### Allow user defined options to overwrite defaults:
export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

#CXX      ?= g++
#CXXFLAGS ?= -g -O3 -Wall -Woverloaded-virtual -Wno-parentheses

#ifdef PLUGIN
#CFLAGS   += -fPIC
#CXXFLAGS += -fPIC
#endif
#DEFINES  += -D_GNU_SOURCE -D_LARGEFILE_SOURCE
#-include $(VDRDIR)/Make.config

### Includes and Defines (add further entries here):
#INCLUDES += -I$(VDRDIR)/include -I$(DVBDIR)/include
INCLUDES += $(BSPINCLUDE) $(HDINCLUDE)
INCLUDES += `freetype-config --cflags`

# default
LIBPNG = -lpng

ifeq ($(OS), Fedora)
ifeq ($(shell test $(VER) -ge 33; echo $$?),0)
  # Fedora >= 33
  INCLUDES += -I/usr/include/ffmpeg
  DEFINES  += -DNEW_FFMPEG
  # select libpng12
  DEFINES  += -DUSE_LIBPNG12			# FIXED: invalid use of incomplete type 'png_info'
  LIBPNG   = -lpng12
endif # Fedora 33
endif # Fedora

ifdef REELSKIN
  DEFINES += -DREELSKIN
  OBJS += BspTrueColorOsd.o ReelSkin.o
else
  DEFINES += -DNOT_THEME_LIKE
endif

DEFINES += -DPLAYER_VERSION=\"$(PLAYER_VERSION)\" -D__LINUX__

ifdef REELVDR
  DEFINES += -DREELVDR
else
  DEFINES += -DNOT_THEME_LIKE
endif

LIBS += -lasound -lmad $(LIBPNG) -lavcodec -lswscale -la52 -lpng

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' $(PLUGIN).c | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The version number of VDR (taken from VDR's "config.h"):

#APIVERSION = $(shell grep 'define APIVERSION ' $(VDRDIR)/config.h | awk '{ print $$3 }' | sed -e 's/"//g')
APIVERSION = $(call PKGCFG,apiversion)

VDRLOCALE = $(shell grep '^LOCALEDIR' $(VDRDIR)/Makefile)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so


### Includes and Defines (add further entries here):

INCLUDES +=
DEFINES  += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'

ifdef DEBUG
  DEFINES += -DDEBUG
  CXXFLAGS += -g
endif

### Targets:

#plug: $(SOFILE)

all: $(SOFILE) i18n

### Implicit rules:

%.o: %.c
	$(CXX) $(CXXFLAGS) -D__STDC_CONSTANT_MACROS -c $(DEFINES) -DPLUGIN_NAME='"$(PLUGIN)"' $(INCLUDES) -o $@ $<

# Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c)   > $@

-include $(DEPFILE)

### Internationalization (I18N):
#ifneq ($(strip $(VDRLOCALE)),)
### do gettext based i18n stuff

PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.c $(PLUGIN).h $(EXTRA_I18N))
	echo $(I18Nmsgs)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --from-code=utf-8 --msgid-bugs-address='<reelbox-devel@mailings.reelbox.org>' $^ -o $@ `ls $^`

#%.po:
%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@


.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)



#i18n-dist: $(I18Nmsgs)
#i18n-dist:
#	for i in `ls po/*.po` ; do \
#		odir=`echo $$i | cut -b4-8` ;\
#		msgfmt -c -o $(LOCALEDIR)/$$odir/LC_MESSAGES/vdr-$(PLUGIN).mo $$i ;\
#	done

$(SOFILE): $(OBJS)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) $(LDFLAGS) $(LIBS) -o $@


install-lib: $(SOFILE)
	install -D $^ $(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: distclean
	@rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@rm -f $(TMPDIR)/$(ARCHIVE)/$(PLUGIN).kdevelop
	@rm -f $(TMPDIR)/$(ARCHIVE)/$(PLUGIN).kdevelop.filelist
	@rm -f $(TMPDIR)/$(ARCHIVE)/$(PLUGIN).kdevelop.pcs
	@rm -f $(TMPDIR)/$(ARCHIVE)/$(PLUGIN).kdevses
	@rm -rf $(TMPDIR)/$(ARCHIVE)/CVS
	@rm -rf $(TMPDIR)/$(ARCHIVE)/Examples/CVS
	@rm -rf $(TMPDIR)/$(ARCHIVE)/Patch/CVS
	@ln -s $(ARCHIVE) $(TMPDIR)/$(PLUGIN)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE) $(PLUGIN)
	@rm -rf $(TMPDIR)/$(ARCHIVE) $(TMPDIR)/$(PLUGIN)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~


distclean: clean
	@-rm -f $(PODIR)/*.pot

#useless-target-for-compatibility-with-vanilla-vdr:
#	$(LIBDIR)/$@.$(APIVERSION)
