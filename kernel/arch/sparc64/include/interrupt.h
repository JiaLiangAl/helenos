/*
 * Copyright (c) 2005 Jakub Jermar
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

/** @addtogroup sparc64interrupt sparc64
 * @ingroup interrupt
 * @{
 */
/** @file
 */

#ifndef KERN_sparc64_INTERRUPT_H_
#define KERN_sparc64_INTERRUPT_H_

#include <typedefs.h>
#include <arch/regdef.h>

#define IVT_ITEMS 	15
#define IVT_FIRST	1

/* This needs to be defined for inter-architecture API portability. */
#define VECTOR_TLB_SHOOTDOWN_IPI	0

enum {
	IPI_TLB_SHOOTDOWN = VECTOR_TLB_SHOOTDOWN_IPI
};		

typedef struct istate {
	uint64_t	tnpc;
	uint64_t	tpc;
	uint64_t	tstate;
} istate_t;

static inline void istate_set_retaddr(istate_t *istate, uintptr_t retaddr)
{
	istate->tpc = retaddr;
}

static inline int istate_from_uspace(istate_t *istate)
{
	return !(istate->tstate & TSTATE_PRIV_BIT);
}

static inline unative_t istate_get_pc(istate_t *istate)
{
	return istate->tpc;
}

static inline unative_t istate_get_fp(istate_t *istate)
{
	return 0;	/* TODO */
}

extern void decode_istate(istate_t *);

#endif

/** @}
 */
