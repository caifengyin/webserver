#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

/*
    项目中将连接资源、定时事件和超时时间封装为定时器类，具体的：
    * 连接资源包括客户端套接字地址、文件描述符和定时器
    * 定时事件为回调函数，将其封装起来由用户自定义，这里是删除非活动socket上的注册事件，并关闭
    * 定时器超时时间 = 浏览器和服务器连接时刻 + 固定时间(TIMESLOT)，可以看出，
      定时器使用绝对时间作为超时值，这里alarm设置为5秒，连接超时为15秒
*/

//前向声明: 连接资源结构体成员需要用到定时器类; 
class util_timer;

// 连接资源
struct client_data
{
    sockaddr_in address; // 客户端socket地址
    int sockfd;          // socket文件描述符
    util_timer *timer;   // 定时器
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;                      // 超时时间
    void (* cb_func)(client_data *);    // 回调函数
    client_data *user_data;             // 连接资源
    util_timer *prev;                   // 前向定时器
    util_timer *next;                   // 后继定时器
};

// **定时器容器**为带头尾结点的升序双向链表，具体的为每个连接创建一个定时器，
// 将其添加到链表中，并按照超时时间升序排列。执行定时任务时，将到期的定时器从链表中删除。
// 主要涉及双向链表的插入，删除操作，其中添加定时器的事件复杂度是O(n),删除定时器的事件复杂度是O(1)
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick(); // 定时任务处理函数

private:
    //私有成员，被公有成员add_timer和adjust_time调用, 主要用于调整链表内部结点
    void add_timer(util_timer *timer, util_timer *lst_head);
    
    // 头尾结点
    util_timer *head;
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst; //创建定时器容器链表
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
