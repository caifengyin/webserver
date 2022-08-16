#ifndef LOG_H
#define LOG_H
/*
同步/异步日志系统
同步/异步日志系统主要涉及了两个模块，一个是日志模块，一个是阻塞队列模块,其中加入阻塞队列模块主要是解决异步写入日志做准备.
> * 自定义阻塞队列
> * 单例模式创建日志
> * 同步日志
> * 异步日志
> * 实现按天、超行分类
*/
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log {
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance() {
        static Log instance;
        return &instance;
    }
    //异步写日志公有方法，调用私有方法async_write_log
    static void *flush_log_thread(void *args) {
        Log::get_instance()->async_write_log();  
    }
    // init函数实现日志创建、写入方式的判断。
    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, 
              int close_log, 
              int log_buf_size = 8192, 
              int split_lines = 5000000, 
              int max_queue_size = 0);
    //write_log函数完成写入日志文件中的具体内容，主要实现日志分级、分文件、格式化输出内容。
    //将输出内容按照标准格式整理
    void write_log(int level, const char *format, ...);
    //强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();
    //异步写日志方法
    void* async_write_log() {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            // int fputs(const char *str, FILE *stream);
            // str，一个数组，包含了要写入的以空字符终止的字符序列。
            // stream，指向FILE对象的指针，该FILE对象标识了要被写入字符串的流。
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }

    }

private:
    char dir_name[128];                 // 路径名
    char log_name[128];                 // log文件名
    int m_split_lines;                  // 日志最大行数
    int m_log_buf_size;                 // 日志缓冲区大小
    long long m_count;                  // 日志行数记录
    int m_today;                        // 因为按天分类,记录当前时间是那一天
    FILE *m_fp;                         // 打开log的文件指针
    char *m_buf;
    block_queue<string> *m_log_queue;   // 阻塞队列
    bool m_is_async;                    // 是否同步标志位
    locker m_mutex;
    int m_close_log;                    // 关闭日志
};

// __VA_ARGS__是一个可变参数的宏，定义时宏定义中参数列表的最后一个参数为省略号
// __VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，
// 这里printf参数列表中的的##会把前面多余的","去掉，否则会编译出错，建议使用后面这种，使得程序更加健壮。
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
