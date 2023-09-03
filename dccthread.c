#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>

#include "dccthread.h"
#include "dlist.h"

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

typedef struct dccthread{
	int id;
	char name[DCCTHREAD_MAX_NAME_SIZE];
	ucontext_t context;
} dccthread_t;

struct dlist *ready_list;

dccthread_t *manager_thread;

int threads_counter = 0;

void dccthread_init(void (*func)(int), int param) {
    ready_list = dlist_create();

    manager_thread = (dccthread_t*) malloc(sizeof(dccthread_t));

    if (getcontext(&manager_thread->context) == -1) {
        handle_error("Cannot get context to manager thread");
    }

    manager_thread->context.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    manager_thread->context.uc_stack.ss_size = THREAD_STACK_SIZE;
    manager_thread->context.uc_link = NULL;

    strcpy(manager_thread->name, "manager");

    dccthread_create("main", func, param);
    
    while (!dlist_empty(ready_list)) {
        dccthread_t *next_thread = (dccthread_t*) malloc(sizeof(dccthread_t));
        next_thread = (dccthread_t*) dlist_pop_left(ready_list);

        if (swapcontext(&manager_thread->context, &next_thread->context) == -1) {
            handle_error("Cannot swap context from manager to next thread");
        }
    }
    
    exit(EXIT_SUCCESS);
}

dccthread_t * dccthread_create(const char *name, void (*func)(int ), int param) {
    dccthread_t *new_thread;
    new_thread = (dccthread_t*) malloc(sizeof(dccthread_t));

    new_thread->id = threads_counter++;
    strcpy(new_thread->name, name);

    if (getcontext(&new_thread->context) == -1) {
        handle_error("Cannot get context to create a new thread");
    }

    new_thread->context.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    new_thread->context.uc_stack.ss_size = THREAD_STACK_SIZE;
    new_thread->context.uc_link = &manager_thread->context;

    makecontext(&new_thread->context, (void(*)(void))func, 1, param);

    dlist_push_right(ready_list, new_thread);

    return new_thread;
}