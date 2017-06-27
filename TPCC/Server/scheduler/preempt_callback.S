.file	"preempt_callback.c"

.extern stm_validate
.extern control
.extern thread_tx

.text
.globl	preempt_callback
.type	preempt_callback, @function

preempt_callback:

	# save used REGISTER: rax
	pushq		%rax

	# save FLAGS
	lahf
	seto		%al
	pushq		%rax

	# mask delivery of further ticks
	movq		%fs:0, %rax
	addq		control@gottpoff(%rip), %rax
	movw		$0, (%rax)

	# save remaining caller-save REGISTERS
	pushq		%rcx
	pushq		%rdx
	pushq		%rsi
	pushq		%rdi
	pushq		%r8
	pushq		%r9
	pushq		%r10
	pushq		%r11
	subq		$16, %rsp
	movsd		%xmm0, (%rsp)
	subq		$16, %rsp
	movsd		%xmm1, (%rsp)
	subq		$16, %rsp
	movsd		%xmm2, (%rsp)
	subq		$16, %rsp
	movsd		%xmm3, (%rsp)
	subq		$16, %rsp
	movsd		%xmm4, (%rsp)
	subq		$16, %rsp
	movsd		%xmm5, (%rsp)
	subq		$16, %rsp
	movsd		%xmm6, (%rsp)
	subq		$16, %rsp
	movsd		%xmm7, (%rsp)

	# NUMBER OF TICKs
	lock incl	tick_count(%rip)

	# invoke STM validation service and check if valid...
	call		stm_validate_weak
	test		%rax, %rax
	jnz			still_valid

	# ...otherwise, rollback transaction
	movl		$7968, %edi # STM_ABORT_EARLY
	call		stm_abort

	# In this branch the function never returns, as stm_abort
	# executes a longjmp to the beginning of the transaction

still_valid:

	# restore remaining caller-save REGISTERS
	movsd		(%rsp), %xmm7
	addq		$16, %rsp
	movsd		(%rsp), %xmm6
	addq		$16, %rsp
	movsd		(%rsp), %xmm5
	addq		$16, %rsp
	movsd		(%rsp), %xmm4
	addq		$16, %rsp
	movsd		(%rsp), %xmm3
	addq		$16, %rsp
	movsd		(%rsp), %xmm2
	addq		$16, %rsp
	movsd		(%rsp), %xmm1
	addq		$16, %rsp
	movsd		(%rsp), %xmm0
	addq		$16, %rsp
	popq		%r11
	popq		%r10
	popq		%r9
	popq		%r8
	popq		%rdi
	popq		%rsi
	popq		%rdx
	popq		%rcx

	# unmask delivery of further ticks
	movq		%fs:0, %rax
	addq		control@gottpoff(%rip), %rax
	movw		$1, (%rax)

	# restore FLAGS
	popq		%rax
	addb		$0x7f, %al
	sahf

	# restore REGISTER: rax
	popq		%rax

	retq

.size	preempt_callback, .-preempt_callback