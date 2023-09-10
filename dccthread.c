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

#define QUANTUM_TIME_NSEC 10000000
#define ROUND_ROBIN_TIMER_SIGNAL SIGUSR1

#define SLEEP_TIMER_SIGNAL SIGUSR2

typedef struct dccthread{
	int id;
	char name[DCCTHREAD_MAX_NAME_SIZE];
	ucontext_t context;
    dccthread_t *waiting_for;
    int exit_state;
    timer_t sleep_timer_id;
} dccthread_t;

struct dlist *ready_list;
struct dlist *waiting_list;

dccthread_t *manager_thread;

int threads_counter = 0;

timer_t round_robin_timer_id;
struct itimerspec round_robin_timer_spec;
struct sigevent round_robin_timer_sigevent;
struct sigaction round_robin_sigaction;
sigset_t mask_signals_set;

struct sigaction sleep_sigaction;

static void round_robin_sigaction_handler(int signo, siginfo_t *info, void *context) {
    dccthread_yield();
}

int dccthread_dlist_check_thread_sleep (const void *e1, const void *e2, void *userdata) {
    dccthread_t *waiting_thread = (dccthread_t*) e1;
    timer_t *timer_id = (timer_t*) e2;

    return (waiting_thread->sleep_timer_id == *timer_id) ? 0 : 1;
}

static void sleep_sigaction_handler(int signo, siginfo_t *info, void *context) {
    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in sleep_sigaction_handler");
    }

    timer_t *timer_id = info->si_ptr;
    dccthread_t *was_sleeping = (dccthread_t*) dlist_find_remove(waiting_list, timer_id, 
        dccthread_dlist_check_thread_sleep, NULL);

    if (was_sleeping != NULL) {
        dlist_push_right(ready_list, was_sleeping);
    }

    dccthread_t *current_thread = dccthread_self();
    setcontext(&current_thread->context);

    if (sigprocmask(SIG_UNBLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot unblock signals in sleep_sigaction_handler");
    }
}

void dccthread_init(void (*func)(int), int param) {
    ready_list = dlist_create();
    waiting_list = dlist_create();

    round_robin_timer_sigevent.sigev_notify = SIGEV_SIGNAL;
    round_robin_timer_sigevent.sigev_signo = ROUND_ROBIN_TIMER_SIGNAL;

    if (timer_create(CLOCK_PROCESS_CPUTIME_ID, &round_robin_timer_sigevent, &round_robin_timer_id) == -1) {
        handle_error("Cannot create timer to force thread yield");
    }

    round_robin_timer_spec.it_value.tv_nsec = QUANTUM_TIME_NSEC;

    round_robin_sigaction.sa_flags = SA_SIGINFO;
    round_robin_sigaction.sa_sigaction = round_robin_sigaction_handler;

    if (sigaction(ROUND_ROBIN_TIMER_SIGNAL, &round_robin_sigaction, NULL) == -1) {
        handle_error("Cannot set action for thread yield timer signal");
    }

    sleep_sigaction.sa_flags = SA_SIGINFO;
    sleep_sigaction.sa_sigaction = sleep_sigaction_handler;

    if (sigaction(SLEEP_TIMER_SIGNAL, &sleep_sigaction, NULL) == -1) {
        handle_error("Cannot set action for sleep thread");
    }

    if (sigemptyset(&mask_signals_set) == -1) {
        handle_error("Cannot initiate an empty mask signals set");
    }

    if (sigaddset(&mask_signals_set, ROUND_ROBIN_TIMER_SIGNAL) == -1) {
        handle_error("Cannot add thread yield timer signal to mask signals set");
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

    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in manager thread");
    }
    
    while (!dlist_empty(ready_list) || !dlist_empty(waiting_list)) {
        if (dlist_empty(ready_list)) {
            continue;
        }

        dccthread_t *next_thread = (dccthread_t*) malloc(sizeof(dccthread_t));
        next_thread = ready_list->head->data;

        if (timer_settime(round_robin_timer_id, 0, &round_robin_timer_spec, NULL) == -1) {
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

    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in dccthread_create");
    }

    new_thread->context.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    new_thread->context.uc_stack.ss_size = THREAD_STACK_SIZE;
    new_thread->context.uc_link = &manager_thread->context;

    makecontext(&new_thread->context, (void(*)(void))func, 1, param);

    dlist_push_right(ready_list, new_thread);

    if (sigprocmask(SIG_UNBLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot unblock signals in dccthread_create");
    }

    return new_thread;
}

dccthread_t * dccthread_self(void) {
    return ready_list->head->data;
}

const char * dccthread_name(dccthread_t *tid) {
    return tid->name;
}

void dccthread_yield(void) {
    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in dccthread_yield");
    }

    dccthread_t *current_thread = dccthread_self();

    dlist_push_right(ready_list, current_thread);

    if (swapcontext(&current_thread->context, &manager_thread->context) == -1) {
        handle_error("Cannot swap context from current to manager thread");
    }

    if (sigprocmask(SIG_UNBLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot unblock signals in dccthread_yield");
    }
}

void dccthread_wait(dccthread_t *tid) {
    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in dccthread_wait");
    }

    dccthread_t *current_thread = dccthread_self();

    if (tid->exit_state != 0) {
        return;
    }
    
    current_thread->waiting_for = tid;
    dlist_push_right(waiting_list, current_thread);

    if (swapcontext(&current_thread->context, &manager_thread->context) == -1) {
        handle_error("Cannot swap context from current to manager thread when waiting for other thread");
    }

    if (sigprocmask(SIG_UNBLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot unblock signals in dccthread_wait");
    }
}

int dccthread_dlist_check_thread_waiting_for_other (const void *e1, const void *e2, void *userdata) {
    dccthread_t *waiting_thread = (dccthread_t*) e1;
    dccthread_t *exiting_thread = (dccthread_t*) e2;

    return (waiting_thread->waiting_for == exiting_thread) ? 0 : 1;
}

void dccthread_exit(void) {
    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in dccthread_exit");
    }

    dccthread_t *current_thread = dccthread_self();
    current_thread->exit_state = 1;

    dccthread_t *was_waiting = (dccthread_t*) dlist_find_remove(waiting_list, current_thread, 
        dccthread_dlist_check_thread_waiting_for_other, NULL);

    if (was_waiting != NULL) {
        dlist_push_right(ready_list, was_waiting);
    }

    setcontext(&manager_thread->context);

    if (sigprocmask(SIG_UNBLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot unblock signals in dccthread_exit");
    }
}

void dccthread_sleep(struct timespec ts) {
    if (sigprocmask(SIG_BLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot block signals in dccthread_sleep");
    }

    dccthread_t *current_thread = dccthread_self();

    struct itimerspec sleep_timer_spec;
    struct sigevent sleep_timer_sigevent;

    sleep_timer_sigevent.sigev_notify = SIGEV_SIGNAL;
    sleep_timer_sigevent.sigev_signo = SLEEP_TIMER_SIGNAL;
    sleep_timer_sigevent.sigev_value.sival_ptr = &current_thread->sleep_timer_id;

    if (timer_create(CLOCK_REALTIME, &sleep_timer_sigevent, &current_thread->sleep_timer_id) == -1) {
        handle_error("Cannot create sleep timer");
    }

    sleep_timer_spec.it_value = ts;

    if (timer_settime(current_thread->sleep_timer_id, 0, &sleep_timer_spec, NULL) == -1) {
        handle_error("Cannot set timer to sleep thread");
    }

    dlist_push_right(waiting_list, current_thread);

    if (swapcontext(&current_thread->context, &manager_thread->context) == -1) {
        handle_error("Cannot swap context from current to manager thread when waiting sleep thread");
    }

    if (sigprocmask(SIG_UNBLOCK, &mask_signals_set, NULL) == -1) {
        handle_error("Cannot unblock signals in dccthread_sleep");
    }
}
