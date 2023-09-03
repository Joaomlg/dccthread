#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#include "dccthread.h"

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

static ucontext_t uctx_main, uctx_func;

void dccthread_init(void (*func)(int), int param) {
    char func_stack[THREAD_STACK_SIZE];

    if (getcontext(&uctx_func) == -1) {
        handle_error("getcontext");
    }

    uctx_func.uc_stack.ss_sp = func_stack;
    uctx_func.uc_stack.ss_size = sizeof(func_stack);
    uctx_func.uc_link = &uctx_main;

    makecontext(&uctx_func, (void(*)(void))func, 1, param);
    
    if (swapcontext(&uctx_main, &uctx_func) == -1) {
        handle_error("swapcontext");
    }
    
    exit(EXIT_SUCCESS);
}