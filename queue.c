#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    Node* tail;
    atomic_size_t visited_count; // Lock-free for visited()
    size_t count;                // Current items in the queue
    
    mtx_t mutex;
    cnd_t not_empty;
    
    // Ticket system for FIFO wakeup
    size_t next_ticket;    // Next ticket number to assign
    size_t current_ticket; // Current ticket being served
    size_t waiters;        // Number of waiting threads
} Queue;

static Queue queue;

void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    atomic_init(&queue.visited_count, 0);
    queue.count = 0;
    mtx_init(&queue.mutex, mtx_plain);
    cnd_init(&queue.not_empty);
    queue.next_ticket = 0;
    queue.current_ticket = 0;
    queue.waiters = 0;
}

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
    queue.count = 0;
    mtx_unlock(&queue.mutex);
    mtx_destroy(&queue.mutex);
    cnd_destroy(&queue.not_empty);
}

void enqueue(void* item) {
    Node* new_node = malloc(sizeof(Node));
    new_node->data = item;
    new_node->next = NULL;

    mtx_lock(&queue.mutex);
    if (queue.tail == NULL) {
        queue.head = new_node;
    } else {
        queue.tail->next = new_node;
    }
    queue.tail = new_node;
    queue.count++;
    
    // Wake all waiters to check their tickets
    if (queue.waiters > 0) {
        cnd_broadcast(&queue.not_empty);
    }
    mtx_unlock(&queue.mutex);
}

void* dequeue(void) {
    mtx_lock(&queue.mutex);
    const size_t my_ticket = queue.next_ticket++;
    queue.waiters++;
    
    // Wait until queue has items and it's this thread's turn
    while (queue.count == 0 || queue.current_ticket != my_ticket) {
        cnd_wait(&queue.not_empty, &queue.mutex);
    }
    
    Node* old_head = queue.head;
    void* data = old_head->data;
    queue.head = old_head->next;
    if (queue.head == NULL) queue.tail = NULL;
    queue.count--;
    atomic_fetch_add(&queue.visited_count, 1);
    free(old_head);
    
    queue.current_ticket++; // Advance to next waiter
    queue.waiters--;
    mtx_unlock(&queue.mutex);
    return data;
}

bool tryDequeue(void** output) {
    mtx_lock(&queue.mutex);
    bool success = false;
    
    // Only succeed if queue has items and no waiters
    if (queue.count > 0 && queue.waiters == 0) {
        Node* old_head = queue.head;
        *output = old_head->data;
        queue.head = old_head->next;
        if (queue.head == NULL) queue.tail = NULL;
        queue.count--;
        atomic_fetch_add(&queue.visited_count, 1);
        free(old_head);
        success = true;
    }
    
    mtx_unlock(&queue.mutex);
    return success;
}

size_t visited(void) {
    return atomic_load(&queue.visited_count);
}