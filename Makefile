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
mklocal    =

ifdef MKLOCAL
prefix     = $(CURDIR)
bindir     = $(CURDIR)
sbindir    = $(CURDIR)
libexecdir = $(CURDIR)
mklocal    = 1
endif

CFLAGS = -Wall -Wextra -W -Wshadow -Wcast-align \
	-Wwrite-strings -Wconversion -Waggregate-return -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn \
	-Wmissing-format-attribute -Wredundant-decls -Wdisabled-optimization \
	-Wno-pointer-arith \
	-Os

CP = cp -a
INSTALL = install
MKDIR_P = mkdir -p
TOUCH_R = touch -r
HELP2MAN = env -i help2man -N
MAKEINFO_FLAGS = -D "VERSION $(VERSION)"

PROGRAMS    = vm
DATA        = \
    vm-options
HELPERS     = \
    vm-sh-config \
    vm-command-debug \
    vm-command-help \
    vm-command-init \
    vm-command-kern \
    vm-command-list \
    vm-command-run \
    vm-command-sandbox \
    vm-command-setup
BIN_HELPERS = init
TARGETS     = $(PROGRAMS) $(HELPERS) $(BIN_HELPERS)
SUBDIRS     =

KERNEL_CONFIGS = \
	kernel.config \
	kernel.config.balloon.virtio \
	kernel.config.console.serial \
	kernel.config.console.virtio \
	kernel.config.rnd.virtio

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
		-e 's,@MKLOCAL@,$(mklocal),g' \
		<$< >$@
	$(TOUCH_R) $< $@
	chmod --reference=$< $@

init: guest/init.c
	$(CC) -static $(CFLAGS) -o $@ $<

#init: guest/init.S
#	$(CC) -s -nostdlib -o $@ $<

install: $(TARGETS)
	$(MKDIR_P) -- $(DESTDIR)$(libexecdir)
	$(INSTALL) -p -m755 $(BIN_HELPERS) $(DESTDIR)$(libexecdir)/
	$(INSTALL) -p -m644 $(KERNEL_CONFIGS) $(DESTDIR)$(libexecdir)/
	$(MKDIR_P) -- $(DESTDIR)$(prefix)
	$(INSTALL) -p -m644 $(DATA) $(DESTDIR)$(prefix)/
	$(INSTALL) -p -m755 $(HELPERS) $(DESTDIR)$(prefix)/
	$(MKDIR_P) -- $(DESTDIR)$(bindir)
	$(INSTALL) -p -m755 $(PROGRAMS) $(DESTDIR)$(bindir)/

clean: $(SUBDIRS)
	$(RM) -- $(TARGETS)

$(SUBDIRS):
	$(MAKE) $(MFLAGS) -C "$@" $(MAKECMDGOALS)
