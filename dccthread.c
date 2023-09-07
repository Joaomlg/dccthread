#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "dccthread.h"
#include "dlist.h"

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define THREAD_YIELD_TIME_NSEC 10000000
#define THREAD_YIELD_TIMER_SIGNAL SIGUSR1

typedef struct dccthread{
	int id;
	char name[DCCTHREAD_MAX_NAME_SIZE];
	ucontext_t context;
    dccthread_t *waiting_for;
    int exit_state;
} dccthread_t;

struct dlist *ready_list;
struct dlist *waiting_list;

dccthread_t *manager_thread;

int threads_counter = 0;

timer_t thread_yield_timer_id;
struct itimerspec thread_yield_timer_spec;
struct sigevent thread_yield_timer_sigevent;
struct sigaction thread_yield_sigaction;

static void thread_yield_sigaction_handler(int signo, siginfo_t *info, void *context) {
    dccthread_yield();
}

void dccthread_init(void (*func)(int), int param) {
    ready_list = dlist_create();
    waiting_list = dlist_create();

    thread_yield_timer_sigevent.sigev_notify = SIGEV_SIGNAL;
    thread_yield_timer_sigevent.sigev_signo = THREAD_YIELD_TIMER_SIGNAL;

    if (timer_create(CLOCK_PROCESS_CPUTIME_ID, &thread_yield_timer_sigevent, &thread_yield_timer_id) == -1) {
        handle_error("Cannot create timer to force thread yield");
    }

    thread_yield_timer_spec.it_value.tv_nsec = THREAD_YIELD_TIME_NSEC;

    thread_yield_sigaction.sa_flags = SA_SIGINFO;
    thread_yield_sigaction.sa_sigaction = thread_yield_sigaction_handler;

    if (sigaction(THREAD_YIELD_TIMER_SIGNAL, &thread_yield_sigaction, NULL) == -1) {
        handle_error("Cannot set action for thread yield timer signal");
    }

    manager_thread = (dccthread_t*) malloc(sizeof(dccthread_t));

    if (getcontext(&manager_thread->context) == -1) {
        handle_error("Cannot get context to manager thread");
    }

    manager_thread->id = -1;
    manager_thread->context.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    manager_thread->context.uc_stack.ss_size = THREAD_STACK_SIZE;
    manager_thread->context.uc_link = NULL;
    manager_thread->waiting_for = NULL;
    manager_thread->exit_state = 0;

    strcpy(manager_thread->name, "manager");

    dccthread_create("main", func, param);
    
    while (!dlist_empty(ready_list)) {
        dccthread_t *next_thread = (dccthread_t*) malloc(sizeof(dccthread_t));
        next_thread = ready_list->head->data;

        if (timer_settime(thread_yield_timer_id, 0, &thread_yield_timer_spec, NULL) == -1) {
            handle_error("Cannot set timer to yield thread");
        }

        if (swapcontext(&manager_thread->context, &next_thread->context) == -1) {
            handle_error("Cannot swap context from manager to next thread");
        }

        dlist_pop_left(ready_list);
    }
    
    exit(EXIT_SUCCESS);
}

dccthread_t * dccthread_create(const char *name, void (*func)(int ), int param) {
    dccthread_t *new_thread;
    new_thread = (dccthread_t*) malloc(sizeof(dccthread_t));

    new_thread->id = threads_counter++;
    manager_thread->waiting_for = NULL;
    manager_thread->exit_state = 0;

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

dccthread_t * dccthread_self(void) {
    return ready_list->head->data;
}

const char * dccthread_name(dccthread_t *tid) {
    return tid->name;
}

void dccthread_yield(void) {
    dccthread_t *current_thread = dccthread_self();
    dlist_push_right(ready_list, current_thread);
    if (swapcontext(&current_thread->context, &manager_thread->context) == -1) {
        handle_error("Cannot swap context from current to manager thread");
    }
}

void dccthread_wait(dccthread_t *tid) {
    dccthread_t *current_thread = dccthread_self();

    if (tid->exit_state != 0) {
        return;
    }
    
    current_thread->waiting_for = tid;
    dlist_push_right(waiting_list, current_thread);

    if (swapcontext(&current_thread->context, &manager_thread->context) == -1) {
        handle_error("Cannot swap context from current to manager thread when waiting for other thread");
    }
}

int dccthread_dlist_check_thread_waiting_for_other (const void *e1, const void *e2, void *userdata) {
    dccthread_t *waiting_thread = (dccthread_t*) e1;
    dccthread_t *exiting_thread = (dccthread_t*) e2;

    return (waiting_thread->waiting_for == exiting_thread) ? 0 : 1;
}

void dccthread_exit(void) {
    dccthread_t *current_thread = dccthread_self();
    current_thread->exit_state = 1;

    dccthread_t *was_waiting = (dccthread_t*) dlist_find_remove(waiting_list, current_thread, 
        dccthread_dlist_check_thread_waiting_for_other, NULL);

    if (was_waiting != NULL) {
        dlist_push_right(ready_list, was_waiting);
    }

    setcontext(&manager_thread->context);
}
