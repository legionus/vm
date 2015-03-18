PROJECT = vm
VERSION = 0.0.1

sysconfdir ?= /etc
bootdir    ?= /boot
bindir     ?= /usr/bin
sbindir    ?= /usr/sbin
datadir    ?= /usr/share
infodir    ?= $(datadir)/info
mandir     ?= $(datadir)/man
man1dir    ?= $(mandir)/man1
tmpdir     ?= /tmp
prefix     ?= $(datadir)/$(PROJECT)
DESTDIR    ?=

ifdef MKLOCAL
prefix  = $(CURDIR)
bindir  = $(CURDIR)
sbindir = $(CURDIR)
endif

CP = cp -a
INSTALL = install
MKDIR_P = mkdir -p
TOUCH_R = touch -r
HELP2MAN = env -i help2man -N
MAKEINFO_FLAGS = -D "VERSION $(VERSION)"

TARGETS = vm
SUBDIRS =

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
		<$< >$@
	$(TOUCH_R) $< $@
	chmod --reference=$< $@

clean: $(SUBDIRS)
	$(RM) -- $(TARGETS)

$(SUBDIRS):
	$(MAKE) $(MFLAGS) -C "$@" $(MAKECMDGOALS)
