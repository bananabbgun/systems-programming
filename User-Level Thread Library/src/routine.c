#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread_tool.h"

void idle(int id, int *args) {
    thread_setup(id, args);
    while (1) {
        printf("thread %d: idle\n", current_thread->id);
        sleep(1);
        thread_yield();
    }
}

void fibonacci(int id, int *args) {
    thread_setup(id, args);

    current_thread->n = current_thread->args[0];
    for (current_thread->i = 1;; current_thread->i++) {
        if (current_thread->i <= 2) {
            current_thread->f_cur = 1;
            current_thread->f_prev = 1;
        } else {
            int f_next = current_thread->f_cur + current_thread->f_prev;
            current_thread->f_prev = current_thread->f_cur;
            current_thread->f_cur = f_next;
        }
        printf("thread %d: F_%d = %d\n", current_thread->id, current_thread->i,
               current_thread->f_cur);

        sleep(1);

        if (current_thread->i == current_thread->n) {
            thread_exit();
        } else {
            thread_yield();
        }
    }
}

void pm(int id, int *args) {
    thread_setup(id, args);
    //printf("DEBUG: pm thread %d started with n = %d\n", id, args[0]);
    
    current_thread->n = current_thread->args[0];
    current_thread->f_cur = 1;  // 初始值為1
    
    for (current_thread->i = 1;; current_thread->i++) {
        //printf("DEBUG: thread %d iteration %d\n", current_thread->id, current_thread->i);
        if (current_thread->i == 1) {
            printf("thread %d: pm(%d) = %d\n", current_thread->id, current_thread->i, current_thread->f_cur);
        } else {
            current_thread->f_cur = (current_thread->i % 2 == 0 ? -1 : 1) * current_thread->i + current_thread->f_prev;
            printf("thread %d: pm(%d) = %d\n", current_thread->id, current_thread->i, current_thread->f_cur);
        }
        current_thread->f_prev = current_thread->f_cur;
        
        sleep(1);
        
        if (current_thread->i == current_thread->n) {
            thread_exit();
        } else {
            thread_yield();
        }
    }
}

void enroll(int id, int *args) {
    thread_setup(id, args);
    
    // Step 1: Sleep
    current_thread->dp = current_thread->args[0];
    current_thread->ds = current_thread->args[1];
    current_thread->s = current_thread->args[2];
    current_thread->b = current_thread->args[3];
    printf("thread %d: sleep %d\n", current_thread->id, current_thread->s);
    thread_sleep(current_thread->s);
    
    // Step 2: Wake up friend and read lock
    thread_awake(current_thread->b);
    read_lock();
    printf("thread %d: acquire read lock\n", current_thread->id);
    sleep(1);
    thread_yield();
    
    // Step 3: Release read lock and calculate priorities
    read_unlock();
    current_thread->p_p = current_thread->dp * q_p;
    current_thread->p_s = current_thread->ds * q_s;
    printf("thread %d: release read lock, p_p = %d, p_s = %d\n", current_thread->id, current_thread->p_p, current_thread->p_s);
    sleep(1);
    thread_yield();
    
    // Step 4: Write lock and enroll
    write_lock();
    const char* class_name;
    if (current_thread->p_p > current_thread->p_s || (current_thread->p_p == current_thread->p_s && current_thread->dp > current_thread->ds)) {
        // Enroll in PJ class if space available
        if (q_p > 0) {
            q_p--;
            class_name = "pj_class";
        } else {
            q_s--;
            class_name = "sw_class";
        }
    } else {
        // Enroll in SW class if space available
        if (q_s > 0) {
            q_s--;
            class_name = "sw_class";
        } else {
            q_p--;
            class_name = "pj_class";
        }
    }
    printf("thread %d: acquire write lock, enroll in %s\n", current_thread->id, class_name);
    sleep(1);
    thread_yield();
    
    // Step 5: Release write lock and exit
    write_unlock();
    printf("thread %d: release write lock\n", current_thread->id);
    sleep(1);
    thread_exit();
}

