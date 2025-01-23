// A tester for assignment 4.
// If the tester succeeds, it will print "All tests succeeded!" in the end.
// Otherwise, it will hopefully print the reason it failed.
// Don't forget to compile with -pthread
//
// There are four tests here:
// - Single-Threaded Test:
//   checks if the queue works in a single thread.
// - Reader Starvation Test:
//   checks if the queue works when reader threads run before writer threads.
// - Write-First Test:
//   checks if the queue works when writer threads run before reader threads.
// - Order Test:
//   checks if writer threads send values to waiting readers in FIFO order.

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdint.h>

void initQueue(void);
void destroyQueue(void);
void enqueue(void *value);
void *dequeue(void);
bool tryDequeue(void **output);
size_t visited(void);

char addresses[256];

static void test_single_threaded(void) {
    printf("Single-Threaded Test\n");
    printf("====================\n");
    printf("Initializing queue.\n");
    initQueue();
    int actions[] = {
        1, 2, 3, -1, 4, 5, 6, -2, -3, -4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
        -5, -6, -7, -8, -9, -10, -11, -12, -13, 18, 19, -14, -15, -16, -17, -18
    };
    if (visited() != 0) {
        printf("visited() should return zero.\n");
        exit(1);
    }
    for (size_t i = 0; i < sizeof(actions) / sizeof(int); i++) {
        int action = actions[i];
        if (action > 0) {
            printf("Enqueuing %dth pointer.\n", action);
            enqueue(&addresses[action]);
        }
        else {
            printf("Dequeuing; expecting %dth pointer.\n", -action);
            assert(dequeue() == &addresses[-action]);
            if (visited() != (size_t)-action) {
                printf("visited() returned wrong number.\n");
                exit(1);
            }
        }
    }
    void *tmp;
    printf("Trying to dequeue; expecting to succeed.\n");
    assert(tryDequeue(&tmp) && tmp == &addresses[19]);
    printf("Trying to dequeue; expecting to fail.\n");
    assert(!tryDequeue(&tmp));
    printf("Destroying queue.\n");
    destroyQueue();
    printf("\n");
}

atomic_char test_reader_starvation_counters[25];

static int test_reader_starvation_reader(void *input) {
    int index = (int)(uintptr_t)input;
    printf("Reader thread %d: started.\n", index);
    for (int i = 0; i < 5; i++) {
        printf("Reader thread %d: dequeuing.\n", index);
        void *ptr = dequeue();
        if ((uintptr_t)ptr < (uintptr_t)addresses ||
            (uintptr_t)ptr > (uintptr_t)&addresses[25])
        {
            printf("Reader thread %d: dequeued pointer was out of range.\n", index);
            exit(1);
        }
        int n = (char*)ptr - addresses;
        printf("Reader thread %d: dequeued the %dth pointer.\n", index, n);
        test_reader_starvation_counters[n]++;
    }
    printf("Reader thread %d: finished.\n", index);
    return thrd_success;
}

static int test_reader_starvation_writer(void *input) {
    int index = (int)(uintptr_t)input;
    printf("Writer thread %d: started.\n", index);
    static atomic_int to_send;
    for (int i = 0; i < 5; i++) {
        int n = to_send++;
        printf("Writer thread %d: enqueuing %dth pointer.\n", index, n);
        enqueue(&addresses[n]);
    }
    printf("Writer thread %d: finished.\n", index);
    return thrd_success;
}

static void test_reader_starvation(void) {
    printf("Reader Starvation Test\n");
    printf("======================\n");
    printf("Initializing queue.\n");
    initQueue();
    thrd_t reader_ids[5];
    thrd_t writer_ids[5];
    for (int i = 0; i < 5; i++) {
        thrd_create(&reader_ids[i], test_reader_starvation_reader, (void*)(uintptr_t)i);
    }
    for (int i = 0; i < 100; i++) thrd_yield();
    if (visited() != 0) {
        printf("visited() should return zero.\n");
        exit(1);
    }
    for (int i = 0; i < 5; i++) {
        thrd_create(&writer_ids[i], test_reader_starvation_writer, (void*)(uintptr_t)i);
    }
    for (int i = 0; i < 5; i++) {
        int thread_return;
        thrd_join(reader_ids[i], &thread_return);
        if (thread_return != thrd_success) {
            printf("Failed to join reader %d.\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < 5; i++) {
        int thread_return;
        thrd_join(writer_ids[i], &thread_return);
        if (thread_return != thrd_success) {
            printf("Failed to join writer %d.\n", i);
            exit(1);
        }
    }
    if (visited() != 25) {
        printf("visited() should return 25.\n");
        exit(1);
    }
    for (int i = 0; i < 25; i++) {
        int count = test_reader_starvation_counters[i];
        if (count == 0) {
            printf("The %dth pointer was never dequeued.\n", i);
            exit(1);
        }
        if (count > 1) {
            printf("The %dth pointer was dequeued more than once.\n", i);
        }
    }
    printf("All pointers have been successfully dequeued.\n");
    printf("Destroying queue.\n");
    destroyQueue();
    printf("\n");
}

atomic_char test_write_first_counters[25];

static int test_write_first_reader(void *input) {
    int index = (int)(uintptr_t)input;
    printf("Reader thread %d: started.\n", index);
    for (int i = 0; i < 5; i++) {
        printf("Reader thread %d: dequeuing.\n", index);
        void *ptr = dequeue();
        if ((uintptr_t)ptr < (uintptr_t)addresses ||
            (uintptr_t)ptr > (uintptr_t)&addresses[25])
        {
            printf("Reader thread %d: dequeued pointer was out of range.\n", index);
            exit(1);
        }
        int n = (char*)ptr - addresses;
        printf("Reader thread %d: dequeued the %dth pointer.\n", index, n);
        test_write_first_counters[n]++;
    }
    printf("Reader thread %d: finished.\n", index);
    return thrd_success;
}

static int test_write_first_writer(void *input) {
    int index = (int)(uintptr_t)input;
    printf("Writer thread %d: started.\n", index);
    static atomic_int to_send;
    for (int i = 0; i < 5; i++) {
        int n = to_send++;
        printf("Writer thread %d: enqueuing %dth pointer.\n", index, n);
        enqueue(&addresses[n]);
    }
    printf("Writer thread %d: finished.\n", index);
    return thrd_success;
}

static void test_write_first(void) {
    printf("Write-First Test\n");
    printf("======================\n");
    printf("Initializing queue.\n");
    initQueue();
    if (visited() != 0) {
        printf("visited() should return zero.\n");
        exit(1);
    }
    thrd_t reader_ids[5];
    thrd_t writer_ids[5];
    for (int i = 0; i < 5; i++) {
        thrd_create(&writer_ids[i], test_write_first_writer, (void*)(uintptr_t)i);
    }
    for (int i = 0; i < 100; i++) thrd_yield();
    for (int i = 0; i < 5; i++) {
        thrd_create(&reader_ids[i], test_write_first_reader, (void*)(uintptr_t)i);
    }
    for (int i = 0; i < 5; i++) {
        int thread_return;
        thrd_join(reader_ids[i], &thread_return);
        if (thread_return != thrd_success) {
            printf("Failed to join reader %d.\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < 5; i++) {
        int thread_return;
        thrd_join(writer_ids[i], &thread_return);
        if (thread_return != thrd_success) {
            printf("Failed to join writer %d.\n", i);
            exit(1);
        }
    }
    if (visited() != 25) {
        printf("visited() should return 25.\n");
        exit(1);
    }
    for (int i = 0; i < 25; i++) {
        int count = test_write_first_counters[i];
        if (count == 0) {
            printf("The %dth pointer was never dequeued.\n", i);
            exit(1);
        }
        if (count > 1) {
            printf("The %dth pointer was dequeued more than once.\n", i);
        }
    }
    printf("All pointers have been successfully dequeued.\n");
    printf("Destroying queue.\n");
    destroyQueue();
    printf("\n");
}

static atomic_int test_order_turn;

const struct timespec one_hundred_ms = {
    .tv_nsec = 100 * 1000 * 1000
};

static int test_order_reader(void *input) {
    int index = (int)(uintptr_t)input;
    printf("Reader thread %d: started.\n", index);
    while (test_order_turn != index) thrd_yield();
    thrd_sleep(&one_hundred_ms, NULL);
    printf("Reader thread %d: dequeuing.\n", index);
    test_order_turn++;
    int n = (char*)dequeue() - addresses;
    if (n != index) {
        printf("Reader thread %d: expected %dth pointer, got %dth pointer.\n", index, index, n);
        exit(1);
    }
    printf("Reader thread %d: finished.\n", index);
    return thrd_success;
}

static int test_order_writer(void *input) {
    int index = (int)(uintptr_t)input;
    printf("Writer thread %d: started.\n", index);
    while (test_order_turn != index + 20) thrd_yield();
    thrd_sleep(&one_hundred_ms, NULL);
    printf("Writer thread %d: enqueuing %dth pointer.\n", index, index);
    enqueue(&addresses[index]);
    test_order_turn++;
    printf("Writer thread %d: finished.\n", index);
    return thrd_success;
}

void test_order(void) {
    printf("Thread Order Test\n");
    printf("=================\n");
    printf("Initializing queue.\n");
    initQueue();
    thrd_t reader_ids[20];
    thrd_t writer_ids[25];
    for (int i = 0; i < 20; i++) {
        thrd_create(&reader_ids[i], test_order_reader, (void*)(uintptr_t)i);
    }
    while (test_order_turn != 20) thrd_yield();
    thrd_sleep(&one_hundred_ms, NULL);
    if (visited() != 0) {
        printf("visited() should return zero.\n");
        exit(1);
    }
    for (int i = 0; i < 25; i++) {
        thrd_create(&writer_ids[i], test_order_writer, (void*)(uintptr_t)i);
    }
    for (int i = 0; i < 20; i++) {
        int thread_return;
        thrd_join(reader_ids[i], &thread_return);
        if (thread_return != thrd_success) {
            printf("Failed to join reader %d.\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < 25; i++) {
        int thread_return;
        thrd_join(writer_ids[i], &thread_return);
        if (thread_return != thrd_success) {
            printf("Failed to join writer %d.\n", i);
            exit(1);
        }
    }
    void *ptr;
    for (int i = 20; i < 25; i++) {
        printf("Trying to dequeue; expecting to succeed.\n");
        assert(tryDequeue(&ptr));
        assert(ptr == &addresses[i]);
    }
    printf("Trying to dequeue; expecting to fail.\n");
    assert(!tryDequeue(&ptr));
    printf("Trying to dequeue; expecting to fail.\n");
    assert(!tryDequeue(&ptr));
    printf("Destroying queue.\n");
    destroyQueue();
    printf("\n");
}

int main() {
    test_single_threaded();
    test_reader_starvation();
    test_write_first();
    test_order();
    printf("All tests succeeded!\n");
}

