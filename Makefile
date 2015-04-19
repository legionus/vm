PROJECT = vm
VERSION = 0.0.1

sysconfdir ?= /etc
bootdir    ?= /boot
bindir     ?= /usr/bin
sbindir    ?= /usr/sbin
datadir    ?= /usr/share
libexecdir ?= /usr/libexec/$(PROJECT)
infodir    ?= $(datadir)/info
mandir     ?= $(datadir)/man
man1dir    ?= $(mandir)/man1
tmpdir     ?= /tmp
prefix     ?= $(datadir)/$(PROJECT)
DESTDIR    ?=

ifdef MKLOCAL
prefix     = $(CURDIR)
bindir     = $(CURDIR)
sbindir    = $(CURDIR)
libexecdir = $(CURDIR)
endif

CFLAGS = -Wall -Wextra -W -Wshadow -Wcast-align \
	-Wwrite-strings -Wconversion -Waggregate-return -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn \
	-Wmissing-format-attribute -Wredundant-decls -Wdisabled-optimization \
	-Wno-pointer-arith

CP = cp -a
INSTALL = install
MKDIR_P = mkdir -p
TOUCH_R = touch -r
HELP2MAN = env -i help2man -N
MAKEINFO_FLAGS = -D "VERSION $(VERSION)"

PROGRAMS    = vm
HELPERS     = \
    vm-command-debug \
    vm-command-init \
    vm-command-run \
    vm-command-sandbox \
    vm-command-setup
BIN_HELPERS = init
TARGETS     = $(PROGRAMS) $(HELPERS) $(BIN_HELPERS)
SUBDIRS     =

.PHONY: $(SUBDIRS)

all: $(SUBDIRS) $(TARGETS)

%: %.in
	sed \
		-e 's,@VERSION@,$(VERSION),g' \
		-e 's,@PROJECT@,$(PROJECT),g' \
		-e 's,@BOOTDIR@,$(bootdir),g' \
		-e 's,@CONFIG@,$(sysconfdir),g' \
		-e 's,@PREFIX@,$(prefix),g' \
		-e 's,@BINDIR@,$(bindir),g' \
		-e 's,@SBINDIR@,$(sbindir),g' \
		-e 's,@TMPDIR@,$(tmpdir),g' \
		-e 's,@LIBEXECDIR@,$(libexecdir),g' \
		<$< >$@
	$(TOUCH_R) $< $@
	chmod --reference=$< $@

init: guest/init.c
	$(CC) -static $(CFLAGS) -o $@ $<

install: $(TARGETS)
	$(MKDIR_P) -- $(DESTDIR)$(libexecdir)
	$(INSTALL) -p -m755 $(BIN_HELPERS) $(DESTDIR)$(libexecdir)/
	$(MKDIR_P) -- $(DESTDIR)$(prefix)
	$(INSTALL) -p -m644 vm-options $(DESTDIR)$(prefix)/
	$(INSTALL) -p -m755 $(HELPERS) $(DESTDIR)$(prefix)/
	$(MKDIR_P) -- $(DESTDIR)$(bindir)
	$(INSTALL) -p -m755 $(PROGRAMS) $(DESTDIR)$(bindir)/

clean: $(SUBDIRS)
	$(RM) -- $(TARGETS)

$(SUBDIRS):
	$(MAKE) $(MFLAGS) -C "$@" $(MAKECMDGOALS)
