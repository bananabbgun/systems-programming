#ifndef THREAD_TOOL_H
#define THREAD_TOOL_H

#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

// The maximum number of threads.
#define THREAD_MAX 100

void sighandler(int signum);
void scheduler();

// The thread control block structure.
struct tcb {
    int id;
    int *args;
    // Reveals what resource the thread is waiting for. The values are:
    //  - 0: no resource.
    //  - 1: read lock.
    //  - 2: write lock.
    int waiting_for;
    int sleeping_time;
    jmp_buf env;  // Where the scheduler should jump to.
    int n, i, f_cur, f_prev; // Variables to keep between switches.
    int dp, ds, s, b;
    int p_p, p_s;
};

// The only one thread in the RUNNING state.
extern struct tcb *current_thread;
extern struct tcb *idle_thread;

struct tcb_queue {
    struct tcb *arr[THREAD_MAX];  // The circular array.
    int head;                     // The index of the head of the queue
    int size;
};

extern struct tcb_queue ready_queue, waiting_queue;

// The rwlock structure.
struct rwlock {
    int read_count;
    int write_count;
};

extern struct rwlock rwlock;

// Structure for sleeping threads
struct sleeping_thread {
    struct tcb *thread;
};

struct sleeping_set {
    struct sleeping_thread threads[THREAD_MAX];
    int size;
};

extern struct sleeping_set sleeping_set;

// The remaining spots in classes.
extern int q_p, q_s;

// The maximum running time for each thread.
extern int time_slice;

// The long jump buffer for the scheduler.
extern jmp_buf sched_buf;

#define thread_create(func, t_id, t_args)                                              \
    ({                                                                                 \
        func(t_id, t_args);                                                           \
    })

#define thread_setup(t_id, t_args)                                           \
    ({                                                                                                                                         \
        printf("thread %d: set up routine %s\n", t_id, __func__);          \
        current_thread = (struct tcb*)malloc(sizeof(struct tcb));   \
        current_thread->id = t_id;                                      \
        current_thread->args = t_args;                                  \
        current_thread->waiting_for = 0;                                \
        current_thread->sleeping_time = 0;                              \
        current_thread->n = 0;                                          \
        if (t_id == 0) {                                                \
            idle_thread = current_thread;                               \
        } else {                                                        \
            int idx = (ready_queue.head + ready_queue.size) % THREAD_MAX; \
            ready_queue.arr[idx] = current_thread;                      \
            ready_queue.size++;                     \
        }                                                               \
        if (setjmp(current_thread->env) == 0) {                           \
            return;                                                         \
        }                                                                   \
    })

#define thread_yield()                                  \
    ({                                                  \
        if (setjmp(current_thread->env) == 0) {         \
            sigset_t mask_tstp, mask_alrm;              \
            sigemptyset(&mask_tstp);                    \
            sigaddset(&mask_tstp, SIGTSTP);             \
            sigprocmask(SIG_UNBLOCK, &mask_tstp, NULL); \
            sigprocmask(SIG_BLOCK, &mask_tstp, NULL);   \
            sigemptyset(&mask_alrm);                    \
            sigaddset(&mask_alrm, SIGALRM);             \
            sigprocmask(SIG_UNBLOCK, &mask_alrm, NULL); \
            sigprocmask(SIG_BLOCK, &mask_alrm, NULL);   \
        }                                               \
    })

#define read_lock()                                                      \
    ({                                                                   \
        if(rwlock.write_count > 0) {                                    \
            current_thread->waiting_for = 1;                            \
            if(setjmp(current_thread->env) == 0)                        \
                longjmp(sched_buf, 2);                                  \
        }                                                               \
        rwlock.read_count++;                                           \
    })

#define write_lock()                                                     \
    ({                                                                   \
        if(rwlock.read_count > 0 || rwlock.write_count > 0) {          \
            current_thread->waiting_for = 2;                            \
            if(setjmp(current_thread->env) == 0)                        \
                longjmp(sched_buf, 2);                                  \
        }                                                               \
        rwlock.write_count++;                                          \
    })

#define read_unlock()                                                                 \
    ({                                                                                \
        rwlock.read_count--;                                                         \
    })

#define write_unlock()                                                                \
    ({                                                                                \
        rwlock.write_count--;                                                        \
    })

#define thread_sleep(sec)                                             \
    ({                                                                \
        if (sleeping_set.size >= THREAD_MAX) {                        \
            fprintf(stderr, "Error: Sleeping set overflow.\n");       \
            exit(EXIT_FAILURE);                                       \
        }                                                             \
        current_thread->sleeping_time = sec;                          \
        int idx = sleeping_set.size;                                  \
        sleeping_set.threads[idx].thread = current_thread;             \
        sleeping_set.size++;                                          \
        if (setjmp(current_thread->env) == 0)                         \
            longjmp(sched_buf, 3);                                    \
    })

#define thread_awake(t_id)                                                        \
    ({                                                                            \
        for(int i = 0; i < sleeping_set.size; i++) {                             \
            if(sleeping_set.threads[i].thread && sleeping_set.threads[i].thread->id == t_id) {                      \
                int idx = (ready_queue.head + ready_queue.size) % THREAD_MAX;     \
                ready_queue.arr[idx] = sleeping_set.threads[i].thread;            \
                ready_queue.size++;                                               \
                for(int j = i; j < sleeping_set.size - 1; j++) {                 \
                    sleeping_set.threads[j] = sleeping_set.threads[j + 1];        \
                }                                                                 \
                sleeping_set.size--;                                             \
                break;                                                           \
            }                                                                    \
        }                                                                        \
    })

#define thread_exit()                                    \
    ({                                                   \
        printf("thread %d: exit\n", current_thread->id); \
        longjmp(sched_buf, 4);                          \
    })

#endif  // THREAD_TOOL_H