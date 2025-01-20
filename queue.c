#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>

// Node structure for the queue
struct Node {
    void *data;
    struct Node *next;
};

// Queue structure with synchronization variables
static struct {
    struct Node *head;
    struct Node *tail;
    size_t count;
    atomic_size_t visited_count;

    mtx_t lock;
    cnd_t not_empty;

    // For FIFO wakeup order of waiting threads
    size_t next_ticket;
    size_t current_ticket;
    size_t wait_count;
} queue;

// Initialize the queue
void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    queue.count = 0;
    atomic_init(&queue.visited_count, 0);
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.not_empty);
    queue.next_ticket = 0;
    queue.current_ticket = 0;
    queue.wait_count = 0;
}

// Destroy the queue and clean up resources
void destroyQueue(void) {
    mtx_lock(&queue.lock);
    struct Node *current = queue.head;
    while (current != NULL) {
        struct Node *next = current->next;
        free(current);
        current = next;
    }
    queue.head = NULL;
    queue.tail = NULL;
    queue.count = 0;
    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
    cnd_destroy(&queue.not_empty);
}

// Enqueue an item
void enqueue(void *item) {
    struct Node *new_node = malloc(sizeof(struct Node));
    new_node->data = item;
    new_node->next = NULL;

    mtx_lock(&queue.lock);
    if (queue.tail == NULL) {
        queue.head = new_node;
    } else {
        queue.tail->next = new_node;
    }
    queue.tail = new_node;
    queue.count++;

    // Wake all waiting threads to check if they can proceed
    if (queue.wait_count > 0) {
        cnd_broadcast(&queue.not_empty);
    }
    mtx_unlock(&queue.lock);
}

// Dequeue an item (blocking)
void* dequeue(void) {
    mtx_lock(&queue.lock);

    // Fast path: queue is not empty and no waiters
    if (queue.count > 0 && queue.wait_count == 0) {
        struct Node *old_head = queue.head;
        void *data = old_head->data;
        queue.head = old_head->next;
        if (queue.head == NULL) queue.tail = NULL;
        queue.count--;
        atomic_fetch_add(&queue.visited_count, 1);
        free(old_head);
        mtx_unlock(&queue.lock);
        return data;
    }

    // Wait for an item, following FIFO order
    const size_t my_ticket = queue.next_ticket++;
    queue.wait_count++;

    while (queue.count == 0 || queue.current_ticket != my_ticket) {
        cnd_wait(&queue.not_empty, &queue.lock);
    }

    // Dequeue the item
    struct Node *old_head = queue.head;
    void *data = old_head->data;
    queue.head = old_head->next;
    if (queue.head == NULL) queue.tail = NULL;
    queue.count--;
    atomic_fetch_add(&queue.visited_count, 1);
    free(old_head);

    // Update state for next waiter
    queue.current_ticket++;
    queue.wait_count--;

    mtx_unlock(&queue.lock);
    return data;
}

// Try to dequeue without blocking
bool tryDequeue(void **output) {
    mtx_lock(&queue.lock);
    bool success = false;

    if (queue.count > 0 && queue.wait_count == 0) {
        struct Node *old_head = queue.head;
        *output = old_head->data;
        queue.head = old_head->next;
        if (queue.head == NULL) queue.tail = NULL;
        queue.count--;
        atomic_fetch_add(&queue.visited_count, 1);
        free(old_head);
        success = true;
    }

    mtx_unlock(&queue.lock);
    return success;
}

// Get the number of visited items (atomic and lock-free)
size_t visited(void) {
    return atomic_load(&queue.visited_count);
}