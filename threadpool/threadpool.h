#ifndef THREADPOOL_H
#define THREADPOOL_H

/**
 * 半同步/半反应堆线程池(即：生产者消费者模式)
 * 使用一个工作队列完全解除了主线程和工作线程的耦合关系：主线程往工作队列中插入任务，工作线程通过竞争来取得任务并执行它。
 * > * 同步I/O模拟proactor模式
 * > * 半同步/半反应堆
 * > * 线程池
 **/
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
// 线程池的设计模式为半同步/半反应堆，其中反应堆具体为Proactor事件处理模式。
// 具体的: 主线程为异步线程，负责监听文件描述符，接收socket新连接，
// 若当前监听的socket发生了读写事件，然后将任务插入到请求队列。
// 工作线程从请求队列中取出任务，完成读写数据的处理。
template <typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool( int actor_model, 
                connection_pool* connPool,
                int thread_number = 8,
                int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); //向请求队列中插入任务请求
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之。C++中必须是静态函数*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;            // 线程池中的线程数
    pthread_t *m_threads;           // 描述线程池的数组，其大小为m_thread_number

    std::list<T *> m_workqueue;     // 请求队列
    int m_max_requests;             // 请求队列中允许的最大请求数
    sem m_queuestat;                // 信号量: 是否有任务需要处理
    locker m_queuelocker;           // 互斥锁: 保护请求队列的互斥锁
    connection_pool *m_connPool;    // 数据库数据库连接池指针

    int m_actor_model;              // 模型切换
};

template <typename T>
threadpool<T>::threadpool( int actor_model, 
                           connection_pool *connPool,
                           int thread_number,
                           int max_requests) :
                           m_actor_model(actor_model),
                           m_thread_number(thread_number), 
                           m_max_requests(max_requests),
                           m_threads(NULL),
                           m_connPool(connPool) {
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i) {
        // 循环创建线程，并将工作线程按要求运行(worker参数)
        // pthread_create的函数原型中第三个参数的类型为函数指针，
        // 指向的线程处理函数参数类型为(void *),若线程函数为类成员函数，
        // 则this指针会作为默认的参数被传进函数中，
        // 从而和线程函数参数(void*)不能匹配，不能通过编译。
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        // 将线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;     // 释放线程池
}

template <typename T>
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        printf("workqueue reached maxsize!\n");
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T* request) {
    m_queuelocker.lock();
    //根据硬件，预先设置请求队列的最大值
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 线程处理函数： 通过私有成员函数run，完成线程处理要求。
template <typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}
// run执行任务： 主要实现工作线程从请求队列中取出某个任务进行处理，注意线程同步
template <typename T>
void threadpool<T>::run() {
    while (true) {
        //信号量等待
        m_queuestat.wait();
        //被唤醒后先加互斥锁
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        //从请求队列中取出第一个任务, 并将任务从请求队列删除
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        // m_actor_model: 设置反应堆模型    
        // 0：Proactor模型 
        // 1：Reactor模型    
        if (1 == m_actor_model)  {
            if (0 == request->m_state) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    // process(模板类中的方法,这里是http类)进行处理
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif