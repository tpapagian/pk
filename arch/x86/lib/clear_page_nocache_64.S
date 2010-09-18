#include <linux/linkage.h>
#include <asm/dwarf2.h>

ENTRY(clear_page_nocache)
	CFI_STARTPROC
	xorq	%rax, %rax
	movl	$4096/64, %ecx
	.p2align 4
1:
	decl	%ecx
	movnti	%rax,(%rdi)
	movnti  %rax,0x8(%rdi)
	movnti  %rax,0x10(%rdi)
	movnti  %rax,0x18(%rdi)
	movnti  %rax,0x20(%rdi)
	movnti  %rax,0x28(%rdi)
	movnti  %rax,0x30(%rdi)
	movnti  %rax,0x38(%rdi)
	leaq	64(%rdi),%rdi
	jnz	1b
	nop
	ret
	CFI_ENDPROC
END (clear_page_nocache)
