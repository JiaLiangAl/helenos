/*
 * Copyright (C) 2003-2004 Jakub Jermar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <arch.h>
#include <arch/cp0.h>
#include <arch/exception.h>
#include <arch/asm.h>
#include <mm/as.h>

#include <userspace.h>
#include <arch/console.h>
#include <memstr.h>
#include <proc/thread.h>
#include <print.h>

#include <arch/interrupt.h>
#include <arch/drivers/arc.h>
#include <console/chardev.h>
#include <arch/debugger.h>

#include <arch/asm/regname.h>

/* Size of the code jumping to the exception handler code 
 * - J+NOP 
 */
#define EXCEPTION_JUMP_SIZE    8

#define TLB_EXC ((char *) 0x80000000)
#define NORM_EXC ((char *) 0x80000180)
#define CACHE_EXC ((char *) 0x80000100)

void arch_pre_mm_init(void)
{
	/* It is not assumed by default */
	interrupts_disable();
	
	/* Initialize dispatch table */
	exception_init();
	interrupt_init();

	arc_init();

	/* Copy the exception vectors to the right places */
	memcpy(TLB_EXC, (char *)tlb_refill_entry, EXCEPTION_JUMP_SIZE);
	memcpy(NORM_EXC, (char *)exception_entry, EXCEPTION_JUMP_SIZE);
	memcpy(CACHE_EXC, (char *)cache_error_entry, EXCEPTION_JUMP_SIZE);

	/*
	 * Switch to BEV normal level so that exception vectors point to the kernel.
	 * Clear the error level.
	 */
	cp0_status_write(cp0_status_read() & ~(cp0_status_bev_bootstrap_bit|cp0_status_erl_error_bit));

	/* 
	 * Mask all interrupts 
	 */
	cp0_mask_all_int();
	/*
	 * Unmask hardware clock interrupt.
	 */
	cp0_unmask_int(TIMER_IRQ);

	/*
	 * Start hardware clock.
	 */
	cp0_compare_write(cp0_compare_value + cp0_count_read());

	console_init();
	debugger_init();
	/* Setup usermode...*/
	config.init_addr = INIT_ADDRESS;
	config.init_size = INIT_SIZE;
}

void arch_post_mm_init(void)
{
}

void arch_pre_smp_init(void)
{
}

void arch_post_smp_init(void)
{
}

/* Stack pointer saved when entering user mode */
/* TODO: How do we do it on SMP system???? */

/* Why the linker moves the variable 64K away in assembler
 * when not in .text section ????????
 */
__address supervisor_sp __attribute__ ((section (".text")));

void userspace(void)
{
	/* EXL=1, UM=1, IE=1 */
	cp0_status_write(cp0_status_read() | (cp0_status_exl_exception_bit |
					      cp0_status_um_bit |
					      cp0_status_ie_enabled_bit));
	cp0_epc_write(UTEXT_ADDRESS);
	userspace_asm(USTACK_ADDRESS+PAGE_SIZE);
	while (1)
		;
}

void before_thread_runs_arch(void)
{
	supervisor_sp = (__address) &THREAD->kstack[THREAD_STACK_SIZE-SP_DELTA];
}

void after_thread_ran_arch(void)
{
}
