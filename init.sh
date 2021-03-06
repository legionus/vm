#!/bin/sh -efu

busybox_links()
{
	[ -x /virt/bin/busybox ] ||
		return 0
	/virt/bin/busybox --list |
	while read -r name; do
		[ -e "/virt/bin/$name" ] ||
			/virt/bin/busybox ln -s -- busybox "/virt/bin/$name"
	done
}

loadmodules()
{
	[ -s "$1" ] ||
		return 0
	while read -r modpath; do
		modprobe "$modpath"
	done < "$1"
}

setenv()
{
	if [ ! -s "$1" ]; then
		export 'PATH=/sbin:/usr/sbin:/usr/local/sbin:/bin:/usr/bin:/usr/local/bin:/virt/bin'
		export 'TERM=linux'
		export 'HOME=/virt/home'
		export 'PS1=[shell]# '
		return 0
	fi
	while IFS="$'\n'" read -r s; do
		export "$s"
	done < "$1"
}

do_mount()
{
	[ -d "$1" ] && ! mountpoint -q "$1" ||
		return 0
	local d="$1"; shift
	mount "$@" "$d"
}

ttysz()
{
	local esc cols rows
	echo -ne "\e[s\e[1000;1000H\e[6n\e[u"
	IFS=';[' read -s -t2 -dR esc rows cols ||
		echo >&2 'ttysz() FAILED'
	stty rows $rows cols $cols
}

trap : CHLD INT STOP QUIT

busybox_links
loadmodules /virt/etc/modules
setenv /virt/etc/environ
ttysz ||:

do_mount /host -t 9p -o ro,trans=virtio,version=9p2000.L hostfs
do_mount /proc -t proc proc
do_mount /sys -t sysfs sysfs
do_mount /dev -t devtmpfs devtmpfs
do_mount /dev/pts -t devpts devpts
do_mount /tmp -t tmpfs tmpfs

prog=/virt/sandbox.sh

[ -x "$prog" ] ||
	prog=/bin/bash

setsid -c "$prog"

for n in i s b; do
	echo $n > /proc/sysrq-trigger
done
