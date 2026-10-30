/**
 * @file    test_pal_base.c
 * @brief   Self-checking tests for PAL base, synchronization, thread, and socket APIs.
 *
 * @details No external test framework is used so the same file can run on a
 *          desktop OS today and be reused on small targets later. The program
 *          returns 0 when every check passes and 1 otherwise (CTest friendly).
 */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>    /* host errno values used for the collision checks */
#include <limits.h>   /* CHAR_BIT                                        */
#include <stdio.h>    /* printf                                          */
#include <string.h>   /* strcmp, strncmp                                 */

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

#include "pal_types.h"
#include "pal_error.h"
#include "pal_platform.h"
#include "pal_mutex.h"
#include "pal_queue.h"
#include "pal_thread.h"
#include "pal_socket.h"
#include "pal_time.h"

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                   */
/* ------------------------------------------------------------------ */

static int g_checks   = 0;   /* number of executed checks */
static int g_failures = 0;   /* number of failed checks   */

typedef struct queue_wait_test_context
{
    pal_queue_t *queue;
    int32_t status;
    void *item;
} queue_wait_test_context_t;

/* Records one check; prints the failing expression with file and line. */
#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static void test_log_step_result(
    const char *description,
    int checks_before,
    int failures_before)
{
    const int checks_in_step = g_checks - checks_before;
    const int failures_in_step = g_failures - failures_before;

    if (failures_in_step == 0)
    {
        printf("  [ PASS ] %s (%d checks)\n", description, checks_in_step);
    }
    else
    {
        printf(
            "  [ FAIL ] %s (%d/%d checks failed; see FAIL details above)\n",
            description,
            failures_in_step,
            checks_in_step);
    }
}

#define TEST_STEP(description, ...)                                        \
    do {                                                                   \
        const int checks_before = g_checks;                                \
        const int failures_before = g_failures;                            \
        printf("  [ STEP ] %s\n", description);                            \
        __VA_ARGS__;                                                       \
        test_log_step_result(description, checks_before, failures_before); \
    } while (0)

#define RUN_TEST(test_function)                                            \
    do {                                                                   \
        int failures_before = g_failures;                                  \
        printf("[ RUN      ] %s\n", #test_function);                       \
        test_function();                                                   \
        printf("[ %s ] %s\n",                                              \
               (g_failures == failures_before) ? "      OK" : "  FAILED", \
               #test_function);                                            \
    } while (0)

/* ------------------------------------------------------------------ */
/* Compile time checks (a failure here stops the build)                */
/* ------------------------------------------------------------------ */

PAL_STATIC_ASSERT(sizeof(pal_status_t) == 4u, test_status_is_32_bit);
PAL_STATIC_ASSERT(sizeof(pal_handle_t) == sizeof(void *), test_handle_is_pointer_sized);
PAL_STATIC_ASSERT(sizeof(pal_timeout_t) == 4u, test_timeout_is_32_bit);

/* ------------------------------------------------------------------ */
/* Every defined error code, listed once for the tests                 */
/* ------------------------------------------------------------------ */

static const pal_status_t k_all_errors[] =
{
    PAL_ERR_UNKNOWN,          PAL_ERR_INVALID_ARGUMENT, PAL_ERR_NULL_POINTER,
    PAL_ERR_OUT_OF_MEMORY,    PAL_ERR_NOT_SUPPORTED,    PAL_ERR_NOT_IMPLEMENTED,
    PAL_ERR_NOT_INITIALIZED,  PAL_ERR_ALREADY_INITIALIZED, PAL_ERR_INVALID_STATE,
    PAL_ERR_OUT_OF_RANGE,     PAL_ERR_BUFFER_TOO_SMALL, PAL_ERR_OVERFLOW,
    PAL_ERR_TIMEOUT,          PAL_ERR_BUSY,             PAL_ERR_WOULD_BLOCK,
    PAL_ERR_INTERRUPTED,      PAL_ERR_NOT_FOUND,        PAL_ERR_ALREADY_EXISTS,
    PAL_ERR_ACCESS_DENIED,    PAL_ERR_IO,               PAL_ERR_RESOURCE_EXHAUSTED,
    PAL_ERR_CONNECTION_REFUSED, PAL_ERR_ADDRESS_IN_USE, PAL_ERR_NOT_CONNECTED,
    PAL_ERR_NETWORK_UNREACHABLE
};

#define ERROR_COUNT (PAL_ARRAY_SIZE(k_all_errors))

/* ------------------------------------------------------------------ */
/* pal_types tests                                                     */
/* ------------------------------------------------------------------ */

static void test_types(void)
{
    int sample[7];

    TEST_STEP("Array helper returns the number of elements", {
        CHECK(PAL_ARRAY_SIZE(sample) == 7u);
    });
    TEST_STEP("PAL handles and timeout constants have documented values", {
        CHECK(PAL_INVALID_HANDLE == (pal_handle_t)0);
        CHECK(PAL_NO_WAIT == 0u);
        CHECK(PAL_WAIT_FOREVER == UINT32_MAX);
    });
    TEST_STEP("Version fields are valid and PAL_UNUSED compiles", {
        CHECK(PAL_VERSION_MAJOR >= 0 && PAL_VERSION_MINOR >= 0 && PAL_VERSION_PATCH >= 0);
        PAL_UNUSED(sample);
    });
}

/* ------------------------------------------------------------------ */
/* pal_error tests                                                     */
/* ------------------------------------------------------------------ */

static void test_error_basics(void)
{
    size_t i;
    size_t j;

    TEST_STEP("Success status helpers recognize PAL_OK", {
        CHECK(PAL_OK == 0);
        CHECK(PAL_SUCCEEDED(PAL_OK));
        CHECK(!PAL_FAILED(PAL_OK));
    });
    TEST_STEP("Every PAL error is recognized and has a unique symbolic name", {
        for (i = 0u; i < ERROR_COUNT; i++)
        {
            const pal_status_t code = k_all_errors[i];
            const char *name = pal_error_to_string(code);

            CHECK(PAL_FAILED(code));
            CHECK(!PAL_SUCCEEDED(code));
            CHECK(pal_error_is_defined(code));
            CHECK(code <= PAL_ERR_RANGE_MAX);
            CHECK(code >= PAL_ERR_RANGE_MIN);
            CHECK(name != NULL);
            CHECK(strncmp(name, "PAL_ERR_", 8u) == 0);

            for (j = i + 1u; j < ERROR_COUNT; j++)
            {
                CHECK(code != k_all_errors[j]);
                CHECK(strcmp(name, pal_error_to_string(k_all_errors[j])) != 0);
            }
        }
    });
    TEST_STEP("PAL_OK maps to its own symbolic name", {
        CHECK(strcmp(pal_error_to_string(PAL_OK), "PAL_OK") == 0);
        CHECK(pal_error_is_defined(PAL_OK));
    });
}

static void test_error_table_completeness(void)
{
    pal_status_t value;
    size_t defined_in_block = 0u;

    TEST_STEP("Defined errors cover every code in the reserved block", {
        for (value = PAL_ERR_RANGE_MIN; value <= PAL_ERR_RANGE_MAX; value++)
        {
            if (pal_error_is_defined(value))
            {
                defined_in_block++;
            }
        }
        CHECK(defined_in_block == ERROR_COUNT);
    });
    TEST_STEP("Reserved error block anchor is not a valid status", {
        CHECK(!pal_error_is_defined(PAL_ERR_BASE));
    });
}

static void test_error_collisions(void)
{
    size_t i;
    int32_t native;
    int checks_before;
    int failures_before;

    TEST_STEP("PAL error codes do not collide with common OS and HRESULT ranges", {
        for (i = 0u; i < ERROR_COUNT; i++)
        {
            const pal_status_t code = k_all_errors[i];

            CHECK(code < -65535);
            CHECK((((uint32_t)code) >> 24) == 0xFFu);
            for (native = 0; native <= 65535; native++)
            {
                CHECK(code != native);
                CHECK(code != -native);
            }
        }
    });

    /* Also compare against the real errno macros of this host. */
    checks_before = g_checks;
    failures_before = g_failures;
    printf("  [ STEP ] Compare PAL error codes with host errno constants\n");
#define CHECK_NOT_ERRNO(e)                                                 \
    do {                                                                   \
        size_t k;                                                          \
        for (k = 0u; k < ERROR_COUNT; k++) {                               \
            CHECK(k_all_errors[k] != (e));                                 \
            CHECK(k_all_errors[k] != -(e));                                \
        }                                                                  \
    } while (0)

#ifdef EPERM
    CHECK_NOT_ERRNO(EPERM);
#endif
#ifdef ENOENT
    CHECK_NOT_ERRNO(ENOENT);
#endif
#ifdef EINTR
    CHECK_NOT_ERRNO(EINTR);
#endif
#ifdef EIO
    CHECK_NOT_ERRNO(EIO);
#endif
#ifdef EAGAIN
    CHECK_NOT_ERRNO(EAGAIN);
#endif
#ifdef ENOMEM
    CHECK_NOT_ERRNO(ENOMEM);
#endif
#ifdef EACCES
    CHECK_NOT_ERRNO(EACCES);
#endif
#ifdef EBUSY
    CHECK_NOT_ERRNO(EBUSY);
#endif
#ifdef EEXIST
    CHECK_NOT_ERRNO(EEXIST);
#endif
#ifdef EINVAL
    CHECK_NOT_ERRNO(EINVAL);
#endif
#ifdef ERANGE
    CHECK_NOT_ERRNO(ERANGE);
#endif
#ifdef ENOSYS
    CHECK_NOT_ERRNO(ENOSYS);
#endif
#ifdef ETIMEDOUT
    CHECK_NOT_ERRNO(ETIMEDOUT);
#endif
#ifdef EWOULDBLOCK
    CHECK_NOT_ERRNO(EWOULDBLOCK);
#endif
#undef CHECK_NOT_ERRNO
    test_log_step_result(
        "PAL error codes do not collide with host errno constants",
        checks_before,
        failures_before);
}

static void test_error_foreign_values(void)
{
    TEST_STEP("Foreign OS and integer status values are not PAL errors", {
        CHECK(!pal_error_is_defined(-1));
        CHECK(!pal_error_is_defined(1));
        CHECK(!pal_error_is_defined(INT32_MIN));
        CHECK(!pal_error_is_defined(INT32_MAX));
        CHECK(!pal_error_is_defined((pal_status_t)0x80004005u));
        CHECK(!pal_error_is_defined((pal_status_t)0xC0000001u));
    });
#ifdef EINVAL
    TEST_STEP("Host errno values are not interpreted as PAL error codes", {
        CHECK(!pal_error_is_defined(EINVAL));
        CHECK(!pal_error_is_defined(-EINVAL));
    });
#endif
    TEST_STEP("Unknown statuses return a safe placeholder name", {
        CHECK(pal_error_to_string(-1) != NULL);
        CHECK(strcmp(pal_error_to_string(-1), "PAL_UNRECOGNIZED_STATUS") == 0);
        CHECK(strcmp(pal_error_to_string(INT32_MIN), "PAL_UNRECOGNIZED_STATUS") == 0);
    });
}

/* ------------------------------------------------------------------ */
/* pal_platform tests                                                  */
/* ------------------------------------------------------------------ */

static void test_platform(void)
{
    TEST_STEP("OS, compiler, architecture, endianness, and pointer width are consistent", {
        CHECK((PAL_OS_WINDOWS + PAL_OS_LINUX + PAL_OS_MACOS +
               PAL_OS_RTOS + PAL_OS_BAREMETAL) == 1);
        CHECK((PAL_COMPILER_UNKNOWN + PAL_COMPILER_MSVC +
               PAL_COMPILER_GCC + PAL_COMPILER_CLANG) == 1);
        CHECK((PAL_ARCH_UNKNOWN + PAL_ARCH_X86 + PAL_ARCH_X64 +
               PAL_ARCH_ARM + PAL_ARCH_ARM64 + PAL_ARCH_RISCV) == 1);
        CHECK(PAL_OS_POSIX == (PAL_OS_LINUX || PAL_OS_MACOS));
        CHECK((PAL_IS_LITTLE_ENDIAN + PAL_IS_BIG_ENDIAN) == 1);
        CHECK(pal_platform_probe_little_endian() == (PAL_IS_LITTLE_ENDIAN != 0));
        CHECK(PAL_PTR_BITS == (int)(sizeof(void *) * CHAR_BIT));
    });
    TEST_STEP("Platform identity functions return readable names", {
        CHECK(pal_platform_get_os_name() != NULL && pal_platform_get_os_name()[0] != '\0');
        CHECK(pal_platform_get_compiler_name() != NULL &&
              pal_platform_get_compiler_name()[0] != '\0');
        CHECK(pal_platform_get_arch_name() != NULL &&
              pal_platform_get_arch_name()[0] != '\0');
        printf("    detected: os=%s compiler=%s arch=%s ptr=%d-bit endian=%s\n",
               pal_platform_get_os_name(), pal_platform_get_compiler_name(),
               pal_platform_get_arch_name(), PAL_PTR_BITS,
               PAL_IS_LITTLE_ENDIAN ? "little" : "big");
    });
}

/**
 * @brief Verifies mutex creation, lock/unlock, destruction, and null checks.
 */
static void test_mutex(void)
{
    pal_mutex_t *mutex = NULL;

    TEST_STEP("Create mutex, acquire and release its lock, then destroy it", {
        CHECK(pal_mutex_create(&mutex) == PAL_OK);
        CHECK(mutex != NULL);
        CHECK(pal_mutex_lock(mutex) == PAL_OK);
        CHECK(pal_mutex_unlock(mutex) == PAL_OK);
        CHECK(pal_mutex_destroy(mutex) == PAL_OK);
    });
    TEST_STEP("Null mutex arguments are rejected", {
        CHECK(pal_mutex_create(NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_mutex_lock(NULL) == PAL_ERR_NULL_POINTER);
    });
}

/**
 * @brief Verifies queue FIFO ordering, capacity, and timeout behaviors.
 */
static void test_queue(void)
{
    pal_queue_t *queue = NULL;
    int first = 1;
    int second = 2;
    void *item = NULL;

    TEST_STEP("Queue creation enforces a positive capacity", {
        CHECK(pal_queue_create(0u, &queue) == PAL_ERR_INVALID_ARGUMENT);
        CHECK(queue == NULL);
        CHECK(pal_queue_create(2u, &queue) == PAL_OK);
        CHECK(queue != NULL);
    });
    TEST_STEP("Empty queue pop reports non-blocking and timed wait results", {
        CHECK(pal_queue_pop(queue, &item, PAL_NO_WAIT) == PAL_ERR_WOULD_BLOCK);
        CHECK(pal_queue_pop(queue, &item, 5u) == PAL_ERR_TIMEOUT);
    });
    TEST_STEP("Queue preserves FIFO order and reports full capacity", {
        CHECK(pal_queue_push(queue, &first, PAL_NO_WAIT) == PAL_OK);
        CHECK(pal_queue_push(queue, &second, PAL_NO_WAIT) == PAL_OK);
        CHECK(pal_queue_push(queue, NULL, PAL_NO_WAIT) == PAL_ERR_WOULD_BLOCK);
        CHECK(pal_queue_push(queue, NULL, 5u) == PAL_ERR_TIMEOUT);
        CHECK(pal_queue_pop(queue, &item, PAL_NO_WAIT) == PAL_OK);
        CHECK(item == &first);
        CHECK(pal_queue_pop(queue, &item, PAL_NO_WAIT) == PAL_OK);
        CHECK(item == &second);
    });
    TEST_STEP("Queue handles empty reads, null outputs, and destruction", {
        CHECK(pal_queue_pop(queue, &item, PAL_NO_WAIT) == PAL_ERR_WOULD_BLOCK);
        CHECK(pal_queue_pop(queue, NULL, PAL_NO_WAIT) == PAL_ERR_NULL_POINTER);
        CHECK(pal_queue_destroy(queue) == PAL_OK);
        CHECK(pal_queue_destroy(NULL) == PAL_ERR_NULL_POINTER);
    });
}

/**
 * @brief Waits for a queue item on behalf of the cross-platform wakeup test.
 * @param[in,out] context Queue and result storage for the worker thread.
 * @return Thread-library-specific success value.
 */
#if defined(_WIN32)
static DWORD WINAPI test_queue_waiter(LPVOID context)
#else
static void *test_queue_waiter(void *context)
#endif
{
    queue_wait_test_context_t *wait_context = (queue_wait_test_context_t *)context;

    wait_context->status = pal_queue_pop(
        wait_context->queue,
        &wait_context->item,
        2000u);

#if defined(_WIN32)
    return 0u;
#else
    return NULL;
#endif
}

/**
 * @brief Verifies that a blocked queue consumer wakes after an item is pushed.
 */
static void test_queue_wakeup(void)
{
    pal_queue_t *queue = NULL;
    queue_wait_test_context_t context;
    int value = 42;

    context.queue = NULL;
    context.status = PAL_ERR_UNKNOWN;
    context.item = NULL;
    TEST_STEP("Create a queue for the blocked-consumer wakeup test", {
        CHECK(pal_queue_create(1u, &queue) == PAL_OK);
    });
    if (queue == NULL)
    {
        return;
    }
    context.queue = queue;

#if defined(_WIN32)
    {
        HANDLE thread = CreateThread(NULL, 0u, test_queue_waiter, &context, 0u, NULL);

        CHECK(thread != NULL);
        if (thread != NULL)
        {
            Sleep(20u);
            TEST_STEP("Pushing an item wakes the waiting queue consumer", {
                CHECK(pal_queue_push(queue, &value, PAL_NO_WAIT) == PAL_OK);
                CHECK(WaitForSingleObject(thread, 3000u) == WAIT_OBJECT_0);
            });
            CHECK(CloseHandle(thread) != 0);
        }
    }
#else
    {
        pthread_t thread;
        int thread_status = pthread_create(&thread, NULL, test_queue_waiter, &context);

        CHECK(thread_status == 0);
        if (thread_status == 0)
        {
            const struct timespec delay = { 0, 20000000L };

            (void)nanosleep(&delay, NULL);
            TEST_STEP("Pushing an item wakes the waiting queue consumer", {
                CHECK(pal_queue_push(queue, &value, PAL_NO_WAIT) == PAL_OK);
                CHECK(pthread_join(thread, NULL) == 0);
            });
        }
    }
#endif

    TEST_STEP("Consumer receives the exact item and queue cleanup succeeds", {
        CHECK(context.status == PAL_OK);
        CHECK(context.item == &value);
        CHECK(pal_queue_destroy(queue) == PAL_OK);
    });
}

/**
 * @brief Returns the supplied pointer from a thread entry function.
 * @param[in] argument Value returned to the joining thread.
 * @return The supplied argument pointer.
 */
static void *test_thread_return(void *argument)
{
    return argument;
}

/**
 * @brief Terminates the current PAL thread through pal_thread_exit().
 * @param[in] argument Value supplied as the thread result.
 * @return This function does not return.
 */
static void *test_thread_exit(void *argument)
{
    pal_thread_exit(argument);
    return NULL;
}

/**
 * @brief Verifies thread creation, result retrieval, exit, and detach.
 */
static void test_thread(void)
{
    pal_thread_t *thread = NULL;
    int result_value = 7;
    void *result = NULL;

    TEST_STEP("Created thread returns its result through join", {
        CHECK(pal_thread_create(test_thread_return, &result_value, &thread) == PAL_OK);
        CHECK(thread != NULL);
        CHECK(pal_thread_join(thread, &result) == PAL_OK);
        CHECK(result == &result_value);
    });

    thread = NULL;
    result = NULL;
    TEST_STEP("Thread exit API delivers the supplied result to join", {
        CHECK(pal_thread_create(test_thread_exit, &result_value, &thread) == PAL_OK);
        CHECK(thread != NULL);
        CHECK(pal_thread_join(thread, &result) == PAL_OK);
        CHECK(result == &result_value);
    });

    thread = NULL;
    TEST_STEP("Created thread can be detached", {
        CHECK(pal_thread_create(test_thread_return, NULL, &thread) == PAL_OK);
        CHECK(thread != NULL);
        CHECK(pal_thread_detach(thread) == PAL_OK);
    });
    TEST_STEP("Thread APIs reject null required arguments", {
        CHECK(pal_thread_create(NULL, NULL, &thread) == PAL_ERR_NULL_POINTER);
        CHECK(pal_thread_create(test_thread_return, NULL, NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_thread_join(NULL, NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_thread_detach(NULL) == PAL_ERR_NULL_POINTER);
    });
}

typedef struct tcp_echo_context
{
    pal_socket_t *listener;
    int32_t status;
} tcp_echo_context_t;

/**
 * @brief Accepts one TCP connection and echoes its received bytes.
 * @param[in] argument TCP listener context.
 * @return NULL when the operation completes.
 */
static void *test_tcp_echo_server(void *argument)
{
    tcp_echo_context_t *context = (tcp_echo_context_t *)argument;
    pal_socket_t *accepted = NULL;
    char buffer[32];
    size_t received = 0u;
    size_t sent = 0u;

    context->status = pal_socket_accept(
        context->listener,
        3000u,
        &accepted,
        NULL);
    if (context->status == PAL_OK)
    {
        context->status = pal_socket_receive(
            accepted,
            buffer,
            sizeof(buffer),
            &received,
            3000u);
    }
    if (context->status == PAL_OK)
    {
        context->status = pal_socket_send(
            accepted,
            buffer,
            received,
            &sent,
            3000u);
        if (context->status == PAL_OK && sent != received)
        {
            context->status = PAL_ERR_IO;
        }
    }
    if (accepted != NULL)
    {
        const int32_t close_status = pal_socket_close(accepted);

        if (context->status == PAL_OK)
        {
            context->status = close_status;
        }
    }
    return NULL;
}

/**
 * @brief Verifies TCP connection, stream I/O, and UDP datagram exchange.
 */
static void test_sockets(void)
{
    pal_socket_t *listener = NULL;
    pal_socket_t *client = NULL;
    pal_socket_t *datagram_server = NULL;
    pal_socket_t *datagram_client = NULL;
    pal_socket_address_t loopback;
    pal_socket_address_t server_address;
    pal_socket_address_t client_address;
    pal_socket_address_t source_address;
    tcp_echo_context_t echo_context;
    pal_thread_t *server_thread = NULL;
    char received_buffer[32];
    const char message[] = "pal socket";
    size_t transferred = 0u;
    size_t received = 0u;
    void *thread_result = NULL;

    memset(&loopback, 0, sizeof(loopback));
    loopback.family = PAL_SOCKET_FAMILY_IPV4;
    loopback.address[0] = 127u;
    loopback.address[3] = 1u;

    TEST_STEP("Create TCP listener, bind loopback, and obtain its ephemeral port", {
        CHECK(pal_socket_create(
                  PAL_SOCKET_FAMILY_IPV4,
                  PAL_SOCKET_TYPE_TCP,
                  &listener) == PAL_OK);
        CHECK(pal_socket_bind(listener, &loopback) == PAL_OK);
        CHECK(pal_socket_listen(listener, 4) == PAL_OK);
        CHECK(pal_socket_get_local_address(listener, &server_address) == PAL_OK);
        CHECK(server_address.port != 0u);
    });

    echo_context.listener = listener;
    echo_context.status = PAL_ERR_UNKNOWN;
    TEST_STEP("TCP client connects, exchanges the echo payload, and closes cleanly", {
        CHECK(pal_thread_create(test_tcp_echo_server, &echo_context, &server_thread) == PAL_OK);
        CHECK(pal_socket_create(
                  PAL_SOCKET_FAMILY_IPV4,
                  PAL_SOCKET_TYPE_TCP,
                  &client) == PAL_OK);
        CHECK(pal_socket_send(
                  client,
                  message,
                  sizeof(message),
                  &transferred,
                  PAL_NO_WAIT) == PAL_ERR_NOT_CONNECTED);
        CHECK(pal_socket_connect(client, &server_address, 3000u) == PAL_OK);
        CHECK(pal_socket_send(
                  client,
                  message,
                  sizeof(message),
                  &transferred,
                  3000u) == PAL_OK);
        CHECK(transferred == sizeof(message));
        CHECK(pal_socket_receive(
                  client,
                  received_buffer,
                  sizeof(received_buffer),
                  &received,
                  3000u) == PAL_OK);
        CHECK(received == sizeof(message));
        CHECK(memcmp(received_buffer, message, sizeof(message)) == 0);
        CHECK(pal_thread_join(server_thread, &thread_result) == PAL_OK);
        CHECK(thread_result == NULL);
        CHECK(echo_context.status == PAL_OK);
        CHECK(pal_socket_close(client) == PAL_OK);
        CHECK(pal_socket_close(listener) == PAL_OK);
    });

    TEST_STEP("UDP setup and empty receives report timeout and non-blocking status", {
        CHECK(pal_socket_create(
                  PAL_SOCKET_FAMILY_IPV4,
                  PAL_SOCKET_TYPE_UDP,
                  &datagram_server) == PAL_OK);
        CHECK(pal_socket_bind(datagram_server, &loopback) == PAL_OK);
        CHECK(pal_socket_get_local_address(datagram_server, &server_address) == PAL_OK);
        CHECK(pal_socket_create(
                  PAL_SOCKET_FAMILY_IPV4,
                  PAL_SOCKET_TYPE_UDP,
                  &datagram_client) == PAL_OK);
        CHECK(pal_socket_receive_from(
                  datagram_server,
                  received_buffer,
                  sizeof(received_buffer),
                  &received,
                  &source_address,
                  2u) == PAL_ERR_TIMEOUT);
        CHECK(pal_socket_receive_from(
                  datagram_server,
                  received_buffer,
                  sizeof(received_buffer),
                  &received,
                  &source_address,
                  PAL_NO_WAIT) == PAL_ERR_WOULD_BLOCK);
    });
    TEST_STEP("UDP client and server exchange datagrams in both directions", {
        CHECK(pal_socket_send_to(
                  datagram_client,
                  &server_address,
                  message,
                  sizeof(message),
                  &transferred,
                  1000u) == PAL_OK);
        CHECK(transferred == sizeof(message));
        CHECK(pal_socket_receive_from(
                  datagram_server,
                  received_buffer,
                  sizeof(received_buffer),
                  &received,
                  &source_address,
                  1000u) == PAL_OK);
        CHECK(received == sizeof(message));
        CHECK(source_address.family == PAL_SOCKET_FAMILY_IPV4);
        CHECK(source_address.port != 0u);
        CHECK(memcmp(received_buffer, message, sizeof(message)) == 0);
        CHECK(pal_socket_get_local_address(datagram_client, &client_address) == PAL_OK);
        CHECK(client_address.port != 0u);
        CHECK(pal_socket_send_to(
                  datagram_server,
                  &source_address,
                  message,
                  sizeof(message),
                  &transferred,
                  1000u) == PAL_OK);
        CHECK(pal_socket_receive_from(
                  datagram_client,
                  received_buffer,
                  sizeof(received_buffer),
                  &received,
                  NULL,
                  1000u) == PAL_OK);
        CHECK(received == sizeof(message));
        CHECK(memcmp(received_buffer, message, sizeof(message)) == 0);
    });
    TEST_STEP("Socket resources close and invalid inputs are rejected", {
        CHECK(pal_socket_close(datagram_client) == PAL_OK);
        CHECK(pal_socket_close(datagram_server) == PAL_OK);
        CHECK(pal_socket_create(
                  (pal_socket_family_t)0,
                  PAL_SOCKET_TYPE_TCP,
                  &client) == PAL_ERR_INVALID_ARGUMENT);
        CHECK(pal_socket_create(
                  PAL_SOCKET_FAMILY_IPV4,
                  PAL_SOCKET_TYPE_TCP,
                  NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_socket_close(NULL) == PAL_ERR_NULL_POINTER);
    });
}

/* ------------------------------------------------------------------ */
/* pal_time tests                                                      */
/* ------------------------------------------------------------------ */

static void test_time(void)
{
    uint64_t monotonic_before;
    uint64_t monotonic_after;
    uint64_t utc_time;
    uint64_t sleep_start;
    uint64_t sleep_end;
    uint64_t elapsed_ns;
    uint64_t elapsed_us;
    uint64_t elapsed_ms;
    uint64_t remaining_ns;
    bool expired;
    pal_stopwatch_t stopwatch = {0u, 0u, false, false};
    pal_deadline_t deadline = {0u};

    TEST_STEP("Monotonic clock advances, UTC is plausible, and null outputs are rejected", {
        CHECK(pal_time_monotonic_ns(NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_time_monotonic_us(NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_time_monotonic_ms(NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_time_utc_ns(NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_time_monotonic_ns(&monotonic_before) == PAL_OK);
        CHECK(pal_time_monotonic_ns(&monotonic_after) == PAL_OK);
        CHECK(monotonic_after >= monotonic_before);
        CHECK(pal_time_utc_ns(&utc_time) == PAL_OK);
        CHECK(utc_time > UINT64_C(1577836800000000000));
    });
    TEST_STEP("Sleep waits at least the requested time and rejects conversion overflow", {
        CHECK(pal_time_monotonic_ns(&sleep_start) == PAL_OK);
        CHECK(pal_time_sleep_ms(2u) == PAL_OK);
        CHECK(pal_time_monotonic_ns(&sleep_end) == PAL_OK);
        CHECK(sleep_end >= sleep_start);
        CHECK((sleep_end - sleep_start) >= UINT64_C(2000000));
        CHECK(pal_time_sleep_ns(0u) == PAL_OK);
        CHECK(pal_time_sleep_us(UINT64_MAX) == PAL_ERR_OVERFLOW);
        CHECK(pal_time_sleep_ms(UINT64_MAX) == PAL_ERR_OVERFLOW);
    });
    TEST_STEP("Stopwatch starts, measures elapsed time, stops, and converts units", {
        CHECK(pal_stopwatch_elapsed_ns(NULL, &elapsed_ns) == PAL_ERR_NULL_POINTER);
        CHECK(pal_stopwatch_elapsed_ns(&stopwatch, NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_stopwatch_stop(&stopwatch) == PAL_ERR_INVALID_STATE);
        CHECK(pal_stopwatch_elapsed_ns(&stopwatch, &elapsed_ns) == PAL_ERR_INVALID_STATE);
        CHECK(pal_stopwatch_start(NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_stopwatch_start(&stopwatch) == PAL_OK);
        CHECK(pal_time_sleep_ms(1u) == PAL_OK);
        CHECK(pal_stopwatch_elapsed_ns(&stopwatch, &elapsed_ns) == PAL_OK);
        CHECK(elapsed_ns >= UINT64_C(1000000));
        CHECK(pal_stopwatch_stop(&stopwatch) == PAL_OK);
        CHECK(pal_stopwatch_elapsed_ns(&stopwatch, &elapsed_ns) == PAL_OK);
        CHECK(pal_stopwatch_elapsed_us(&stopwatch, &elapsed_us) == PAL_OK);
        CHECK(elapsed_us == elapsed_ns / PAL_NANOSECONDS_PER_MICROSECOND);
        CHECK(pal_stopwatch_elapsed_ms(&stopwatch, &elapsed_ms) == PAL_OK);
        CHECK(elapsed_ms == elapsed_ns / PAL_NANOSECONDS_PER_MILLISECOND);
        CHECK(pal_stopwatch_elapsed_ns(&stopwatch, &elapsed_ns) == PAL_OK);
        CHECK(pal_stopwatch_stop(&stopwatch) == PAL_OK);
        CHECK(pal_stopwatch_start(&stopwatch) == PAL_OK);
    });
    TEST_STEP("Deadlines report remaining time, expiration, and overflow", {
        CHECK(pal_deadline_init(NULL, 1u) == PAL_ERR_NULL_POINTER);
        CHECK(pal_deadline_init(&deadline, UINT64_MAX) == PAL_ERR_OVERFLOW);
        CHECK(pal_deadline_init(&deadline, UINT64_C(1000000000)) == PAL_OK);
        CHECK(pal_deadline_remaining_ns(NULL, &remaining_ns) == PAL_ERR_NULL_POINTER);
        CHECK(pal_deadline_remaining_ns(&deadline, NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_deadline_remaining_ns(&deadline, &remaining_ns) == PAL_OK);
        CHECK(remaining_ns > 0u && remaining_ns <= UINT64_C(1000000000));
        CHECK(pal_deadline_is_expired(NULL, &expired) == PAL_ERR_NULL_POINTER);
        CHECK(pal_deadline_is_expired(&deadline, NULL) == PAL_ERR_NULL_POINTER);
        CHECK(pal_deadline_is_expired(&deadline, &expired) == PAL_OK);
        CHECK(!expired);
        CHECK(pal_deadline_init(&deadline, 0u) == PAL_OK);
        CHECK(pal_deadline_is_expired(&deadline, &expired) == PAL_OK);
        CHECK(expired);
        CHECK(pal_deadline_remaining_ns(&deadline, &remaining_ns) == PAL_OK);
        CHECK(remaining_ns == 0u);
    });
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void)
{
    RUN_TEST(test_types);
    RUN_TEST(test_error_basics);
    RUN_TEST(test_error_table_completeness);
    RUN_TEST(test_error_collisions);
    RUN_TEST(test_error_foreign_values);
    RUN_TEST(test_platform);
    RUN_TEST(test_mutex);
    RUN_TEST(test_queue);
    RUN_TEST(test_queue_wakeup);
    RUN_TEST(test_thread);
    RUN_TEST(test_sockets);
    RUN_TEST(test_time);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
