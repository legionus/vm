# Author: Oleg Nesterov <oleg@redhat.com>
# gcc -s -nostdlib init.S -o init

.data

.m_dev:
.string "hostfs"
.m_dir:
.string "/host"
.m_typ:
.string "9p"
.m_opt:
.string "version=9p2000.L,trans=virtio,access=any"

.e_nam:
.string "/virt/init.sh"

.text
.globl _start
_start:

	mov $165, %rax          # __NR_mount
	mov $.m_dev, %rdi
	mov $.m_dir, %rsi
	mov $.m_typ, %rdx
	mov $1, %r10            # MS_RDONLY
	mov $.m_opt, %r8
	syscall

	mov $112, %rax          # __NR_setsid
	syscall

	mov $59, %rax           # __NR_execve
	mov $.e_nam, %rdi
	lea 8(%rsp), %rsi       # argv[]
	mov %rdi, (%rsi)        # change argv[0]
	pop %rcx                # argc
	inc %rcx
	lea (%rsi,%rcx,8), %rdx # envp[]
	syscall

	mov $60, %rax           # __NR_exit
	mov $1, %rdi
	syscall                 # panic
