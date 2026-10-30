/**
 * @file pal_backend.h
 * @brief Private contracts between common PAL code and platform backends.
 */

#ifndef PAL_BACKEND_H
#define PAL_BACKEND_H

#include "pal_mutex.h"
#include "pal_socket.h"
#include "pal_thread.h"
#include "pal_time.h"

int32_t pal_sync_mutex_backend_create(void **state);
int32_t pal_sync_mutex_backend_destroy(void *state);
int32_t pal_sync_mutex_backend_lock(void *state);
int32_t pal_sync_mutex_backend_unlock(void *state);
void *pal_mutex_backend_state(pal_mutex_t *mutex);

int32_t pal_sync_condition_backend_create(void **state);
int32_t pal_sync_condition_backend_destroy(void *state);
int32_t pal_sync_condition_backend_wait(
    void *condition,
    pal_mutex_t *mutex,
    uint32_t timeout_ms);
int32_t pal_sync_condition_backend_signal(void *condition);
int32_t pal_sync_monotonic_time_ms(uint64_t *time_ms);

int32_t pal_thread_backend_create(
    pal_thread_fn_t entry,
    void *argument,
    void **state);
int32_t pal_thread_backend_join(void *state, void **result);
int32_t pal_thread_backend_detach(void *state);
void pal_thread_backend_exit(void *result);

int32_t pal_socket_backend_create(
    pal_socket_family_t family,
    pal_socket_type_t type,
    void **state);
int32_t pal_socket_backend_bind(void *state, const pal_socket_address_t *address);
int32_t pal_socket_backend_listen(void *state, int32_t backlog);
int32_t pal_socket_backend_accept(
    void *state,
    uint32_t timeout_ms,
    void **accepted_state,
    pal_socket_address_t *peer_address);
int32_t pal_socket_backend_connect(
    void *state,
    const pal_socket_address_t *address,
    uint32_t timeout_ms);
int32_t pal_socket_backend_send(
    void *state,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms);
int32_t pal_socket_backend_receive(
    void *state,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    uint32_t timeout_ms);
int32_t pal_socket_backend_send_to(
    void *state,
    const pal_socket_address_t *address,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms);
int32_t pal_socket_backend_receive_from(
    void *state,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    pal_socket_address_t *source_address,
    uint32_t timeout_ms);
int32_t pal_socket_backend_get_local_address(
    void *state,
    pal_socket_address_t *address);
int32_t pal_socket_backend_shutdown(void *state, pal_socket_shutdown_mode_t mode);
int32_t pal_socket_backend_close(void *state);

int32_t pal_time_backend_monotonic_ns(uint64_t *time_ns);
int32_t pal_time_backend_utc_ns(uint64_t *time_ns);
int32_t pal_time_backend_sleep_ns(uint64_t duration_ns);

#endif
