	.syntax unified
	.arm
	.text
	.globl _start
	.type  _start, %function
_start:
	ldr	r0, [sp]		/* argc */
	add	r1, sp, #4		/* argv */
	bic	sp, sp, #7		/* AAPCS wants 8-byte alignment */
	bl	main
	mov	r7, #1			/* SYS_exit */
	svc	#0
1:	b	1b
	.size _start, .-_start

/* libgcc's __aeabi_ldiv0 calls raise() when a divide by zero happens. There is
 * no libc here and the code never divides by a runtime-zero, so stub it out
 * rather than drag in the whole signal path. */
	.globl raise
	.type  raise, %function
raise:
	bx	lr
	.size raise, .-raise

	.section .note.GNU-stack,"",%progbits
