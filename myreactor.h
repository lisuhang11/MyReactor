#ifndef MYREACTOR_H
#define MYREACTOR_H 

#include <list>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

// 工作线程数量（固定为5个）
#define WORKER_THREAD_NUM 5

class MyReactor {
public:
	MyReactor();
	~MyReactor();
	// 初始化服务器：绑定IP和端口，启动线程
	bool init(std::string ip,short port);
	// 反初始化：停止线程，释放资源
	bool uninit();

	// 关闭客户端连接
	bool close_client(int clientfd);

	// 主事件循环（静态函数，用于主线程处理epoll事件）
	static void* main_loop(void* p);

private:
	// noncopyable禁止拷贝构造和赋值（避免对象复制导致的资源竞争）
	MyReactor(const MyReactor&) = delete;
	MyReactor& operator=(const MyReactor&) = delete;

	// 创建服务器监听套接字并初始化epoll
	bool create_server_listener(std::string ip, short port);
	// 接收线程处理函数（静态函数，用于线程入口）
	static void accept_thread_proc(MyReactor* pReatcor);
	// 工作线程处理函数（静态函数，用于线程入口）
	static void worker_thread_proc(MyReactor* pReatcor);

private:
	int m_listenfd = 0;		// 监听套接字描述符（0表示未初始化）
	int m_epollfd = 0;		// epoll实例描述符（0表示未初始化）
	bool m_bStop = false;	// 服务器停止标志（false：运行中；true：需停止）

	// 接收线程
	std::shared_ptr<std::thread> m_acceptthread;
	// 工作线程数组（共WORKER_THREAD_NUM个）
	std::shared_ptr<std::thread> m_workerthreads[WORKER_THREAD_NUM];

	std::condition_variable		 m_acceptcond;// 接收线程的条件变量（用于通知接收新连接）
	std::mutex					 m_acceptmutex;// 接收线程的互斥锁（保护条件变量等待）

	std::condition_variable		 m_workercond;// 工作线程的条件变量（用于通知处理客户端I/O）
	std::mutex					 m_workermutex;// 工作线程的互斥锁（保护客户端列表和条件变量）

	std::list<int>				 m_listClients;// 待处理的客户端套接字列表（由工作线程消费）
};


#endif