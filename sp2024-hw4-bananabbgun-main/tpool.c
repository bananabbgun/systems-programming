#include "tpool.h"
#include <stdlib.h>
#include <stdio.h>

// 佇列操作的輔助函式
static void queue_init(struct queue* q) {
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void queue_destroy(struct queue* q) {
    if (!q) return;
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static void frontend_queue_push(struct queue* q, struct request* req) {
    if (!q || !req) return;
    
    pthread_mutex_lock(&q->mutex);
    req->next = NULL;
    if (q->tail == NULL) {
        q->head = q->tail = req;
    } else {
        ((struct request*)q->tail)->next = req;
        q->tail = req;
    }
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

static struct request* frontend_queue_pop(struct queue* q, struct tpool* pool) {
    if (!q || !pool) return NULL;
    
    pthread_mutex_lock(&q->mutex);
    while (q->head == NULL && !pool->stop) {
        //fprintf(stderr, "Frontend: Waiting for new request...\n");
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    
    if (q->head == NULL) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    
    struct request* req = q->head;
    q->head = req->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    pthread_mutex_unlock(&q->mutex);
    return req;
}

static void worker_queue_push(struct queue* q, struct work_node* work) {
    if (!q || !work) return;
    
    pthread_mutex_lock(&q->mutex);
    work->next = NULL;
    if (q->tail == NULL) {
        q->head = q->tail = work;
    } else {
        ((struct work_node*)q->tail)->next = work;
        q->tail = work;
    }
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

static struct work_node* worker_queue_pop(struct queue* q, struct tpool* pool) {
    if (!q || !pool) return NULL;
    
    pthread_mutex_lock(&q->mutex);
    while (q->head == NULL && !pool->stop) {
        //fprintf(stderr, "Backend: Waiting for work...\n");
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    
    if (q->head == NULL) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    
    struct work_node* work = q->head;
    q->head = work->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    pthread_mutex_unlock(&q->mutex);
    return work;
}

static void transpose_matrix(Matrix m, int n) {
    if (!m) return;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int temp = m[i][j];
            m[i][j] = m[j][i];
            m[j][i] = temp;
        }
    }
}

static void* frontend_worker(void* arg) {
    struct tpool* pool = (struct tpool*)arg;
    
    while (!pool->stop) {
        struct request* req = frontend_queue_pop(&pool->frontend_queue, pool);
        if (!req || pool->stop) break;
        
        // 轉置矩陣 b
        transpose_matrix(req->b, pool->n);
        
        pthread_mutex_lock(&pool->mutex);
        int total_work = pool->n * pool->n;
        int work_per_unit = (total_work + req->num_works - 1) / req->num_works;
        pool->pending_works = req->num_works;
        pthread_mutex_unlock(&pool->mutex);
        
        // 創建work nodes
        for (int i = 0; i < total_work && i/work_per_unit < req->num_works; i += work_per_unit) {
            struct work_node* work = malloc(sizeof(struct work_node));
            if (!work) continue;
            
            work->work.a = req->a;
            work->work.b = req->b;
            work->work.c = req->c;
            work->work.st = i;
            work->work.ed = (i + work_per_unit < total_work) ? i + work_per_unit : total_work;
            
            worker_queue_push(&pool->worker_queue, work);
        }
        
        free(req);
    }
    
    return NULL;
}

static void* backend_worker(void* arg) {
    struct tpool* pool = (struct tpool*)arg;
    
    while (!pool->stop) {
        struct work_node* work = worker_queue_pop(&pool->worker_queue, pool);
        if (!work) {
            if (pool->stop) break;
            continue;
        }
        
        // 執行矩陣計算
        for (int idx = work->work.st; idx < work->work.ed; idx++) {
            int i = idx / pool->n;
            int j = idx % pool->n;
            if (i < pool->n && j < pool->n) {
                work->work.c[i][j] = calculation(pool->n, work->work.a[i], work->work.b[j]);
            }
        }
        
        pthread_mutex_lock(&pool->mutex);
        pool->pending_works--;
        if (pool->pending_works == 0) {
            pthread_cond_broadcast(&pool->work_done);
        }
        pthread_mutex_unlock(&pool->mutex);
        
        free(work);
    }
    
    return NULL;
}

struct tpool* tpool_init(int num_threads, int n) {
    //fprintf(stderr, "Initializing thread pool with %d threads, matrix size: %d\n", num_threads, n);
    
    struct tpool* pool = calloc(1, sizeof(struct tpool));
    if (!pool) {
        //fprintf(stderr, "Failed to allocate pool\n");
        return NULL;
    }
    
    pool->backend = calloc(num_threads, sizeof(pthread_t));
    if (!pool->backend) {
        //fprintf(stderr, "Failed to allocate backend threads array\n");
        free(pool);
        return NULL;
    }
    
    pool->num_threads = num_threads;
    pool->n = n;
    pool->stop = 0;
    pool->pending_works = 0;
    
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->work_done, NULL);
    
    queue_init(&pool->frontend_queue);
    queue_init(&pool->worker_queue);
    
    //fprintf(stderr, "Creating frontend thread\n");
    if (pthread_create(&pool->frontend, NULL, frontend_worker, pool) != 0) {
        //fprintf(stderr, "Failed to create frontend thread\n");
        free(pool->backend);
        free(pool);
        return NULL;
    }
    
    //fprintf(stderr, "Creating backend threads\n");
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->backend[i], NULL, backend_worker, pool) != 0) {
            //fprintf(stderr, "Failed to create backend thread %d\n", i);
            pool->stop = 1;
            pthread_cond_broadcast(&pool->frontend_queue.cond);
            pthread_cond_broadcast(&pool->worker_queue.cond);
            pthread_join(pool->frontend, NULL);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->backend[j], NULL);
            }
            queue_destroy(&pool->frontend_queue);
            queue_destroy(&pool->worker_queue);
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->work_done);
            free(pool->backend);
            free(pool);
            return NULL;
        }
    }
    
    //fprintf(stderr, "Thread pool initialized successfully\n");
    return pool;
}

void tpool_request(struct tpool* pool, Matrix a, Matrix b, Matrix c, int num_works) {
    //fprintf(stderr, "Received new request with num_works=%d\n", num_works);
    if (!pool || !a || !b || !c || num_works <= 0) {
        //fprintf(stderr, "Invalid parameters in tpool_request\n");
        return;
    }
    
    struct request* req = calloc(1, sizeof(struct request));
    if (!req) {
        //fprintf(stderr, "Failed to allocate request\n");
        return;
    }
    
    // 設置請求參數
    req->a = a;
    req->b = b;
    req->c = c;
    req->num_works = num_works;
    
    // 放入隊列前先確保矩陣 c 被初始化為 0
    for(int i = 0; i < pool->n; i++) {
        for(int j = 0; j < pool->n; j++) {
            c[i][j] = 0;
        }
    }
    
    frontend_queue_push(&pool->frontend_queue, req);
    //fprintf(stderr, "Request pushed to frontend queue\n");
}

void tpool_synchronize(struct tpool* pool) {
    //fprintf(stderr, "Synchronize called\n");
    if (!pool) return;
    
    pthread_mutex_lock(&pool->mutex);
    //fprintf(stderr, "Waiting for %d pending works\n", pool->pending_works);
    while (pool->pending_works > 0) {
        pthread_cond_wait(&pool->work_done, &pool->mutex);
    }
    //fprintf(stderr, "All works completed\n");
    pthread_mutex_unlock(&pool->mutex);
}

void tpool_destroy(struct tpool* pool) {
    if (!pool) return;
    
    tpool_synchronize(pool); // 確保所有工作完成
    
    pool->stop = 1;
    
    // 先喚醒frontend
    pthread_cond_signal(&pool->frontend_queue.cond);
    pthread_join(pool->frontend, NULL);
    
    // 再喚醒所有backend
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_cond_signal(&pool->worker_queue.cond);
    }
    
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->backend[i], NULL);
    }
    
    // 清理資源
    queue_destroy(&pool->frontend_queue);
    queue_destroy(&pool->worker_queue);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->work_done);
    
    free(pool->backend);
    free(pool);
}