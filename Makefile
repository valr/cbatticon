##
##
## to set by the user
#
# verbosity (default off)
V = 0

ifeq ($(V),0)
Q=@
else
Q=
endif

# programs
CC ?= gcc
PKG_CONFIG ?= pkg-config
RM = rm -f
INSTALL = install
INSTALL_BIN = $(INSTALL) -m755
INSTALL_DATA = $(INSTALL) -m644

# flags and libs
CFLAGS ?= -O2
CFLAGS += -Wall -Wno-format
PKG_DEPS = gtk+-2.0 libnotify libudev
LIBS += $(shell $(PKG_CONFIG) --libs $(PKG_DEPS)) -lm
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKG_DEPS))

# variables
PACKAGE_NAME = cbatticon
PREFIX ?= /usr
BINDIR = $(PREFIX)/bin
DOCDIR = $(PREFIX)/share/doc/$(PACKAGE_NAME)

BIN = $(PACKAGE_NAME)
SOURCEFILES := $(wildcard *.c)
OBJECTS := $(patsubst %.c,%.o,$(SOURCEFILES))


$(BIN): $(OBJECTS)
	@echo -e '\033[1;31mLinking CC executable $@\033[0m'
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJECTS): $(SOURCEFILES)
	@echo -e '\033[0;32mBuilding CC object $@\033[0m'
	$(Q)$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

install: $(BIN)
	@echo -e '\033[1;33mInstalling $(PACKAGE_NAME)\033[0m'
	$(Q)$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(Q)$(INSTALL_BIN) $(BIN) "$(DESTDIR)$(BINDIR)"/
	$(Q)$(INSTALL) -d "$(DESTDIR)$(DOCDIR)"
	$(Q)$(INSTALL_DATA) README "$(DESTDIR)$(DOCDIR)"/

uninstall:
	@echo -e '\033[1;33mUninstalling $(PACKAGE_NAME)\033[0m'
	$(Q)$(RM) "$(DESTDIR)$(BINDIR)"/$(BIN)
	$(Q)$(RM) "$(DESTDIR)$(DOCDIR)"/README

clean :
	@echo -e '\033[1;33mCleaning up source directory\033[0m'
	$(Q)$(RM) $(BIN) $(OBJECTS)

.PHONY: install uninstall clean
