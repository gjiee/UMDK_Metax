/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_client_api.h
 * Description   : external APIs of DLock client
 * History       : create file & add functions
 * 1.Date        : 2022-03-21
 * Author        : geyi
 * Modification  : Created file
 */

#ifndef __DLOCK_CLIENT_API_H__
#define __DLOCK_CLIENT_API_H__

#include "dlock_types.h"

namespace dlock {
extern "C" {

/**
 * Initialize the DLock client library context
 * @param[in] p_client_cfg: client configuration parameter structure pointer
 * Return: 0 on success, -1 on failure, DLOCK_EINVAL on invalid arguments
 */
int dclient_lib_init(const struct client_cfg *p_client_cfg);

/**
 * Deinitialize the DLock client library context
 */
void dclient_lib_deinit();

// client management APIs
/**
 * Create a client instance
 * @param[in] ip_str：server IP address，currently only supports the IPv4 format
 * @param[out] p_client_id: client_id pointer, used to store the returned client number assigned by the server
 * the current value range for client ID is 1 to 0x7FFFFFFF.
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: The passed p_client_id pointer is null, or the passed ip_str pointer is null.
 * DLOCK_NOT_READY: The Primary Server encountered an error and was restarted;
 *   it is currently waiting for global lock state recovery from the client and temporarily cannot accept new requests.
 * DLOCK_SERVER_NO_RESOURCE: The maximum number of clients supported by the server has been exceeded,
 *   or the server failed to allocate resources.
 * DLOCK_PROTO_VERSION_NEGOTIATION_FAIL: Indicates that the DLock version negotiation between client and server failed.
 */
int client_init(int *p_client_id, const char *ip_str);

/**
 * Re-register the client instance with the server
 * @param[in] client_id：client ID
 * @param[in] ip_str：server IP address，currently only supports the IPv4 format
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: The provided client_id corresponds to a client instance that has not been created.
 * DLOCK_NOT_READY: Failed to connect to the server port.
 * DLOCK_CLIENT_REMOVED_BY_SERVER: The client instance information has been deleted by the server and cannot
 *   be reconnected via the client_reinit interface. It is recommended to create a new client instance using client_init
 * DLOCK_SERVER_NO_RESOURCE: Resource allocation failed during the fault recovery phase,
 *   or the number of clients actually recovered exceeds the MAX_CLIENT_NUM limit.
 * DLOCK_PROTO_VERSION_NEGOTIATION_FAIL: Indicates that the DLock version negotiation between client and server failed.
 */
int client_reinit(int client_id, const char *ip_str);

/**
 * Unregister the created client instance
 * @param[in] client_id：client ID pending destruction
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_NOT_READY: Primary server encountered an error and was restarted.
 *   Awaiting global lock state recovery from the client. Temporarily not accepting new requests.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 */
int client_deinit(int client_id);

/**
 * The client instance sends a heartbeat check to the server
 * @param[in] client_id：client ID
 * @param[in] timeout：The timeout period in seconds for the heartbeat operation, no more than 300 seconds
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: Invalid parameter, including a timeout value of 0 or greater than 300,
 *   or the client instance corresponding to client_id has not been created.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 * DLOCK_NOT_READY: The primary server encountered an error and was restarted.
 *   Awaiting global lock state recovery from the client. Temporarily not accepting new requests.
 */
int client_heartbeat(int client_id, unsigned int timeout);

/**
 * Notify the server that the client instance re-registration process is complete
 * @param[in] client_id：client ID
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_CLIENT_NOT_INIT: Client instance not initialized.
 * DLOCK_ENOMEM: Memory allocation failed.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 */
int client_reinit_done(int client_id);

// lock management APIs
/**
 * Get a lock from the server for the client
 * @param[in] client_id：client ID
 * @param[in] desc：the metadata structure for a distributed lock to be acquired
 * @param[out] lock_id: distributed lock ID — a globally unique identifier for the lock object
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: Invalid input parameters, including invalid lock types, null pointers, invalid array lengths, etc.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 * DLOCK_NOT_READY: The primary server encountered an error and was restarted.
 *   Awaiting global lock state recovery from the client. Temporarily not accepting new requests.
 * DLOCK_SERVER_NO_RESOURCE: This error is returned when the number of locks exceeds the upper limit
 *   (currently limited to 51200 locks) or when no lock IDs are available.
 */
int get_lock(int client_id, const struct lock_desc *desc, int *lock_id);

/**
 * Specify the client instance to release the acquired distributed lock to the server
 * @param[in] client_id：client ID
 * @param[in] lock_id：the lock ID to be released
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_BAD_RESPONSE: Communication exception with the server;
 * DLOCK_NOT_READY: The primary server encountered an error and is being restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 * DLOCK_TICKET_TO_UNLOCK: Indicates that only a lock ticket has been obtained,
 *   but the lock has not been successfully acquired. In this case, an unlock operation must be performed first,
 *   followed by a release operation, to successfully release the lock.
 * DLOCK_EASYNC: The client is currently performing an asynchronous lock operation on this lock.
 *   During the asynchronous lock operation, releasing the lock object is not allowed.
 */
int release_lock(int client_id, int lock_id);

/**
 * The client instance synchronizes the status of the local distributed locks with the server
 * @param[in] client_id：client ID
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_CLIENT_NOT_INIT: Client instance is not initialized.
 * DLOCK_ENOMEM: Failed to allocate memory.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 * DLOCK_EAGAIN: Failed to synchronize all lock states.
 *   This value prompts the user to continue calling this interface for lock state synchronization.
 */
int update_all_locks(int client_id);

/**
 * Obtain the data plane exception statistics of the client instance
 * @param[in] client_id：client ID
 * @param[out] stats：the data plane exception statistics of the client instance
 * Return the specific status codes as follows
 * DLOCK_SUCCESS：Obtain statistics succeeded
 * DLOCK_EINVAL: Invalid parameter, the passed stats pointer is null.
 * DLOCK_CLIENTMGR_NOT_INIT: The DLock client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: The client is not initialized.
 * DLOCK_EAGAIN: The statistics interface is being called too frequently, please try again later.
 * DLOCK_FAIL: Obtain statistics failed
 */
int get_client_debug_stats(int client_id, struct debug_stats *stats);

/**
 * Clear the data plane exception statistics of the client instance
 * @param[in] client_id：client ID
 * Return the specific status codes as follows
 * DLOCK_SUCCESS: Clear statistics succeeded.
 * DLOCK_CLIENTMGR_NOT_INIT: DLock client library context not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client instance not initialized.
 * DLOCK_EAGAIN: Statistical interface call frequency too high, please try again later.
 * DLOCK_FAIL: Clear statistics failed.
 */
int clear_client_debug_stats(int client_id);

// lock operation APIs
/**
 * Client instance performs a non-blocking lock operation on a specified lock to the server
 * @param[in] client_id：client ID
 * @param[in] req: The request structure parameters for the trylock operation include the lock object lock_id,
 *                 the trylock operation type lock_op, and the lock expiration time expire_time (in seconds).
 * @param[out] result：the data structure that stores the lock status results returned after lock operations.
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully acquired the lock on the specified lock object.
 * DLOCK_ALREADY_LOCKED: Successfully reacquired the lock (duplicate lock request).
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_LOCK_NOT_GET: Client has not acquired the lock corresponding to this lock_id.
 * DLOCK_BAD_RESPONSE: Communication with the server failed or the received message is invalid.
 * DLOCK_FAIL: Lock acquisition failed.
 *   After this error, the client’s locally cached lock state will be reset to the initial state.
 * DLOCK_EAGAIN (applies only to fair locks): Lock queuing successful.
 *   The client locally stores the obtained ticket value.
 * DLOCK_ETICKET (applies only to fair locks): Lock operation does not match the queued operation.
 *   For example, a shared lock is requested when the fair lock already has an exclusive ticket,
 *   or an exclusive lock is requested when the fair lock already has a shared ticket.
 * DLOCK_NOT_READY: The primary server encountered an error and is being restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 * DLOCK_EASYNC: The client is in an asynchronous lock operation state.
 *   No other lock operations are allowed during an ongoing asynchronous lock operation.
 */
int trylock(int client_id, const struct lock_request *req, void *result);

/**
 * Client instance performs a blocking lock operation on a specified lock to the server
 * @param[in] client_id：client ID
 * @param[in] req: The request structure parameters for the lock operation
 * @param[out] result：the data structure that stores the lock status results returned after lock operations
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully acquired the lock on the specified lock object.
 * DLOCK_ALREADY_LOCKED: Successfully reacquired the lock (duplicate lock request).
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client instance is not initialized.
 * DLOCK_LOCK_NOT_GET: Client has not acquired the lock corresponding to this lock_id.
 * DLOCK_BAD_RESPONSE: Communication with the server failed or the received message is invalid.
 * DLOCK_FAIL: Lock acquisition failed.
 *   After this error, the client’s locally cached lock state will be reset to the initial state.
 * DLOCK_EAGAIN (applies only to fair locks): Lock queuing successful.
 *   The client locally stores the obtained ticket value.
 * DLOCK_ETICKET (applies only to fair locks): Lock operation does not match the queued operation.
 *   For example, a shared lock is requested when the fair lock already has an exclusive ticket,
 *   or an exclusive lock is requested when the fair lock already has a shared ticket.
 * DLOCK_NOT_READY: The primary server encountered an error and is being restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 * DLOCK_EASYNC: The client is in an asynchronous lock operation state.
 *   No other lock operations are allowed during an ongoing asynchronous lock operation.
 */
int lock(int client_id, const struct lock_request *req, void *result);

/**
 * Unlock the lock corresponding to lock_id
 * @param[in] client_id：client ID
 * @param[in] lock_id: lock ID
 * @param[out] result：the data structure that stores the lock status results returned after lock operations
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully performed the unlock operation on the specified lock.
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_LOCK_NOT_GET: The client has not acquired the lock corresponding to this lock_id.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client instance is not initialized.
 * DLOCK_BAD_RESPONSE: Failed to send or receive messages with the server, or the received message is incorrect.
 * DLOCK_FAIL: The unlock operation failed on the server side.
 *   After this error, the locally cached lock state of the client will be reset to the initialized state.
 * DLOCK_ALREADY_UNLOCKED: The local lock object is not in a locked state.
 * DLOCK_ALREADY_LOCKED: Since locking is reentrant, each lock operation increments the local reference count by 1.
 *   Therefore, unlocking also requires multiple unlock operations. When the lock reference count is greater than 1,
 *   the client locally decrements the count by 1 without contacting the server, and returns DLOCK_ALREADY_LOCKED,
 *   indicating that the lock is not fully unlocked and this client is still occupying it.
 * DLOCK_NOT_READY: The Primary Server encountered an error and was restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 * DLOCK_EASYNC: The client is in an asynchronous lock operation state.
 *   During asynchronous lock operations, no other unlock operations are allowed.
 */
int unlock(int client_id, int lock_id, void *result);

/**
 * Client instance requests to extend the lock timeout from the server
 * @param[in] client_id：client ID
 * @param[in] req：the request structure parameter for the extend operation, including the lock object lock_id,
 *                 the extend operation type lock_op, and the extended lock duration expire_time (in seconds).
 * @param[out] result：the data structure that stores the lock status results returned after lock operations
 * Return status codes as follows
 * DLOCK_SUCCESS: The client has successfully completed the lock duration extension operation.
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_LOCK_NOT_GET: The client has not acquired the lock corresponding to this lock_id.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_BAD_RESPONSE: Message exchange with the server failed or the received message is invalid.
 * DLOCK_FAIL: The server failed to execute lock_extend.
 * DLOCK_NOT_READY: The primary server encountered an error and was restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 * DLOCK_EASYNC: The client is in an asynchronous lock operation state.
 *   During asynchronous lock operations, no other unlock operations are allowed.
 */
int lock_extend(int client_id, const struct lock_request *req, void *result);

// batch APIs
/**
 * Client instance batch create or acquire distributed lock objects from the server
 * @param[in] client_id：client ID
 * @param[in] lock_num: Number of locks for batch operations, with the maximum value limited to MAX_LOCK_BATCH_SIZE (31)
 * @param[in] descs: An array of distributed lock description information structures to be acquired
 * @param[out] lock_ids: The returned distributed lock ID array
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 * DLOCK_NOT_READY: The primary server encountered an error and was restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 */
int batch_get_lock(int client_id, unsigned int lock_num, const struct lock_desc *descs, int *lock_ids);

/**
 * Client instance batch release the acquired distributed lock objects to the server
 * @param[in] client_id：client ID
 * @param[in] lock_num: Number of locks for batch operations, with the maximum value limited to MAX_LOCK_BATCH_SIZE (31)
 * @param[in] lock_ids: Array of distributed lock IDs to be released
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_BAD_RESPONSE: Communication exception with the server.
 * DLOCK_NOT_READY: The primary server encountered an error and was restarted.
 *   Waiting for the global lock state to be restored from the client. New requests are temporarily not accepted.
 */
int batch_release_lock(int client_id, unsigned int lock_num, int *lock_ids);

/**
 * Client instance perform non-blocking lock operations on specified lock objects in batches to the server
 * @param[in] client_id：client ID
 * @param[in] lock_num: Number of locks for batch operations, with the maximum value limited to MAX_LOCK_BATCH_SIZE (31)
 * @param[in] reqs：array of the request structure parameters for trylock operation
 * @param[out] results: The returned array of lock operation results and lock object statuses.
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully acquired the lock for the specified lock object.
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_ENOMEM: Failed to allocate memory.
 */
int batch_trylock(int client_id, unsigned int lock_num, const struct lock_request *reqs, void *results);

/**
 * Client instance performs batch unlock operations on specified lock objects to the server
 * @param[in] client_id：client ID
 * @param[in] lock_num: Number of locks for batch operations, with the maximum value limited to MAX_LOCK_BATCH_SIZE (31)
 * @param[in] lock_ids: Array of distributed lock IDs to be unlocked
 * @param[out] results: The returned array of lock operation results and lock object statuses.
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully performed the unlock operation on the specified lock object.
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_ENOMEM: Failed to allocate memory.
 */
int batch_unlock(int client_id, unsigned int lock_num, const int *lock_ids, void *results);

/**
 * Client instance performs batch operations to extend lock time limits on the server side
 * @param[in] client_id：client ID
 * @param[in] lock_num: Number of locks for batch operations, with the maximum value limited to MAX_LOCK_BATCH_SIZE (31)
 * @param[in] reqs：array of the request structure parameters for lock extend operation
 * @param[out] results: The returned array of lock operation results and lock object statuses.
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully performed the unlock operation on the specified lock object.
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_ENOMEM: Failed to allocate memory.
 */
int batch_lock_extend(int client_id, unsigned int lock_num, const struct lock_request *reqs, void *results);

// async operation APIs
/**
 * The basic asynchronous operation interface returns immediately upon successfully sending the lock request
 * @param[in] client_id：client ID
 * @param[in] req：the request structure parameter
 * Return status codes as follows
 * DLOCK_SUCCESS: The client instance successfully performed the lock operation on the specified lock object.
 * DLOCK_ALREADY_LOCKED: Successfully re-locked (duplicate lock acquired).
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_LOCK_NOT_GET: Client has not acquired the lock corresponding to this lock_id.
 * DLOCK_BAD_RESPONSE: Failed to send or receive messages with the server.
 * DLOCK_EASYNC: Client is in an asynchronous lock operation state;
 *   no additional asynchronous requests are allowed during this period.
 * DLOCK_ALREADY_UNLOCKED: For unlock operations, the local lock object is not in a locked state.
 * DLOCK_ALREADY_LOCKED: For unlock operations, since locking is reentrant,
 *   each lock operation increments the local reference count. Therefore, unlocking requires multiple operations.
 *   If the lock reference count is greater than 1, the client directly decrements the count
 *   without contacting the server and returns this value.
 * DLOCK_ETICKET (only applicable to fair locks): Lock operation does not match the queued operation:
 *   Fair lock is in an exclusive ticket state, but a shared lock is requested.
 *   Fair lock is in a shared ticket state, but an exclusive lock is requested.
 */
int lock_request_async(int client_id, const struct lock_request *req);

/**
 * Query the result of an asynchronous request made by the lock_request_async interface
 * @param[in] client_id：client ID
 * @param[out] result: The returned lock operation result and lock object status
 * Return status codes as follows
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_LOCK_NOT_GET: The client has not acquired the lock corresponding to this lock_id.
 * DLOCK_BAD_RESPONSE: Failed to receive the response from the server or the received message is invalid.
 * DLOCK_NO_ASYNC: No asynchronous operation is currently issued, and no result check is required.
 * DLOCK_EINVAL: The result pointer is null.
 * DLOCK_ETIMEOUT: No response result was polled within the specified time (currently 5 seconds).
 * DLOCK_SUCCESS: The client instance successfully performed the lock operation on the specified lock object.
 * DLOCK_ALREADY_LOCKED: Successfully re-locked (duplicate lock acquisition).
 * DLOCK_EAGAIN (only fair locks return this value):
 *   Lock queuing succeeded, and the client locally stores the obtained ticket value.
 * DLOCK_ASYNC_AGAIN: The response message has not been received yet,
     and this interface needs to be called again to query.
 * DLOCK_FAIL: The lock operation failed on the server side.
 *   After this error, the locally cached lock state on the client will be reset to the initialized state.
 */
int lock_result_check(int client_id, void *result);

// Distribute Object management APIs
/**
 * Create a distributed object and assign it the initial value
 * @param[in] client_id：client ID
 * @param[in] desc: object descriptor
 * @param[in] init_val: Initial value
 * @param[out] obj_id: Returns the object ID upon successful creation
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_FAIL: Failed to create the specified distributed object for the client instance.
 * DLOCK_EINVAL: Parameter validation failed or abnormal message length received.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_OBJECT_ALREADY_CREATED: Object with the same descriptor has already been created.
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_OBJECT_ALREADY_EXISTED: The same descriptor has already been created by another client ID.
 * DLOCK_OBJECT_TOO_MANY: Exceeded the limit of maximum objects.
 * DLOCK_SERVER_NO_RESOURCE: Insufficient server resources.
 * DLOCK_ENOMEM: Failed to allocate memory.
 */
int umo_atomic64_create(int client_id, const struct umo_atomic64_desc *desc, uint64_t init_val, int *obj_id);

/**
 * Destroy a distributed object
 * @param[in] client_id：client ID
 * @param[in] obj_id: object ID
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_FAIL: Failed to destroy the specified distributed object for the client instance.
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_OBJECT_NOT_CREATE: The client ID has not created this object, or the object does not exist on the server side
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_OBJECT_INVALID_OWNER: The object was not created by this client ID.
 * DLOCK_OBJECT_ALREADY_DESTROYED: This object has already been destroyed.
 * DLOCK_ENOMEM: Failed to allocate memory.
 */
int umo_atomic64_destroy(int client_id, int obj_id);

/**
 * Get a distributed object
 * @param[in] client_id：client ID
 * @param[in] desc: object descriptor
 * @param[out] obj_id: object ID
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_EINVAL: Invalid parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_OBJECT_NOT_CREATE: The object does not exist on the server side.
 * DLOCK_OBJECT_ALREADY_DESTROYED: This object has already been destroyed.
 * DLOCK_ENOMEM: Failed to allocate memory.
 * DLOCK_SERVER_NO_RESOURCE: Insufficient server resources.
 */
int umo_atomic64_get(int client_id, const struct umo_atomic64_desc *desc, int *obj_id);

/**
 * Release a distributed object
 * @param[in] client_id：client ID
 * @param[in] obj_id: object ID
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_OBJECT_NOT_GET: The client ID has not acquired this object, or the object does not exist on the server side.
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_ENOMEM: Failed to allocate memory.
 */
int umo_atomic64_release(int client_id, int obj_id);

// Distribute Object operation APIs
/**
 * Fetch and add a distributed object, increase the object value by add_val, and return the value before the increase.
 * @param[in] client_id：client ID
 * @param[in] obj_id: object ID
 * @param[in] add_val: The value to be added
 * @param[in] res_val: The value before addition
 * Return status codes as follows
 * DLOCK_SUCCESS: The faa operation was successful, and the return value is stored in the memory pointed to by res_val.
 * DLOCK_EINVAL: Invalid input parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_OBJECT_NOT_GET: The client ID has not acquired the object.
 * DLOCK_FAIL: Failed to post FAA.
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_EASYNC: Asynchronous operations are still pending.
 */
int umo_atomic64_faa(int client_id, int obj_id, uint64_t add_val, uint64_t *res_val);

/**
 * Compare and swap a distributed object: if the object value equals cmp_val, then modify it to swap_val.
 * @param[in] client_id：client ID
 * @param[in] obj_id: object ID
 * @param[in] cmp_val: Expected value
 * @param[in] swap_val: Update value
 * Return status codes as follows
 * DLOCK_SUCCESS: The cas operation was successful.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_OBJECT_NOT_GET: The client ID has not acquired the object.
 * DLOCK_FAIL: Failed to post CAS.
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_OBJECT_CAS_FAILED: CAS operation failed, meaning the current value of the object is not equal to cmp_val.
 * DLOCK_EASYNC: Asynchronous operations are still pending.
 */
int umo_atomic64_cas(int client_id, int obj_id, uint64_t cmp_val, uint64_t swap_val);

/**
 * Get snapshot of a distributed object
 * @param[in] client_id：client ID
 * @param[in] obj_id: object ID
 * @param[in] res_val: current value of the object
 * Return status codes as follows
 * DLOCK_SUCCESS: The get snapshot operation was successful,
 *   and the return value is stored in the memory pointed to by res_val.
 * DLOCK_EINVAL: Invalid input parameters.
 * DLOCK_CLIENTMGR_NOT_INIT: Client library context is not initialized.
 * DLOCK_CLIENT_NOT_INIT: Client is not initialized.
 * DLOCK_OBJECT_NOT_GET: The client ID has not acquired the object.
 * DLOCK_FAIL: Failed to post read.
 * DLOCK_BAD_RESPONSE: Failed to send or receive the request.
 * DLOCK_EASYNC: Asynchronous operations are still pending.
 */
int umo_atomic64_get_snapshot(int client_id, int obj_id, uint64_t *res_val);
}
};
#endif  // __DLOCK_CLIENT_API_H__
