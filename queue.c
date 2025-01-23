#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <threads.h>
#include <stdio.h>  // Added for perror


// Queue Node Definition
typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    Node* tail;
    atomic_size_t visited_count; // Tracks visited items
    atomic_size_t size;          // Tracks current queue size
    size_t current_turn;
    mtx_t mutex;
    cnd_t cond;
} Queue;

// Global queue instance
static Queue queue;

// Initialize the queue
void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    atomic_init(&queue.visited_count, 0);
    atomic_init(&queue.size, 0);
    queue.current_turn = 0;
    mtx_init(&queue.mutex, mtx_plain);
    cnd_init(&queue.cond);
}

// Destroy the queue
void destroyQueue(void) {
    mtx_lock(&queue.mutex);
    Node* current = queue.head;
    while (current != NULL) {
        Node* temp = current;
        current = current->next;
        free(temp);
    }
    queue.head = NULL;
    queue.tail = NULL;
    mtx_unlock(&queue.mutex);
    mtx_destroy(&queue.mutex);
    cnd_destroy(&queue.cond);
}

// Enqueue an item
void enqueue(void* item) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    if (!new_node) {
        perror("Failed to allocate memory for new node");
        exit(EXIT_FAILURE);
    }
    new_node->data = item;
    new_node->next = NULL;

    mtx_lock(&queue.mutex);

    if (queue.tail != NULL) {
        queue.tail->next = new_node;
    } else {
        queue.head = new_node;
    }
    queue.tail = new_node;
    atomic_fetch_add(&queue.size, 1);

    cnd_signal(&queue.cond);
    mtx_unlock(&queue.mutex);
}

// Dequeue an item (blocking)
void* dequeue(void) {
    mtx_lock(&queue.mutex);

    while (queue.head == NULL) {
        cnd_wait(&queue.cond, &queue.mutex);
    }

    Node* node = queue.head;
    queue.head = node->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }

    void* data = node->data;
    free(node);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited_count, 1);
    queue.current_turn++;

    mtx_unlock(&queue.mutex);
    return data;
}

// Attempt to dequeue an item without blocking
bool tryDequeue(void** item) {
    mtx_lock(&queue.mutex);

    if (queue.head == NULL) {
        mtx_unlock(&queue.mutex);
        return false;
    }

    Node* node = queue.head;
    queue.head = node->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }

    *item = node->data;
    free(node);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited_count, 1);
    queue.current_turn++;

    mtx_unlock(&queue.mutex);
    return true;
}

// Get the total number of visited items
size_t visited(void) {
    return atomic_load(&queue.visited_count);
}

// Get the current size of the queue
size_t size(void) {
    return atomic_load(&queue.size);
}
