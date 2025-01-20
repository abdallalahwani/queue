#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>

// Node structure for the queue
struct Node {
    void *data;
    struct Node *next;
};

// Queue structure
static struct Queue {
    struct Node *head;
    struct Node *tail;
    size_t count;
    atomic_size_t visited_count;
    mtx_t lock;
    cnd_t not_empty;
    size_t next_seq; // Next sequence number for waiting threads
    size_t wait_count; // Number of threads waiting
} queue;

// Function to initialize the queue
void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    queue.count = 0;
    atomic_init(&queue.visited_count, 0);
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.not_empty);
    queue.next_seq = 0;
    queue.wait_count = 0;
}

// Function to destroy the queue
void destroyQueue(void) {
    struct Node *current = queue.head;
    struct Node *next;
    while (current != NULL) {
        next = current->next;
        free(current);
        current = next;
    }
    mtx_destroy(&queue.lock);
    cnd_destroy(&queue.not_empty);
}

// Function to enqueue an item
void enqueue(void *item) {
    struct Node *new_node = malloc(sizeof(struct Node));
    if (!new_node) {
        // Handle memory allocation failure
        return;
    }
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
    // Signal waiting threads if any
    if (queue.wait_count > 0) {
        cnd_signal(&queue.not_empty);
    }
    mtx_unlock(&queue.lock);
}

// Function to dequeue an item
void* dequeue(void) {
    void *data = NULL;
    size_t my_seq;
    mtx_lock(&queue.lock);
    while (queue.count == 0) {
        my_seq = queue.next_seq++;
        queue.wait_count++;
        // Wait until it's this thread's turn
        while (queue.count == 0 && my_seq != queue.next_seq - 1) {
            cnd_wait(&queue.not_empty, &queue.lock);
        }
        queue.wait_count--;
        if (queue.count == 0) {
            mtx_unlock(&queue.lock);
            return NULL; // Queue is empty after wait
        }
    }
    // Dequeue the item
    data = queue.head->data;
    struct Node *old_head = queue.head;
    queue.head = queue.head->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }
    queue.count--;
    atomic_fetch_add(&queue.visited_count, 1);
    free(old_head);
    mtx_unlock(&queue.lock);
    return data;
}

// Function to try to dequeue an item without waiting
bool tryDequeue(void **item) {
    bool success = false;
    mtx_lock(&queue.lock);
    if (queue.count > 0) {
        *item = queue.head->data;
        struct Node *old_head = queue.head;
        queue.head = queue.head->next;
        if (queue.head == NULL) {
            queue.tail = NULL;
        }
        queue.count--;
        atomic_fetch_add(&queue.visited_count, 1);
        free(old_head);
        success = true;
    }
    mtx_unlock(&queue.lock);
    return success;
}

// Function to get the number of visited items
size_t visited(void) {
    return atomic_load(&queue.visited_count);
}