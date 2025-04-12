#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "routine.h"
#include "thread_tool.h"
// 定義 sleeping_set
struct sleeping_set sleeping_set = {
    .threads ={{0}},
    .size = 0
};

void sighandler(int signum) {
    // 根據規格要求輸出信號捕獲訊息
    if (signum == SIGTSTP) {
        printf("caught SIGTSTP\n");
    } else if (signum == SIGALRM) {
        printf("caught SIGALRM\n");
    }
    
    // 跳轉到調度器
    longjmp(sched_buf, 1);
}

void clearsig(){
    struct sigaction sa;
    // 設置為 SIG_IGN
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);

    // 恢復為我們的 handler
    sa.sa_handler = sighandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);
}

void scheduler() {
    thread_create(idle, 0, NULL);

    int val = setjmp(sched_buf);
    //printf("DEBUG: scheduler called with val = %d\n", val);
    //printf("DEBUG: ready_queue size = %d\n", ready_queue.size);
    /*// 第一次調用時的初始化
    if (val == 0) {
        // 創建閒置線程
        current_thread = (struct tcb*)malloc(sizeof(struct tcb));
        thread_create(idle, 0, NULL);
        
        // 保存調度器上下文
        if (setjmp(sched_buf) == 0) {
            return;
        }
    }*/
    clearsig();
   
    // 1. 重置 alarm
    alarm(0);  // 取消之前的 alarm
    alarm(time_slice);  // 設置新的 alarm
    
    // 2. 清除待處理信號
    //clearsig();
    
    // 3. 管理睡眠線程
    if(val == 1){
        for (int i = 0; i < sleeping_set.size; i++) {
            sleeping_set.threads[i].thread->sleeping_time -= time_slice;
            if (sleeping_set.threads[i].thread->sleeping_time <= 0) {
                // 將線程移到就緒隊列
                int idx = (ready_queue.head + ready_queue.size) % THREAD_MAX;
                ready_queue.arr[idx] = sleeping_set.threads[i].thread;
                ready_queue.size++;
                
                // 從睡眠集合中移除
                for (int j = i; j < sleeping_set.size - 1; j++) {
                    sleeping_set.threads[j] = sleeping_set.threads[j + 1];
                }
                sleeping_set.size--;
                i--;  // 調整索引以處理移動後的元素
            }
        }
    }
    
    
    // 4. 處理等待線程
    while (waiting_queue.size > 0) {
        struct tcb* thread = waiting_queue.arr[waiting_queue.head];
        bool can_proceed = false;
        
        if (thread->waiting_for == 1) {  // 等待讀鎖
            can_proceed = (rwlock.write_count == 0);
        } else if (thread->waiting_for == 2) {  // 等待寫鎖
            can_proceed = (rwlock.write_count == 0 && rwlock.read_count == 0);
        }
        
        if (can_proceed) {
            // 移到就緒隊列
            int idx = (ready_queue.head + ready_queue.size) % THREAD_MAX;
            ready_queue.arr[idx] = thread;
            ready_queue.size++;
            
            // 從等待隊列移除
            waiting_queue.head = (waiting_queue.head + 1) % THREAD_MAX;
            waiting_queue.size--;
        } else {
            break;
        }
    }
    
    // 5. 處理之前運行的線程
    if (current_thread != idle_thread) {
    // 根據 setjmp 的返回值判斷線程來源
    // val=1: 來自信號處理
    // val=2: 來自 read_lock/write_lock
    // val=3: 來自 thread_sleep
    // val=4: 來自 thread_exit
        switch (val) {
            case 1: // 從 signal handler
                int idx = (ready_queue.head + ready_queue.size) % THREAD_MAX;
                ready_queue.arr[idx] = current_thread;
                ready_queue.size++;
                break;
            case 2: // 從 lock
                //放到waiting queue
                idx = (waiting_queue.head + waiting_queue.size) % THREAD_MAX;
                waiting_queue.arr[idx] = current_thread;
                waiting_queue.size++;
                break;
            case 3: // 從 sleep
                // 已經在 thread_sleep 中加到 sleeping_set
                break;
            case 4: // 從 exit
                free(current_thread->args);
                free(current_thread);
                break;
        }
    }
    /*printf("scheduler: ready_queue size = %d\n", ready_queue.size);
    printf("scheduler: sleeping_set size = %d\n", sleeping_set.size);
    printf("scheduler: current_thread id = %d\n", current_thread->id);*/
    // 6. 選擇下一個要運行的線程
    if (ready_queue.size > 0) {
        //printf("DEBUG: switching to thread %d\n", ready_queue.arr[ready_queue.head]->id);
        //printf("000\n");
        current_thread = ready_queue.arr[ready_queue.head];
        ready_queue.head = (ready_queue.head + 1) % THREAD_MAX;
        //printf("DEBUG: ready queue head thread %d\n", ready_queue.arr[ready_queue.head]->id);
        ready_queue.size--;
    } else if (sleeping_set.size > 0) {
        //printf("111\n");
        // 如果還有睡眠線程，運行閒置線程
        current_thread = idle_thread;
    } else {
        //printf("222\n");
        // 清理閒置線程並返回
        free(idle_thread);
        return;
    }
    
    // 7. 進行上下文切換
    //printf("DEBUG: switching to thread %d\n", current_thread->id);
    longjmp(current_thread->env, 1);
}
/*// TODO::
// Prints out the signal you received.
// This function should not return. Instead, jumps to the scheduler.
void sighandler(int signum) {
    // Your code here
}

// TODO::
// Perfectly setting up your scheduler.
void scheduler() {
    // Your code here
}*/
