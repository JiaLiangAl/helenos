/*
 * Copyright (c) 2006 Ondrej Palkovsky
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

/** @addtogroup libc
 * @{
 */
/** @file
 */

/**
 * Asynchronous library
 *
 * The aim of this library is to provide a facility for writing programs which
 * utilize the asynchronous nature of HelenOS IPC, yet using a normal way of
 * programming.
 *
 * You should be able to write very simple multithreaded programs. The async
 * framework will automatically take care of most of the synchronization
 * problems.
 *
 * Example of use (pseudo C):
 *
 * 1) Multithreaded client application
 *
 *   fibril_create(fibril1, ...);
 *   fibril_create(fibril2, ...);
 *   ...
 *
 *   int fibril1(void *arg)
 *   {
 *     conn = async_connect_me_to(...);
 *
 *     exch = async_exchange_begin(conn);
 *     c1 = async_send(exch);
 *     async_exchange_end(exch);
 *
 *     exch = async_exchange_begin(conn);
 *     c2 = async_send(exch);
 *     async_exchange_end(exch);
 *
 *     async_wait_for(c1);
 *     async_wait_for(c2);
 *     ...
 *   }
 *
 *
 * 2) Multithreaded server application
 *
 *   main()
 *   {
 *     async_manager();
 *   }
 *
 *   port_handler(ipc_call_t *icall)
 *   {
 *     if (want_refuse) {
 *       async_answer_0(icall, ELIMIT);
 *       return;
 *     }
 *
 *     async_answer_0(icall, EOK);
 *
 *     async_get_call(&call);
 *     somehow_handle_the_call(&call);
 *     async_answer_2(&call, 1, 2, 3);
 *
 *     async_get_call(&call);
 *     ...
 *   }
 *
 */

#define LIBC_ASYNC_C_
#include <ipc/ipc.h>
#include <async.h>
#include "../private/async.h"
#undef LIBC_ASYNC_C_

#include <ipc/irq.h>
#include <ipc/event.h>
#include <fibril.h>
#include <adt/hash_table.h>
#include <adt/hash.h>
#include <adt/list.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <mem.h>
#include <stdlib.h>
#include <macros.h>
#include <str_error.h>
#include <as.h>
#include <abi/mm/as.h>
#include "../private/libc.h"
#include "../private/fibril.h"

#define DPRINTF(...)  ((void) 0)

/* Client connection data */
typedef struct {
	ht_link_t link;

	task_id_t in_task_id;
	int refcnt;
	void *data;
} client_t;

/* Server connection data */
typedef struct {
	/** Fibril handling the connection. */
	fid_t fid;

	/** Hash table link. */
	ht_link_t link;

	/** Incoming client task ID. */
	task_id_t in_task_id;

	/** Incoming phone hash. */
	sysarg_t in_phone_hash;

	/** Link to the client tracking structure. */
	client_t *client;

	/** Channel for messages that should be delivered to this fibril. */
	mpsc_t *msg_channel;

	/** Call data of the opening call. */
	ipc_call_t call;

	/** Fibril function that will be used to handle the connection. */
	async_port_handler_t handler;

	/** Client data */
	void *data;
} connection_t;

/* Member of notification_t::msg_list. */
typedef struct {
	link_t link;
	ipc_call_t calldata;
} notification_msg_t;

/* Notification data */
typedef struct {
	/** notification_hash_table link */
	ht_link_t htlink;

	/** notification_queue link */
	link_t qlink;

	/** Notification method */
	sysarg_t imethod;

	/** Notification handler */
	async_notification_handler_t handler;

	/** Notification handler argument */
	void *arg;

	/** List of arrived notifications. */
	list_t msg_list;
} notification_t;

/** Identifier of the incoming connection handled by the current fibril. */
static fibril_local connection_t *fibril_connection;

static void *default_client_data_constructor(void)
{
	return NULL;
}

static void default_client_data_destructor(void *data)
{
}

static async_client_data_ctor_t async_client_data_create =
    default_client_data_constructor;
static async_client_data_dtor_t async_client_data_destroy =
    default_client_data_destructor;

void async_set_client_data_constructor(async_client_data_ctor_t ctor)
{
	assert(async_client_data_create == default_client_data_constructor);
	async_client_data_create = ctor;
}

void async_set_client_data_destructor(async_client_data_dtor_t dtor)
{
	assert(async_client_data_destroy == default_client_data_destructor);
	async_client_data_destroy = dtor;
}

static FIBRIL_RMUTEX_INITIALIZE(client_mutex);
static hash_table_t client_hash_table;

// TODO: lockfree notification_queue?
static FIBRIL_RMUTEX_INITIALIZE(notification_mutex);
static hash_table_t notification_hash_table;
static LIST_INITIALIZE(notification_queue);
static FIBRIL_SEMAPHORE_INITIALIZE(notification_semaphore, 0);

static LIST_INITIALIZE(notification_freelist);
static long notification_freelist_total = 0;
static long notification_freelist_used = 0;

static sysarg_t notification_avail = 0;

static FIBRIL_RMUTEX_INITIALIZE(conn_mutex);
static hash_table_t conn_hash_table;

static size_t client_key_hash(void *key)
{
	task_id_t in_task_id = *(task_id_t *) key;
	return in_task_id;
}

static size_t client_hash(const ht_link_t *item)
{
	client_t *client = hash_table_get_inst(item, client_t, link);
	return client_key_hash(&client->in_task_id);
}

static bool client_key_equal(void *key, const ht_link_t *item)
{
	task_id_t in_task_id = *(task_id_t *) key;
	client_t *client = hash_table_get_inst(item, client_t, link);
	return in_task_id == client->in_task_id;
}

/** Operations for the client hash table. */
static hash_table_ops_t client_hash_table_ops = {
	.hash = client_hash,
	.key_hash = client_key_hash,
	.key_equal = client_key_equal,
	.equal = NULL,
	.remove_callback = NULL
};

typedef struct {
	task_id_t task_id;
	sysarg_t phone_hash;
} conn_key_t;

/** Compute hash into the connection hash table
 *
 * The hash is based on the source task ID and the source phone hash. The task
 * ID is included in the hash because a phone hash alone might not be unique
 * while we still track connections for killed tasks due to kernel's recycling
 * of phone structures.
 *
 * @param key Pointer to the connection key structure.
 *
 * @return Index into the connection hash table.
 *
 */
static size_t conn_key_hash(void *key)
{
	conn_key_t *ck = (conn_key_t *) key;

	size_t hash = 0;
	hash = hash_combine(hash, LOWER32(ck->task_id));
	hash = hash_combine(hash, UPPER32(ck->task_id));
	hash = hash_combine(hash, ck->phone_hash);
	return hash;
}

static size_t conn_hash(const ht_link_t *item)
{
	connection_t *conn = hash_table_get_inst(item, connection_t, link);
	return conn_key_hash(&(conn_key_t){
		.task_id = conn->in_task_id,
		.phone_hash = conn->in_phone_hash
	});
}

static bool conn_key_equal(void *key, const ht_link_t *item)
{
	conn_key_t *ck = (conn_key_t *) key;
	connection_t *conn = hash_table_get_inst(item, connection_t, link);
	return ((ck->task_id == conn->in_task_id) &&
	    (ck->phone_hash == conn->in_phone_hash));
}

/** Operations for the connection hash table. */
static hash_table_ops_t conn_hash_table_ops = {
	.hash = conn_hash,
	.key_hash = conn_key_hash,
	.key_equal = conn_key_equal,
	.equal = NULL,
	.remove_callback = NULL
};

static client_t *async_client_get(task_id_t client_id, bool create)
{
	client_t *client = NULL;

	fibril_rmutex_lock(&client_mutex);
	ht_link_t *link = hash_table_find(&client_hash_table, &client_id);
	if (link) {
		client = hash_table_get_inst(link, client_t, link);
		client->refcnt++;
	} else if (create) {
		// TODO: move the malloc out of critical section
		/* malloc() is rmutex safe. */
		client = malloc(sizeof(client_t));
		if (client) {
			client->in_task_id = client_id;
			client->data = async_client_data_create();

			client->refcnt = 1;
			hash_table_insert(&client_hash_table, &client->link);
		}
	}

	fibril_rmutex_unlock(&client_mutex);
	return client;
}

static void async_client_put(client_t *client)
{
	bool destroy;

	fibril_rmutex_lock(&client_mutex);

	if (--client->refcnt == 0) {
		hash_table_remove(&client_hash_table, &client->in_task_id);
		destroy = true;
	} else
		destroy = false;

	fibril_rmutex_unlock(&client_mutex);

	if (destroy) {
		if (client->data)
			async_client_data_destroy(client->data);

		free(client);
	}
}

/** Wrapper for client connection fibril.
 *
 * When a new connection arrives, a fibril with this implementing
 * function is created.
 *
 * @param arg Connection structure pointer.
 *
 * @return Always zero.
 *
 */
static errno_t connection_fibril(void *arg)
{
	assert(arg);

	/*
	 * Setup fibril-local connection pointer.
	 */
	fibril_connection = (connection_t *) arg;

	/*
	 * Add our reference for the current connection in the client task
	 * tracking structure. If this is the first reference, create and
	 * hash in a new tracking structure.
	 */

	client_t *client = async_client_get(fibril_connection->in_task_id, true);
	if (!client) {
		ipc_answer_0(fibril_connection->call.cap_handle, ENOMEM);
		return 0;
	}

	fibril_connection->client = client;

	/*
	 * Call the connection handler function.
	 */
	fibril_connection->handler(&fibril_connection->call,
	    fibril_connection->data);

	/*
	 * Remove the reference for this client task connection.
	 */
	async_client_put(client);

	fibril_rmutex_lock(&conn_mutex);

	/*
	 * Remove myself from the connection hash table.
	 */
	hash_table_remove(&conn_hash_table, &(conn_key_t){
		.task_id = fibril_connection->in_task_id,
		.phone_hash = fibril_connection->in_phone_hash
	});

	/*
	 * Close the channel, if it isn't closed already.
	 */
	mpsc_t *c = fibril_connection->msg_channel;
	mpsc_close(c);

	fibril_rmutex_unlock(&conn_mutex);

	/*
	 * Answer all remaining messages with EHANGUP.
	 */
	ipc_call_t call;
	while (mpsc_receive(c, &call, NULL) == EOK)
		ipc_answer_0(call.cap_handle, EHANGUP);

	/*
	 * Clean up memory.
	 */
	mpsc_destroy(c);
	free(fibril_connection);
	return EOK;
}

/** Create a new fibril for a new connection.
 *
 * Create new fibril for connection, fill in connection structures and insert it
 * into the hash table, so that later we can easily do routing of messages to
 * particular fibrils.
 *
 * @param in_task_id     Identification of the incoming connection.
 * @param in_phone_hash  Identification of the incoming connection.
 * @param call           Call data of the opening call. If call is NULL,
 *                       the connection was opened by accepting the
 *                       IPC_M_CONNECT_TO_ME call and this function is
 *                       called directly by the server.
 * @param handler        Connection handler.
 * @param data           Client argument to pass to the connection handler.
 *
 * @return  New fibril id or NULL on failure.
 *
 */
static fid_t async_new_connection(task_id_t in_task_id, sysarg_t in_phone_hash,
    ipc_call_t *call, async_port_handler_t handler, void *data)
{
	connection_t *conn = malloc(sizeof(*conn));
	if (!conn) {
		if (call)
			ipc_answer_0(call->cap_handle, ENOMEM);

		return (fid_t) NULL;
	}

	conn->in_task_id = in_task_id;
	conn->in_phone_hash = in_phone_hash;
	conn->msg_channel = mpsc_create(sizeof(ipc_call_t));
	conn->handler = handler;
	conn->data = data;

	if (call)
		conn->call = *call;
	else
		conn->call.cap_handle = CAP_NIL;

	/* We will activate the fibril ASAP */
	conn->fid = fibril_create(connection_fibril, conn);

	if (conn->fid == 0) {
		mpsc_destroy(conn->msg_channel);
		free(conn);

		if (call)
			ipc_answer_0(call->cap_handle, ENOMEM);

		return (fid_t) NULL;
	}

	/* Add connection to the connection hash table */

	fibril_rmutex_lock(&conn_mutex);
	hash_table_insert(&conn_hash_table, &conn->link);
	fibril_rmutex_unlock(&conn_mutex);

	fibril_start(conn->fid);

	return conn->fid;
}

/** Wrapper for making IPC_M_CONNECT_TO_ME calls using the async framework.
 *
 * Ask through phone for a new connection to some service.
 *
 * @param exch    Exchange for sending the message.
 * @param iface   Callback interface.
 * @param arg1    User defined argument.
 * @param arg2    User defined argument.
 * @param handler Callback handler.
 * @param data    Handler data.
 * @param port_id ID of the newly created port.
 *
 * @return Zero on success or an error code.
 *
 */
errno_t async_create_callback_port(async_exch_t *exch, iface_t iface, sysarg_t arg1,
    sysarg_t arg2, async_port_handler_t handler, void *data, port_id_t *port_id)
{
	if ((iface & IFACE_MOD_CALLBACK) != IFACE_MOD_CALLBACK)
		return EINVAL;

	if (exch == NULL)
		return ENOENT;

	ipc_call_t answer;
	aid_t req = async_send_3(exch, IPC_M_CONNECT_TO_ME, iface, arg1, arg2,
	    &answer);

	errno_t rc;
	async_wait_for(req, &rc);
	if (rc != EOK)
		return rc;

	rc = async_create_port_internal(iface, handler, data, port_id);
	if (rc != EOK)
		return rc;

	sysarg_t phone_hash = IPC_GET_ARG5(answer);
	fid_t fid = async_new_connection(answer.in_task_id, phone_hash,
	    NULL, handler, data);
	if (fid == (fid_t) NULL)
		return ENOMEM;

	return EOK;
}

static size_t notification_key_hash(void *key)
{
	sysarg_t id = *(sysarg_t *) key;
	return id;
}

static size_t notification_hash(const ht_link_t *item)
{
	notification_t *notification =
	    hash_table_get_inst(item, notification_t, htlink);
	return notification_key_hash(&notification->imethod);
}

static bool notification_key_equal(void *key, const ht_link_t *item)
{
	sysarg_t id = *(sysarg_t *) key;
	notification_t *notification =
	    hash_table_get_inst(item, notification_t, htlink);
	return id == notification->imethod;
}

/** Operations for the notification hash table. */
static hash_table_ops_t notification_hash_table_ops = {
	.hash = notification_hash,
	.key_hash = notification_key_hash,
	.key_equal = notification_key_equal,
	.equal = NULL,
	.remove_callback = NULL
};

/** Try to route a call to an appropriate connection fibril.
 *
 * If the proper connection fibril is found, a message with the call is added to
 * its message queue. If the fibril was not active, it is activated and all
 * timeouts are unregistered.
 *
 * @param call Data of the incoming call.
 *
 * @return EOK if the call was successfully passed to the respective fibril.
 * @return ENOENT if the call doesn't match any connection.
 * @return Other error code if routing failed for other reasons.
 *
 */
static errno_t route_call(ipc_call_t *call)
{
	assert(call);

	fibril_rmutex_lock(&conn_mutex);

	ht_link_t *link = hash_table_find(&conn_hash_table, &(conn_key_t){
		.task_id = call->in_task_id,
		.phone_hash = call->in_phone_hash
	});
	if (!link) {
		fibril_rmutex_unlock(&conn_mutex);
		return ENOENT;
	}

	connection_t *conn = hash_table_get_inst(link, connection_t, link);

	errno_t rc = mpsc_send(conn->msg_channel, call);

	if (IPC_GET_IMETHOD(*call) == IPC_M_PHONE_HUNGUP) {
		/* Close the channel, but let the connection fibril answer. */
		mpsc_close(conn->msg_channel);
		// FIXME: Ideally, we should be able to discard/answer the
		//        hungup message here and just close the channel without
		//        passing it out. Unfortunatelly, somehow that breaks
		//        handling of CPU exceptions.
	}

	fibril_rmutex_unlock(&conn_mutex);
	return rc;
}

/** Function implementing the notification handler fibril. Never returns. */
static errno_t notification_fibril_func(void *arg)
{
	(void) arg;

	while (true) {
		fibril_semaphore_down(&notification_semaphore);

		fibril_rmutex_lock(&notification_mutex);

		/*
		 * The semaphore ensures that if we get this far,
		 * the queue must be non-empty.
		 */
		assert(!list_empty(&notification_queue));

		notification_t *notification = list_get_instance(
		    list_first(&notification_queue), notification_t, qlink);

		async_notification_handler_t handler = notification->handler;
		void *arg = notification->arg;

		notification_msg_t *m = list_pop(&notification->msg_list,
		    notification_msg_t, link);
		assert(m);
		ipc_call_t calldata = m->calldata;

		notification_freelist_used--;

		if (notification_freelist_total > 64 &&
		    notification_freelist_total > 2 * notification_freelist_used) {
			/* Going to free the structure if we have too much. */
			notification_freelist_total--;
		} else {
			/* Otherwise add to freelist. */
			list_append(&m->link, &notification_freelist);
			m = NULL;
		}

		if (list_empty(&notification->msg_list))
			list_remove(&notification->qlink);

		fibril_rmutex_unlock(&notification_mutex);

		if (handler)
			handler(&calldata, arg);

		free(m);
	}

	/* Not reached. */
	return EOK;
}

/**
 * Creates a new dedicated fibril for handling notifications.
 * By default, there is one such fibril. This function can be used to
 * create more in order to increase the number of notification that can
 * be processed concurrently.
 *
 * Currently, there is no way to destroy those fibrils after they are created.
 */
errno_t async_spawn_notification_handler(void)
{
	fid_t f = fibril_create(notification_fibril_func, NULL);
	if (f == 0)
		return ENOMEM;

	fibril_add_ready(f);
	return EOK;
}

/** Queue notification.
 *
 * @param call   Data of the incoming call.
 *
 */
static void queue_notification(ipc_call_t *call)
{
	assert(call);

	fibril_rmutex_lock(&notification_mutex);

	notification_msg_t *m = list_pop(&notification_freelist,
	    notification_msg_t, link);

	if (!m) {
		fibril_rmutex_unlock(&notification_mutex);
		m = malloc(sizeof(notification_msg_t));
		if (!m) {
			DPRINTF("Out of memory.\n");
			abort();
		}

		fibril_rmutex_lock(&notification_mutex);
		notification_freelist_total++;
	}

	ht_link_t *link = hash_table_find(&notification_hash_table,
	    &IPC_GET_IMETHOD(*call));
	if (!link) {
		/* Invalid notification. */
		// TODO: Make sure this can't happen and turn it into assert.
		notification_freelist_total--;
		fibril_rmutex_unlock(&notification_mutex);
		free(m);
		return;
	}

	notification_t *notification =
	    hash_table_get_inst(link, notification_t, htlink);

	notification_freelist_used++;
	m->calldata = *call;
	list_append(&m->link, &notification->msg_list);

	if (!link_in_use(&notification->qlink))
		list_append(&notification->qlink, &notification_queue);

	fibril_rmutex_unlock(&notification_mutex);

	fibril_semaphore_up(&notification_semaphore);
}

/**
 * Creates a new notification structure and inserts it into the hash table.
 *
 * @param handler  Function to call when notification is received.
 * @param arg      Argument for the handler function.
 * @return         The newly created notification structure.
 */
static notification_t *notification_create(async_notification_handler_t handler, void *arg)
{
	notification_t *notification = calloc(1, sizeof(notification_t));
	if (!notification)
		return NULL;

	notification->handler = handler;
	notification->arg = arg;

	list_initialize(&notification->msg_list);

	fid_t fib = 0;

	fibril_rmutex_lock(&notification_mutex);

	if (notification_avail == 0) {
		/* Attempt to create the first handler fibril. */
		fib = fibril_create(notification_fibril_func, NULL);
		if (fib == 0) {
			fibril_rmutex_unlock(&notification_mutex);
			free(notification);
			return NULL;
		}
	}

	sysarg_t imethod = notification_avail;
	notification_avail++;

	notification->imethod = imethod;
	hash_table_insert(&notification_hash_table, &notification->htlink);

	fibril_rmutex_unlock(&notification_mutex);

	if (imethod == 0) {
		assert(fib);
		fibril_add_ready(fib);
	}

	return notification;
}

/** Subscribe to IRQ notification.
 *
 * @param inr     IRQ number.
 * @param handler Notification handler.
 * @param data    Notification handler client data.
 * @param ucode   Top-half pseudocode handler.
 *
 * @param[out] handle  IRQ capability handle on success.
 *
 * @return An error code.
 *
 */
errno_t async_irq_subscribe(int inr, async_notification_handler_t handler,
    void *data, const irq_code_t *ucode, cap_irq_handle_t *handle)
{
	notification_t *notification = notification_create(handler, data);
	if (!notification)
		return ENOMEM;

	cap_irq_handle_t ihandle;
	errno_t rc = ipc_irq_subscribe(inr, notification->imethod, ucode,
	    &ihandle);
	if (rc == EOK && handle != NULL) {
		*handle = ihandle;
	}
	return rc;
}

/** Unsubscribe from IRQ notification.
 *
 * @param handle  IRQ capability handle.
 *
 * @return Zero on success or an error code.
 *
 */
errno_t async_irq_unsubscribe(cap_irq_handle_t ihandle)
{
	// TODO: Remove entry from hash table
	//       to avoid memory leak

	return ipc_irq_unsubscribe(ihandle);
}

/** Subscribe to event notifications.
 *
 * @param evno    Event type to subscribe.
 * @param handler Notification handler.
 * @param data    Notification handler client data.
 *
 * @return Zero on success or an error code.
 *
 */
errno_t async_event_subscribe(event_type_t evno,
    async_notification_handler_t handler, void *data)
{
	notification_t *notification = notification_create(handler, data);
	if (!notification)
		return ENOMEM;

	return ipc_event_subscribe(evno, notification->imethod);
}

/** Subscribe to task event notifications.
 *
 * @param evno    Event type to subscribe.
 * @param handler Notification handler.
 * @param data    Notification handler client data.
 *
 * @return Zero on success or an error code.
 *
 */
errno_t async_event_task_subscribe(event_task_type_t evno,
    async_notification_handler_t handler, void *data)
{
	notification_t *notification = notification_create(handler, data);
	if (!notification)
		return ENOMEM;

	return ipc_event_task_subscribe(evno, notification->imethod);
}

/** Unmask event notifications.
 *
 * @param evno Event type to unmask.
 *
 * @return Value returned by the kernel.
 *
 */
errno_t async_event_unmask(event_type_t evno)
{
	return ipc_event_unmask(evno);
}

/** Unmask task event notifications.
 *
 * @param evno Event type to unmask.
 *
 * @return Value returned by the kernel.
 *
 */
errno_t async_event_task_unmask(event_task_type_t evno)
{
	return ipc_event_task_unmask(evno);
}

/** Return new incoming message for the current (fibril-local) connection.
 *
 * @param call  Storage where the incoming call data will be stored.
 * @param usecs Timeout in microseconds. Zero denotes no timeout.
 *
 * @return If no timeout was specified, then true is returned.
 * @return If a timeout is specified, then true is returned unless
 *         the timeout expires prior to receiving a message.
 *
 */
bool async_get_call_timeout(ipc_call_t *call, usec_t usecs)
{
	assert(call);
	assert(fibril_connection);

	struct timespec ts;
	struct timespec *expires = NULL;
	if (usecs) {
		getuptime(&ts);
		ts_add_diff(&ts, USEC2NSEC(usecs));
		expires = &ts;
	}

	errno_t rc = mpsc_receive(fibril_connection->msg_channel,
	    call, expires);

	if (rc == ETIMEOUT)
		return false;

	if (rc != EOK) {
		/*
		 * The async_get_call_timeout() interface doesn't support
		 * propagating errors. Return a null call instead.
		 */

		memset(call, 0, sizeof(ipc_call_t));
	}

	return true;
}

void *async_get_client_data(void)
{
	assert(fibril_connection);
	return fibril_connection->client->data;
}

void *async_get_client_data_by_id(task_id_t client_id)
{
	client_t *client = async_client_get(client_id, false);
	if (!client)
		return NULL;

	if (!client->data) {
		async_client_put(client);
		return NULL;
	}

	return client->data;
}

void async_put_client_data_by_id(task_id_t client_id)
{
	client_t *client = async_client_get(client_id, false);

	assert(client);
	assert(client->data);

	/* Drop the reference we got in async_get_client_data_by_hash(). */
	async_client_put(client);

	/* Drop our own reference we got at the beginning of this function. */
	async_client_put(client);
}

/** Handle a call that was received.
 *
 * If the call has the IPC_M_CONNECT_ME_TO method, a new connection is created.
 * Otherwise the call is routed to its connection fibril.
 *
 * @param call Data of the incoming call.
 *
 */
static void handle_call(ipc_call_t *call)
{
	assert(call);

	if (call->flags & IPC_CALL_ANSWERED) {
		/* Answer to a call made by us. */
		async_reply_received(call);
		return;
	}

	if (call->cap_handle == CAP_NIL) {
		if (call->flags & IPC_CALL_NOTIF) {
			/* Kernel notification */
			queue_notification(call);
		}
		return;
	}

	/* New connection */
	if (IPC_GET_IMETHOD(*call) == IPC_M_CONNECT_ME_TO) {
		iface_t iface = (iface_t) IPC_GET_ARG1(*call);
		sysarg_t in_phone_hash = IPC_GET_ARG5(*call);

		// TODO: Currently ignores all ports but the first one.
		void *data;
		async_port_handler_t handler =
		    async_get_port_handler(iface, 0, &data);

		async_new_connection(call->in_task_id, in_phone_hash, call,
		    handler, data);
		return;
	}

	/* Try to route the call through the connection hash table */
	errno_t rc = route_call(call);
	if (rc == EOK)
		return;

	// TODO: Log the error.

	if (call->cap_handle != CAP_NIL)
		/* Unknown call from unknown phone - hang it up */
		ipc_answer_0(call->cap_handle, EHANGUP);
}

/** Endless loop dispatching incoming calls and answers.
 *
 * @return Never returns.
 *
 */
static errno_t async_manager_worker(void)
{
	ipc_call_t call;
	errno_t rc;

	while (true) {
		rc = fibril_ipc_wait(&call, NULL);
		if (rc == EOK)
			handle_call(&call);
	}

	return 0;
}

/** Function to start async_manager as a standalone fibril.
 *
 * When more kernel threads are used, one async manager should exist per thread.
 *
 * @param arg Unused.
 * @return Never returns.
 *
 */
static errno_t async_manager_fibril(void *arg)
{
	async_manager_worker();
	return 0;
}

/** Add one manager to manager list. */
fid_t async_create_manager(void)
{
	fid_t fid = fibril_create_generic(async_manager_fibril, NULL, PAGE_SIZE);
	fibril_start(fid);
	return fid;
}

/** Initialize the async framework.
 *
 */
void __async_server_init(void)
{
	if (!hash_table_create(&client_hash_table, 0, 0, &client_hash_table_ops))
		abort();

	if (!hash_table_create(&conn_hash_table, 0, 0, &conn_hash_table_ops))
		abort();

	if (!hash_table_create(&notification_hash_table, 0, 0,
	    &notification_hash_table_ops))
		abort();

	async_create_manager();
}

errno_t async_answer_0(ipc_call_t *call, errno_t retval)
{
	return ipc_answer_0(call->cap_handle, retval);
}

errno_t async_answer_1(ipc_call_t *call, errno_t retval, sysarg_t arg1)
{
	return ipc_answer_1(call->cap_handle, retval, arg1);
}

errno_t async_answer_2(ipc_call_t *call, errno_t retval, sysarg_t arg1,
    sysarg_t arg2)
{
	return ipc_answer_2(call->cap_handle, retval, arg1, arg2);
}

errno_t async_answer_3(ipc_call_t *call, errno_t retval, sysarg_t arg1,
    sysarg_t arg2, sysarg_t arg3)
{
	return ipc_answer_3(call->cap_handle, retval, arg1, arg2, arg3);
}

errno_t async_answer_4(ipc_call_t *call, errno_t retval, sysarg_t arg1,
    sysarg_t arg2, sysarg_t arg3, sysarg_t arg4)
{
	return ipc_answer_4(call->cap_handle, retval, arg1, arg2, arg3, arg4);
}

errno_t async_answer_5(ipc_call_t *call, errno_t retval, sysarg_t arg1,
    sysarg_t arg2, sysarg_t arg3, sysarg_t arg4, sysarg_t arg5)
{
	return ipc_answer_5(call->cap_handle, retval, arg1, arg2, arg3, arg4,
	    arg5);
}

errno_t async_forward_fast(ipc_call_t *call, async_exch_t *exch,
    sysarg_t imethod, sysarg_t arg1, sysarg_t arg2, unsigned int mode)
{
	assert(call);

	if (exch == NULL)
		return ENOENT;

	return ipc_forward_fast(call->cap_handle, exch->phone, imethod, arg1,
	    arg2, mode);
}

errno_t async_forward_slow(ipc_call_t *call, async_exch_t *exch,
    sysarg_t imethod, sysarg_t arg1, sysarg_t arg2, sysarg_t arg3,
    sysarg_t arg4, sysarg_t arg5, unsigned int mode)
{
	assert(call);

	if (exch == NULL)
		return ENOENT;

	return ipc_forward_slow(call->cap_handle, exch->phone, imethod, arg1,
	    arg2, arg3, arg4, arg5, mode);
}

/** Wrapper for making IPC_M_CONNECT_TO_ME calls using the async framework.
 *
 * Ask through phone for a new connection to some service.
 *
 * @param exch  Exchange for sending the message.
 * @param iface Callback interface.
 * @param arg2  User defined argument.
 * @param arg3  User defined argument.
 *
 * @return Zero on success or an error code.
 *
 */
errno_t async_connect_to_me(async_exch_t *exch, iface_t iface, sysarg_t arg2,
    sysarg_t arg3)
{
	if (exch == NULL)
		return ENOENT;

	ipc_call_t answer;
	aid_t req = async_send_3(exch, IPC_M_CONNECT_TO_ME, iface, arg2, arg3,
	    &answer);

	errno_t rc;
	async_wait_for(req, &rc);
	if (rc != EOK)
		return (errno_t) rc;

	return EOK;
}

/** Wrapper for receiving the IPC_M_SHARE_IN calls using the async framework.
 *
 * This wrapper only makes it more comfortable to receive IPC_M_SHARE_IN
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * So far, this wrapper is to be used from within a connection fibril.
 *
 * @param call Storage for the data of the IPC_M_SHARE_IN call.
 * @param size Destination address space area size.
 *
 * @return True on success, false on failure.
 *
 */
bool async_share_in_receive(ipc_call_t *call, size_t *size)
{
	assert(call);
	assert(size);

	async_get_call(call);

	if (IPC_GET_IMETHOD(*call) != IPC_M_SHARE_IN)
		return false;

	*size = (size_t) IPC_GET_ARG1(*call);
	return true;
}

/** Wrapper for answering the IPC_M_SHARE_IN calls using the async framework.
 *
 * This wrapper only makes it more comfortable to answer IPC_M_SHARE_IN
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * @param call  IPC_M_DATA_READ call to answer.
 * @param src   Source address space base.
 * @param flags Flags to be used for sharing. Bits can be only cleared.
 *
 * @return Zero on success or a value from @ref errno.h on failure.
 *
 */
errno_t async_share_in_finalize(ipc_call_t *call, void *src, unsigned int flags)
{
	assert(call);

	return ipc_answer_2(call->cap_handle, EOK, (sysarg_t) src, (sysarg_t) flags);
}

/** Wrapper for receiving the IPC_M_SHARE_OUT calls using the async framework.
 *
 * This wrapper only makes it more comfortable to receive IPC_M_SHARE_OUT
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * So far, this wrapper is to be used from within a connection fibril.
 *
 * @param call  Storage for the data of the IPC_M_SHARE_OUT call.
 * @param size  Storage for the source address space area size.
 * @param flags Storage for the sharing flags.
 *
 * @return True on success, false on failure.
 *
 */
bool async_share_out_receive(ipc_call_t *call, size_t *size,
    unsigned int *flags)
{
	assert(call);
	assert(size);
	assert(flags);

	async_get_call(call);

	if (IPC_GET_IMETHOD(*call) != IPC_M_SHARE_OUT)
		return false;

	*size = (size_t) IPC_GET_ARG2(*call);
	*flags = (unsigned int) IPC_GET_ARG3(*call);
	return true;
}

/** Wrapper for answering the IPC_M_SHARE_OUT calls using the async framework.
 *
 * This wrapper only makes it more comfortable to answer IPC_M_SHARE_OUT
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * @param call IPC_M_DATA_WRITE call to answer.
 * @param dst  Address of the storage for the destination address space area
 *             base address.
 *
 * @return  Zero on success or a value from @ref errno.h on failure.
 *
 */
errno_t async_share_out_finalize(ipc_call_t *call, void **dst)
{
	assert(call);

	return ipc_answer_2(call->cap_handle, EOK, (sysarg_t) __progsymbols.end,
	    (sysarg_t) dst);
}

/** Wrapper for receiving the IPC_M_DATA_READ calls using the async framework.
 *
 * This wrapper only makes it more comfortable to receive IPC_M_DATA_READ
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * So far, this wrapper is to be used from within a connection fibril.
 *
 * @param call Storage for the data of the IPC_M_DATA_READ.
 * @param size Storage for the maximum size. Can be NULL.
 *
 * @return True on success, false on failure.
 *
 */
bool async_data_read_receive(ipc_call_t *call, size_t *size)
{
	assert(call);

	async_get_call(call);

	if (IPC_GET_IMETHOD(*call) != IPC_M_DATA_READ)
		return false;

	if (size)
		*size = (size_t) IPC_GET_ARG2(*call);

	return true;
}

/** Wrapper for answering the IPC_M_DATA_READ calls using the async framework.
 *
 * This wrapper only makes it more comfortable to answer IPC_M_DATA_READ
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * @param call IPC_M_DATA_READ call to answer.
 * @param src  Source address for the IPC_M_DATA_READ call.
 * @param size Size for the IPC_M_DATA_READ call. Can be smaller than
 *             the maximum size announced by the sender.
 *
 * @return  Zero on success or a value from @ref errno.h on failure.
 *
 */
errno_t async_data_read_finalize(ipc_call_t *call, const void *src, size_t size)
{
	assert(call);

	return ipc_answer_2(call->cap_handle, EOK, (sysarg_t) src,
	    (sysarg_t) size);
}

/** Wrapper for forwarding any read request
 *
 */
errno_t async_data_read_forward_fast(async_exch_t *exch, sysarg_t imethod,
    sysarg_t arg1, sysarg_t arg2, sysarg_t arg3, sysarg_t arg4,
    ipc_call_t *dataptr)
{
	if (exch == NULL)
		return ENOENT;

	ipc_call_t call;
	if (!async_data_read_receive(&call, NULL)) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	aid_t msg = async_send_fast(exch, imethod, arg1, arg2, arg3, arg4,
	    dataptr);
	if (msg == 0) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	errno_t retval = ipc_forward_fast(call.cap_handle, exch->phone, 0, 0, 0,
	    IPC_FF_ROUTE_FROM_ME);
	if (retval != EOK) {
		async_forget(msg);
		async_answer_0(&call, retval);
		return retval;
	}

	errno_t rc;
	async_wait_for(msg, &rc);

	return (errno_t) rc;
}

/** Wrapper for receiving the IPC_M_DATA_WRITE calls using the async framework.
 *
 * This wrapper only makes it more comfortable to receive IPC_M_DATA_WRITE
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * So far, this wrapper is to be used from within a connection fibril.
 *
 * @param call Storage for the data of the IPC_M_DATA_WRITE.
 * @param size Storage for the suggested size. May be NULL.
 *
 * @return True on success, false on failure.
 *
 */
bool async_data_write_receive(ipc_call_t *call, size_t *size)
{
	assert(call);

	async_get_call(call);

	if (IPC_GET_IMETHOD(*call) != IPC_M_DATA_WRITE)
		return false;

	if (size)
		*size = (size_t) IPC_GET_ARG2(*call);

	return true;
}

/** Wrapper for answering the IPC_M_DATA_WRITE calls using the async framework.
 *
 * This wrapper only makes it more comfortable to answer IPC_M_DATA_WRITE
 * calls so that the user doesn't have to remember the meaning of each IPC
 * argument.
 *
 * @param call IPC_M_DATA_WRITE call to answer.
 * @param dst  Final destination address for the IPC_M_DATA_WRITE call.
 * @param size Final size for the IPC_M_DATA_WRITE call.
 *
 * @return  Zero on success or a value from @ref errno.h on failure.
 *
 */
errno_t async_data_write_finalize(ipc_call_t *call, void *dst, size_t size)
{
	assert(call);

	return async_answer_2(call, EOK, (sysarg_t) dst, (sysarg_t) size);
}

/** Wrapper for receiving binary data or strings
 *
 * This wrapper only makes it more comfortable to use async_data_write_*
 * functions to receive binary data or strings.
 *
 * @param data       Pointer to data pointer (which should be later disposed
 *                   by free()). If the operation fails, the pointer is not
 *                   touched.
 * @param nullterm   If true then the received data is always zero terminated.
 *                   This also causes to allocate one extra byte beyond the
 *                   raw transmitted data.
 * @param min_size   Minimum size (in bytes) of the data to receive.
 * @param max_size   Maximum size (in bytes) of the data to receive. 0 means
 *                   no limit.
 * @param granulariy If non-zero then the size of the received data has to
 *                   be divisible by this value.
 * @param received   If not NULL, the size of the received data is stored here.
 *
 * @return Zero on success or a value from @ref errno.h on failure.
 *
 */
errno_t async_data_write_accept(void **data, const bool nullterm,
    const size_t min_size, const size_t max_size, const size_t granularity,
    size_t *received)
{
	assert(data);

	ipc_call_t call;
	size_t size;
	if (!async_data_write_receive(&call, &size)) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	if (size < min_size) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	if ((max_size > 0) && (size > max_size)) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	if ((granularity > 0) && ((size % granularity) != 0)) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	void *arg_data;

	if (nullterm)
		arg_data = malloc(size + 1);
	else
		arg_data = malloc(size);

	if (arg_data == NULL) {
		async_answer_0(&call, ENOMEM);
		return ENOMEM;
	}

	errno_t rc = async_data_write_finalize(&call, arg_data, size);
	if (rc != EOK) {
		free(arg_data);
		return rc;
	}

	if (nullterm)
		((char *) arg_data)[size] = 0;

	*data = arg_data;
	if (received != NULL)
		*received = size;

	return EOK;
}

/** Wrapper for voiding any data that is about to be received
 *
 * This wrapper can be used to void any pending data
 *
 * @param retval Error value from @ref errno.h to be returned to the caller.
 *
 */
void async_data_write_void(errno_t retval)
{
	ipc_call_t call;
	async_data_write_receive(&call, NULL);
	async_answer_0(&call, retval);
}

/** Wrapper for forwarding any data that is about to be received
 *
 */
errno_t async_data_write_forward_fast(async_exch_t *exch, sysarg_t imethod,
    sysarg_t arg1, sysarg_t arg2, sysarg_t arg3, sysarg_t arg4,
    ipc_call_t *dataptr)
{
	if (exch == NULL)
		return ENOENT;

	ipc_call_t call;
	if (!async_data_write_receive(&call, NULL)) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	aid_t msg = async_send_fast(exch, imethod, arg1, arg2, arg3, arg4,
	    dataptr);
	if (msg == 0) {
		async_answer_0(&call, EINVAL);
		return EINVAL;
	}

	errno_t retval = ipc_forward_fast(call.cap_handle, exch->phone, 0, 0, 0,
	    IPC_FF_ROUTE_FROM_ME);
	if (retval != EOK) {
		async_forget(msg);
		async_answer_0(&call, retval);
		return retval;
	}

	errno_t rc;
	async_wait_for(msg, &rc);

	return (errno_t) rc;
}

/** Wrapper for receiving the IPC_M_CONNECT_TO_ME calls.
 *
 * If the current call is IPC_M_CONNECT_TO_ME then a new
 * async session is created for the accepted phone.
 *
 * @param mgmt Exchange management style.
 *
 * @return New async session.
 * @return NULL on failure.
 *
 */
async_sess_t *async_callback_receive(exch_mgmt_t mgmt)
{
	/* Accept the phone */
	ipc_call_t call;
	async_get_call(&call);

	cap_phone_handle_t phandle = (cap_handle_t) IPC_GET_ARG5(call);

	if ((IPC_GET_IMETHOD(call) != IPC_M_CONNECT_TO_ME) ||
	    !CAP_HANDLE_VALID((phandle))) {
		async_answer_0(&call, EINVAL);
		return NULL;
	}

	async_sess_t *sess = calloc(1, sizeof(async_sess_t));
	if (sess == NULL) {
		async_answer_0(&call, ENOMEM);
		return NULL;
	}

	sess->iface = 0;
	sess->mgmt = mgmt;
	sess->phone = phandle;

	fibril_mutex_initialize(&sess->remote_state_mtx);
	list_initialize(&sess->exch_list);
	fibril_mutex_initialize(&sess->mutex);

	/* Acknowledge the connected phone */
	async_answer_0(&call, EOK);

	return sess;
}

/** Wrapper for receiving the IPC_M_CONNECT_TO_ME calls.
 *
 * If the call is IPC_M_CONNECT_TO_ME then a new
 * async session is created. However, the phone is
 * not accepted automatically.
 *
 * @param mgmt   Exchange management style.
 * @param call   Call data.
 *
 * @return New async session.
 * @return NULL on failure.
 * @return NULL if the call is not IPC_M_CONNECT_TO_ME.
 *
 */
async_sess_t *async_callback_receive_start(exch_mgmt_t mgmt, ipc_call_t *call)
{
	cap_phone_handle_t phandle = (cap_handle_t) IPC_GET_ARG5(*call);

	if ((IPC_GET_IMETHOD(*call) != IPC_M_CONNECT_TO_ME) ||
	    !CAP_HANDLE_VALID((phandle)))
		return NULL;

	async_sess_t *sess = calloc(1, sizeof(async_sess_t));
	if (sess == NULL)
		return NULL;

	sess->iface = 0;
	sess->mgmt = mgmt;
	sess->phone = phandle;

	fibril_mutex_initialize(&sess->remote_state_mtx);
	list_initialize(&sess->exch_list);
	fibril_mutex_initialize(&sess->mutex);

	return sess;
}

bool async_state_change_receive(ipc_call_t *call)
{
	assert(call);

	async_get_call(call);

	if (IPC_GET_IMETHOD(*call) != IPC_M_STATE_CHANGE_AUTHORIZE)
		return false;

	return true;
}

errno_t async_state_change_finalize(ipc_call_t *call, async_exch_t *other_exch)
{
	assert(call);

	return async_answer_1(call, EOK, CAP_HANDLE_RAW(other_exch->phone));
}

__noreturn void async_manager(void)
{
	fibril_event_t ever = FIBRIL_EVENT_INIT;
	fibril_wait_for(&ever);
	__builtin_unreachable();
}

/** @}
 */
