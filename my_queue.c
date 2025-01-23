#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>


struct Node {
    void *data;
    struct Node *next;
};


static struct {
    struct Node *head;
    struct Node *tail;
    size_t count;
    atomic_size_t visited_count; 

    mtx_t lock;                  
    cnd_t not_empty;             
    
   
    size_t next_ticket;         
    size_t current_ticket;      
    size_t waiters;              
} queue;


void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    queue.count = 0;
    atomic_init(&queue.visited_count, 0);
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.not_empty);
    queue.next_ticket = 0;
    queue.current_ticket = 0;
    queue.waiters = 0;
}


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

 
    if (queue.waiters > 0) {
        cnd_broadcast(&queue.not_empty);
    }
    mtx_unlock(&queue.lock);
}


void* dequeue(void) {
    mtx_lock(&queue.lock);
    const size_t my_ticket = queue.next_ticket++;
    queue.waiters++;

    
    while (queue.count == 0 || queue.current_ticket != my_ticket) {
        cnd_wait(&queue.not_empty, &queue.lock);
    }

    
    struct Node *old_head = queue.head;
    void *data = old_head->data;
    queue.head = old_head->next;
    if (queue.head == NULL) queue.tail = NULL;
    queue.count--;
    atomic_fetch_add(&queue.visited_count, 1);
    free(old_head);

    
    queue.current_ticket++;
    queue.waiters--;
    mtx_unlock(&queue.lock);
    return data;
}


bool tryDequeue(void **output) {
    mtx_lock(&queue.lock);
    bool success = false;

    
    if (queue.count > 0 && queue.waiters == 0) {
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


size_t visited(void) {
    return atomic_load(&queue.visited_count);
}