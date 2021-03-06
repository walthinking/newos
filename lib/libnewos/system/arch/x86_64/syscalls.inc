/*
** Copyright 2001-2004, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

/*
** syscall interface works as such:
** rax has syscall #
** rdi,rsi,rdx,r10,r8,r9 contain the first 6 args
** r11 has number of args on stack
** rcx is trashed by the syscall instruction
** rbx,r12-r15 are untouched (callee-saved)
*/

#define SYSCALL0(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	syscall; \
	ret

#define SYSCALL1(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	syscall; \
	ret

#define SYSCALL2(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL3(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL4(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL5(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL6(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$0,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL7(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$1,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL8(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$2,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL9(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$3,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL10(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$4,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL11(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$5,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL12(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$6,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL13(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$7,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL14(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$8,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

#define SYSCALL15(name, n) \
.globl name; \
.type name,@function; \
.align 8; \
name: \
	mov		$n,%ax; \
	mov		$9,%r11b; \
	mov		%rcx,%r10; \
	syscall; \
	ret

