/*
 * Copyright (c) 2007 Jakub Jermar
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

/** @addtogroup fs
 * @{
 */ 

/**
 * @file	vfs_node.c
 * @brief	Various operations on VFS nodes have their home in this file.
 */

#include "vfs.h"

/** Increment reference count of a VFS node.
 *
 * @param node		VFS node that will have its refcnt incremented.
 */
void vfs_node_addref(vfs_node_t *node)
{
	/* TODO */
}

/** Decrement reference count of a VFS node.
 *
 * This function handles the case when the reference count drops to zero.
 *
 * @param node		VFS node that will have its refcnt decremented.
 */
void vfs_node_delref(vfs_node_t *node)
{
	/* TODO */
}

/** Find VFS node.
 *
 * This function will try to lookup the given triplet in the VFS node hash
 * table. In case the triplet is not found there, a new VFS node is created.
 * In any case, the VFS node will have its reference count incremented. Every
 * node returned by this call should be eventually put back by calling
 * vfs_node_put() on it.
 *
 * @param triplet	Triplet encoding the identity of the VFS node.
 *
 * @return		VFS node corresponding to the given triplet.
 */
vfs_node_t *vfs_node_get(vfs_triplet_t *triplet)
{
	/* TODO */
	return NULL;
}

/** Return VFS node when no longer needed by the caller.
 *
 * This function will remove the reference on the VFS node created by
 * vfs_node_get(). This function can only be called as a closing bracket to the
 * preceding vfs_node_get() call.
 *
 * @param node		VFS node being released.
 */
void vfs_node_put(vfs_node_t *node)
{
	vfs_node_delref(node);
}

/**
 * @}
 */ 
