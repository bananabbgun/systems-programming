#pragma once
#include <pthread.h>

typedef int** Matrix;
typedef int* Vector;

// 工作結構
struct work {
    Matrix a;
    Matrix b;
    Matrix c;
    int st;               // starting index
    int ed;               // ending index
};

// Frontend queue節點 (linked list)
struct request {
    Matrix a;
    Matrix b;
    Matrix c;
    int num_works;
    int completed_works;
    struct request* next;
};

// Worker queue節點 (linked list)
struct work_node {
    struct work work;
    struct work_node* next;
};

// 佇列結構 - 使用linked list實作
struct queue {
    void* head;           // 可以指向request或work_node
    void* tail;           // 可以指向request或work_node
    pthread_mutex_t mutex;
    pthread_cond_t cond;  // 用於通知queue不為空
};

// Thread pool結構
struct tpool {
    // 執行緒相關
    pthread_t frontend;              // frontend thread
    pthread_t* backend;              // backend threads array
    int num_threads;                 // number of backend threads
    int n;                          // matrix dimension

    // 佇列
    struct queue frontend_queue;     // 存放request的佇列
    struct queue worker_queue;       // 存放work的佇列
    
    // 同步相關
    int stop;               // 停止旗標
    int pending_works;               // 待完成工作數
    pthread_mutex_t mutex;           // 保護共享資料
    pthread_cond_t work_done;        // 用於同步等待工作完成
};


// 作業要求的函式
struct tpool* tpool_init(int num_threads, int n);
void tpool_request(struct tpool*, Matrix a, Matrix b, Matrix c, int num_works);
void tpool_synchronize(struct tpool*);
void tpool_destroy(struct tpool*);
int calculation(int n, Vector, Vector);
