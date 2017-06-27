.file	"stm_et_callback.S"

.text
.globl stm_revalidate
.type  stm_revalidate, @function

stm_revalidate:

	# Save used REGISTER: rax
	pushq		%rax

	# Save FLAGS
	lahf
	seto		%al
	pushq		%rax

	# Mask delivery of further ticks by setting `tx_on` to 0
	movq		%fs:0, %rax
	addq		stm_control_buffer@gottpoff(%rip), %rax
	movw		$0, (%rax)

	# Save remaining caller-save REGISTERS
	pushq		%rcx
	pushq		%rdx
	pushq		%rsi
	pushq		%rdi
	pushq		%r8
	pushq		%r9
	pushq		%r10
	pushq		%r11
	// subq		$16, %rsp
	// movsd		%xmm0, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm1, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm2, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm3, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm4, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm5, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm6, (%rsp)
	// subq		$16, %rsp
	// movsd		%xmm7, (%rsp)
	movq		%fs:0, %rax
	addq		et_fxregs@gottpoff(%rip), %rax
	// fxsave wants 16-byte aligned memory
	fxsave	(%rax)

	# Try to extend the current snapshot...
	movq		%fs:0, %rax
	addq		thread_tx@gottpoff(%rip), %rax
	movq		(%rax), %rdi

	call		stm_extend
	test		%rax, %rax
	jnz			still_valid

	# ...otherwise, rollback tx
	movq		$7968, %rdi # STM_ABORT_EARLY
	call		stm_abort

	# In this branch the function never returns, as stm_abort
	# executes a longjmp to the beginning of the transaction

still_valid:

	# Restore remaining caller-save REGISTERS
	movq		%fs:0, %rax
	addq		et_fxregs@gottpoff(%rip), %rax
	// fxrstor wants 16-byte aligned memory
	fxrstor	(%rax)
	// movsd		(%rsp), %xmm7
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm6
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm5
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm4
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm3
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm2
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm1
	// addq		$16, %rsp
	// movsd		(%rsp), %xmm0
	// addq		$16, %rsp
	popq		%r11
	popq		%r10
	popq		%r9
	popq		%r8
	popq		%rdi
	popq		%rsi
	popq		%rdx
	popq		%rcx

	# Unmask delivery of further ticks by setting `tx_on` to 1
	movq		%fs:0, %rax
	addq		stm_control_buffer@gottpoff(%rip), %rax
	movw		$1, (%rax)

	# Restore FLAGS
	popq		%rax
	addb		$0x7f, %al
	sahf

	# Restore REGISTER: rax
	popq		%rax

	retq

.size stm_revalidate, .-stm_revalidate
