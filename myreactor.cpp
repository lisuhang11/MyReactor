#include "myreactor.h"
#include <iostream>     // 标准输出
#include <string.h>     // 内存操作函数（memset等）
#include <sys/types.h>  // 基本系统数据类型
#include <sys/socket.h> // 套接字操作
#include <netinet/in.h> // 网络地址结构（sockaddr_in等）
#include <arpa/inet.h>  // 网络字节序转换（htonl/htons等）
#include <fcntl.h>      // 文件控制（设置非阻塞等）
#include <sys/epoll.h>  // epoll I/O多路复用
#include <list>         // 链表容器
#include <errno.h>      // 错误码定义
#include <time.h>       // 时间函数
#include <sstream>      // 字符串流（格式化时间）
#include <iomanip>      // 格式化输出（补零等）
#include <unistd.h>     // 系统调用（close等）

// 取两个数的较小值
#define min(a, b) ((a <= b) ? (a) : (b))

// 构造函数：初始化成员变量（C11已在声明时初始化，此处留空）
MyReactor::MyReactor()
{
	// m_listenfd、m_epollfd、m_bStop已在声明时初始化
}

// 析构函数：未做特殊处理（资源释放由uninit完成）
MyReactor::~MyReactor()
{

}

/**
 * @brief 初始化服务器
 * @param ip 绑定的IP地址
 * @param port 绑定的端口号
 * @return 成功返回true，失败返回false
 */
bool MyReactor::init(std::string ip, short port) {
	// 第一步：创建监听套接字并初始化epoll
	if (!create_server_listener(ip, port)) {
		std::cout << "Unable to bind: " << ip << ":" << port << "." << std::endl;
		return false;
	}
	// 打印主线程ID（调试用）
	std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

	// 第二步：启动接收新连接的线程（执行accept_thread_proc）
	m_acceptthread.reset(new std::thread(MyReactor::accept_thread_proc, this));

	// 第三步：启动工作线程（共WORKER_THREAD_NUM个，执行worker_thread_proc）
	for (auto& t : m_workerthreads) {
		t.reset(new std::thread(MyReactor::worker_thread_proc, this));
	}

	// 新增：启动main_loop线程（处理epoll事件分发）
	std::thread main_thread(&MyReactor::main_loop, this);
	main_thread.detach();  // 分离线程，独立运行

	return true;
}

/**
 * @brief 反初始化服务器：停止所有线程，释放资源
 * @return 成功返回true
 */
bool MyReactor::uninit() {
	// 第一步：设置停止标志，唤醒所有等待的线程
	m_bStop = true;
	m_acceptcond.notify_all();
	m_workercond.notify_all();

	// 第二步：等待线程结束（回收线程资源）
	m_acceptthread->join();
	for (auto& t : m_workerthreads)
	{
		t->join();
	}

	// 第三步：从epoll中移除监听套接字，关闭资源
	::epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_listenfd, NULL);
	::shutdown(m_listenfd, SHUT_RDWR);  // 关闭套接字的读写
	::close(m_listenfd);                // 关闭监听套接字
	::close(m_epollfd);                 // 关闭epoll实例

	return true;

}


bool MyReactor::close_client(int clientfd)
{
	if (::epoll_ctl(m_epollfd, EPOLL_CTL_DEL, clientfd, NULL) == -1)
	{
		std::cout << "close client socket failed as call epoll_ctl failed" << std::endl;
		//return false;
	}

	::close(clientfd);

	return true;
}

/**
 * @brief 主事件循环：等待epoll事件并分发
 * @param p 指向CMyReactor实例的指针
 * @return 无实际返回值（兼容线程函数签名）
 */
void* MyReactor::main_loop(void* p) {
	std::cout << "main thread id = " << std::this_thread::get_id() << std::endl;

	MyReactor* pReactor = static_cast<MyReactor*>(p);

	// 循环等待事件，直到服务器停止
	while (!pReactor->m_bStop) {
		// 存放epoll检测到的事件（最多1024个）
		struct epoll_event ev[1024];
		// 等待事件（超时时间10ms，避免永久阻塞）
		int n = epoll_wait(pReactor->m_epollfd, ev, 1024, 10);
		if (n == 0) {// 超时：无事件，继续等待
			continue;
		}
		else if (n < 0) {// 错误：打印日志后继续
			std::cout << "epoll_wait error" << std::endl;
			continue;
		}

		// 处理所有检测到的事件
		int m = min(n, 1024);  // 确保不越界
		for (int i = 0; i < m; i++) {
			// 事件1：监听套接字有新连接（唤醒接收线程处理）
			if (ev[i].data.fd == pReactor->m_listenfd) {
				pReactor->m_acceptcond.notify_one();
			}
			else {// 事件2：客户端套接字有I/O事件（交给工作线程处理）
				// 加锁：将客户端fd加入待处理列表
				std::unique_lock<std::mutex> lock(pReactor->m_workermutex);
				pReactor->m_listClients.push_back(ev[i].data.fd);
				// 唤醒一个工作线程处理
				pReactor->m_workercond.notify_one();
			}
		}
	}
	std::cout << "main loop exit ..." << std::endl;
	return NULL;
}

/**
 * @brief 接收线程处理函数：接收新客户端连接
 * @param pReatcor 指向CMyReactor实例的指针
 */
void MyReactor::accept_thread_proc(MyReactor* pReactor) {

	std::cout << "accept thread, thread id = " << std::this_thread::get_id() << std::endl;

	// 循环等待新连接，直到服务器停止
	while (true) {
		int newfd;	// 新客户端套接字
		struct sockaddr_in clientaddr;
		socklen_t addrlen = sizeof(clientaddr);

		// 加锁并等待主线程通知（新连接事件）
		std::unique_lock<std::mutex> lock(pReactor->m_acceptmutex);
		pReactor->m_acceptcond.wait(lock); // 阻塞等待通知

		// 若服务器已停止，退出循环
		if (pReactor->m_bStop) {
			break;
		}

		// 接收新连接（非阻塞监听套接字，此时一定有连接）
		newfd = ::accept(pReactor->m_listenfd, (struct sockaddr*)&clientaddr, &addrlen);

		// 接收失败：跳过
		if (newfd == -1) {
			continue;
		}

		// 打印新连接信息（客户端IP和端口）
		std::cout << "new client connected: " << ::inet_ntoa(clientaddr.sin_addr)
			<< ":" << ::ntohs(clientaddr.sin_port) << std::endl;

		// 第一步：将新套接字设置为非阻塞（配合epoll边缘触发）
		int oldflag = ::fcntl(newfd, F_GETFL, 0);  // 获取当前标志
		int newflag = oldflag | O_NONBLOCK;         // 增加非阻塞标志
		if (::fcntl(newfd, F_SETFL, newflag) == -1)  // 设置新标志
		{
			std::cout << "fcntl error, oldflag =" << oldflag << ", newflag = " << newflag << std::endl;
			continue;
		}

		// 第二步：将新套接字加入epoll，关注读事件、对方关闭事件、边缘触发
		struct epoll_event e;
		memset(&e, 0, sizeof(e));
		e.events = EPOLLIN | EPOLLRDHUP | EPOLLET;  // EPOLLIN：读事件；EPOLLRDHUP：对方关闭；EPOLLET：边缘触发
		e.data.fd = newfd;  // 关联客户端套接字
		if (::epoll_ctl(pReactor->m_epollfd, EPOLL_CTL_ADD, newfd, &e) == -1)
		{
			std::cout << "epoll_ctl error, fd =" << newfd << std::endl;
		}
	}
	std::cout << "accept thread exit ..." << std::endl;
}

/**
 * @brief 工作线程处理函数：处理客户端读写事件
 * @param pReatcor 指向CMyReactor实例的指针
 */
void MyReactor::worker_thread_proc(MyReactor* pReactor) {
	std::cout << "new worker thread, thread id = " << std::this_thread::get_id() << std::endl;

	// 循环处理客户端事件，直到服务器停止
	while (true) {
		int clientfd;	// 客户端套接字

		// 加锁并等待客户端事件（从列表中取一个客户端fd）
		std::unique_lock<std::mutex> guard(pReactor->m_workermutex);
		// 若列表为空，等待通知（避免虚假唤醒，用while循环）
		while (pReactor->m_listClients.empty()) {
			// 若服务器已停止，退出线程
			if (pReactor->m_bStop) {
				break;
			}
			pReactor->m_workercond.wait(guard);  // 阻塞等待通知
		}
		// 从列表中取第一个客户端fd并移除
		clientfd = pReactor->m_listClients.front();
		pReactor->m_listClients.pop_front();

		// 调试用：刷新标准输出
		std::cout << std::endl;

		// 第一步：接收客户端数据
		std::string strclientmsg;  // 存储客户端消息
		char buff[256];            // 接收缓冲区
		bool bError = false;       // 错误标志
		while (true)
		{
			memset(buff, 0, sizeof(buff));
			// 非阻塞接收数据（可能需要多次调用）
			int nRecv = ::recv(clientfd, buff, 256, 0);

			if (nRecv == -1)  // 接收错误
			{
				// EWOULDBLOCK：无数据可读（正常情况，退出循环）
				if (errno == EWOULDBLOCK)
					break;
				// 其他错误：关闭连接
				else
				{
					std::cout << "recv error, client disconnected, fd = " << clientfd << std::endl;
					pReactor->close_client(clientfd);
					bError = true;
					break;
				}
			}
			// 对端关闭连接：关闭本地连接
			else if (nRecv == 0)
			{
				std::cout << "peer closed, client disconnected, fd = " << clientfd << std::endl;
				pReactor->close_client(clientfd);
				bError = true;
				break;
			}

			// 接收成功：累加数据
			strclientmsg += buff;
		}

		// 若出错，跳过后续处理
		if (bError)
			continue;

		// 打印客户端消息
		std::cout << "client msg: " << strclientmsg;

		// 第二步：构造响应（添加时间标签）
		time_t now = time(NULL);  // 当前时间
		struct tm* nowstr = localtime(&now);  // 转换为本地时间
		std::ostringstream ostimestr;  // 字符串流格式化时间
		ostimestr << "[" << nowstr->tm_year + 1900 << "-"  // 年（tm_year是从1900开始的偏移）
			<< std::setw(2) << std::setfill('0') << nowstr->tm_mon + 1 << "-"  // 月（补零）
			<< std::setw(2) << std::setfill('0') << nowstr->tm_mday << " "     // 日（补零）
			<< std::setw(2) << std::setfill('0') << nowstr->tm_hour << ":"     // 时（补零）
			<< std::setw(2) << std::setfill('0') << nowstr->tm_min << ":"      // 分（补零）
			<< std::setw(2) << std::setfill('0') << nowstr->tm_sec << "]server reply: ";  // 秒（补零）

		// 在客户端消息前插入时间标签
		strclientmsg.insert(0, ostimestr.str());

		// 第三步：发送响应给客户端
		while (true)
		{
			// 非阻塞发送数据（可能需要多次调用）
			int nSent = ::send(clientfd, strclientmsg.c_str(), strclientmsg.length(), 0);

			if (nSent == -1)  // 发送错误
			{
				// EWOULDBLOCK：缓冲区满，等待后重试
				if (errno == EWOULDBLOCK)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 休眠10ms
					continue;
				}
				// 其他错误：关闭连接
				else
				{
					std::cout << "send error, fd = " << clientfd << std::endl;
					pReactor->close_client(clientfd);
					break;
				}
			}

			// 发送成功：打印已发送内容，移除已发送部分
			std::cout << "send: " << strclientmsg;
			strclientmsg.erase(0, nSent);

			// 全部发送完成：退出循环
			if (strclientmsg.empty())
				break;
		}
	}
}

/**
 * @brief 创建服务器监听套接字并初始化epoll
 * @param ip 绑定的IP地址
 * @param port 绑定的端口号
 * @return 成功返回true，失败返回false
 */
bool MyReactor::create_server_listener(std::string ip, short port)
{
	// 第一步：创建非阻塞TCP套接字（SOCK_STREAM：TCP；SOCK_NONBLOCK：非阻塞）
	m_listenfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (m_listenfd == -1)  // 创建失败
		return false;

	// 第二步：设置套接字选项（允许地址和端口复用，避免服务器重启时端口占用）
	int on = 1;
	::setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));  // 地址复用
	::setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));  // 端口复用

	// 第三步：绑定IP和端口
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;                // IPv4
	servaddr.sin_addr.s_addr = inet_addr(ip.c_str());     // 绑定的IP地址
	servaddr.sin_port = htons(port);              // 绑定的端口（转换为网络字节序）
	if (::bind(m_listenfd, (sockaddr*)&servaddr, sizeof(servaddr)) == -1)  // 绑定失败
		return false;

	// 第四步：开始监听（backlog=50：最大未完成连接队列长度）
	if (::listen(m_listenfd, 50) == -1)  // 监听失败
		return false;

	// 第五步：创建epoll实例（参数1：忽略，仅需大于0）
	m_epollfd = ::epoll_create(1);
	if (m_epollfd == -1)  // 创建失败
		return false;

	// 第六步：将监听套接字加入epoll，关注读事件和对方关闭事件
	struct epoll_event e;
	memset(&e, 0, sizeof(e));
	e.events = EPOLLIN | EPOLLRDHUP;  // EPOLLIN：有新连接；EPOLLRDHUP：对方关闭
	e.data.fd = m_listenfd;           // 关联监听套接字
	if (::epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &e) == -1)  // 添加失败
		return false;

	return true;
}