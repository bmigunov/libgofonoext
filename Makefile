# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release pkgconfig
.PHONY: print_debug_lib print_release_lib
.PHONY: print_debug_link print_release_link

#
# Required packages
#

PKGS = glib-2.0 gio-2.0 gio-unix-2.0 libgofono libglibutil

#
# Default target
#

all: debug release pkgconfig

#
# Sources
#

SRC = \
  gofonoext_call.c \
  gofonoext_mm.c \
  gofonoext_version.c
GEN_SRC = \
  org.nemomobile.ofono.ModemManager.c \
  org.nemomobile.ofono.SimSettings.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
GEN_DIR = $(BUILD_DIR)
SPEC_DIR = spec
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Library version
#

$(foreach v,MAJOR MINOR RELEASE,$(eval VERSION_$v=$(shell grep -E "^ *\#define +GOFONOEXT_VERSION_"$v" +[0-9]+$$" "$(INCLUDE_DIR)/gofonoext_version.h" | sed 's/  */ /g' | cut -d ' ' -f 3)))

ifeq ($(and $(VERSION_MAJOR),$(VERSION_MINOR),$(VERSION_RELEASE)),)
$(error "Unable to determine library version")
endif

# Version for pkg-config
PCVERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Library name
#

NAME = gofonoext
LIB_NAME = lib$(NAME)
LIB_DEV_SYMLINK = $(LIB_NAME).so
LIB_SYMLINK1 = $(LIB_DEV_SYMLINK).$(VERSION_MAJOR)
LIB_SYMLINK2 = $(LIB_SYMLINK1).$(VERSION_MINOR)
LIB_SONAME = $(LIB_SYMLINK1)
LIB = $(LIB_SONAME).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall -Wno-unused-parameter
INCLUDES = -I$(INCLUDE_DIR) -I$(GEN_DIR)
BASE_FLAGS = -fPIC $(CFLAGS)
FULL_CFLAGS = $(BASE_FLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(BASE_FLAGS) -shared -Wl,-soname -Wl,$(LIB_SONAME) \
  $(shell pkg-config --libs $(PKGS))
DEBUG_FLAGS = -g
RELEASE_FLAGS =

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
DEBUG_LDFLAGS = $(LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(LDFLAGS) $(RELEASE_FLAGS)

#
# Files
#

PKGCONFIG = \
  $(BUILD_DIR)/$(LIB_NAME).pc
DEBUG_OBJS = \
  $(GEN_SRC:%.c=$(DEBUG_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(GEN_SRC:%.c=$(RELEASE_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
GEN_FILES = $(GEN_SRC:%=$(GEN_DIR)/%)
.PRECIOUS: $(GEN_FILES)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(GEN_FILES): | $(GEN_DIR)
$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)
DEBUG_LINK = $(DEBUG_BUILD_DIR)/$(LIB_SONAME)
RELEASE_LINK = $(RELEASE_BUILD_DIR)/$(LIB_SONAME)

debug: $(DEBUG_LIB) $(DEBUG_LINK)

release: $(RELEASE_LIB) $(RELEASE_LINK)

pkgconfig: $(PKGCONFIG)

print_debug_lib:
	@echo $(DEBUG_LIB)

print_release_lib:
	@echo $(RELEASE_LIB)

print_debug_link:
	@echo $(DEBUG_LINK)

print_release_link:
	@echo $(RELEASE_LINK)

clean:
	rm -f *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~ rpm/*~
	rm -fr $(BUILD_DIR) RPMS installroot
	rm -fr debian/tmp debian/libgofono debian/libgofono-dev
	rm -f documentation.list debian/files debian/*.substvars
	rm -f debian/*.debhelper.log debian/*.debhelper debian/*~

$(GEN_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(DEBUG_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c -I. $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c -I. $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_BUILD_DIR) $(DEBUG_OBJS)
	$(LD) $(DEBUG_OBJS) $(DEBUG_LDFLAGS) -o $@

$(RELEASE_LIB): $(RELEASE_BUILD_DIR) $(RELEASE_OBJS)
	$(LD) $(RELEASE_OBJS) $(RELEASE_LDFLAGS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

$(DEBUG_BUILD_DIR)/$(LIB_SYMLINK1): $(DEBUG_BUILD_DIR)/$(LIB_SYMLINK2)
	ln -sf $(LIB_SYMLINK2) $@

$(RELEASE_BUILD_DIR)/$(LIB_SYMLINK1): $(RELEASE_BUILD_DIR)/$(LIB_SYMLINK2)
	ln -sf $(LIB_SYMLINK2) $@

$(DEBUG_BUILD_DIR)/$(LIB_SYMLINK2): $(DEBUG_LIB)
	ln -sf $(LIB) $@

$(RELEASE_BUILD_DIR)/$(LIB_SYMLINK2): $(RELEASE_LIB)
	ln -sf $(LIB) $@

$(PKGCONFIG): $(LIB_NAME).pc.in
	sed -e 's/\[version\]/'$(PCVERSION)/g $< > $@

#
# Install
#

INSTALL_PERM  = 644
INSTALL_OWNER = $(shell id -u)
INSTALL_GROUP = $(shell id -g)

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d
INSTALL_FILES = $(INSTALL) -m $(INSTALL_PERM)

INSTALL_LIB_DIR = $(DESTDIR)/usr/lib
INSTALL_INCLUDE_DIR = $(DESTDIR)/usr/include/$(NAME)
INSTALL_PKGCONFIG_DIR = $(DESTDIR)/usr/lib/pkgconfig

INSTALL_ALIAS = $(INSTALL_LIB_DIR)/$(LIB_SHORTCUT)

install: $(INSTALL_LIB_DIR)
	$(INSTALL_FILES) $(RELEASE_LIB) $(INSTALL_LIB_DIR)
	ln -sf $(LIB) $(INSTALL_LIB_DIR)/$(LIB_SYMLINK2)
	ln -sf $(LIB_SYMLINK2) $(INSTALL_LIB_DIR)/$(LIB_SYMLINK1)

install-dev: install $(INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR)
	$(INSTALL_FILES) $(INCLUDE_DIR)/*.h $(INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)
	ln -sf $(LIB_SYMLINK1) $(INSTALL_LIB_DIR)/$(LIB_DEV_SYMLINK)

$(INSTALL_LIB_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PKGCONFIG_DIR):
	$(INSTALL_DIRS) $@
