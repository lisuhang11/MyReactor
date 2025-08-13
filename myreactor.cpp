#include "myreactor.h"
#include <iostream>     // ��׼���
#include <string.h>     // �ڴ����������memset�ȣ�
#include <sys/types.h>  // ����ϵͳ��������
#include <sys/socket.h> // �׽��ֲ���
#include <netinet/in.h> // �����ַ�ṹ��sockaddr_in�ȣ�
#include <arpa/inet.h>  // �����ֽ���ת����htonl/htons�ȣ�
#include <fcntl.h>      // �ļ����ƣ����÷������ȣ�
#include <sys/epoll.h>  // epoll I/O��·����
#include <list>         // ��������
#include <errno.h>      // �����붨��
#include <time.h>       // ʱ�亯��
#include <sstream>      // �ַ���������ʽ��ʱ�䣩
#include <iomanip>      // ��ʽ�����������ȣ�
#include <unistd.h>     // ϵͳ���ã�close�ȣ�

// ȡ�������Ľ�Сֵ
#define min(a, b) ((a <= b) ? (a) : (b))

// ���캯������ʼ����Ա������C11��������ʱ��ʼ�����˴����գ�
MyReactor::MyReactor()
{
	// m_listenfd��m_epollfd��m_bStop��������ʱ��ʼ��
}

// ����������δ�����⴦����Դ�ͷ���uninit��ɣ�
MyReactor::~MyReactor()
{

}

/**
 * @brief ��ʼ��������
 * @param ip �󶨵�IP��ַ
 * @param port �󶨵Ķ˿ں�
 * @return �ɹ�����true��ʧ�ܷ���false
 */
bool MyReactor::init(std::string ip, short port) {
	// ��һ�������������׽��ֲ���ʼ��epoll
	if (!create_server_listener(ip, port)) {
		std::cout << "Unable to bind: " << ip << ":" << port << "." << std::endl;
		return false;
	}
	// ��ӡ���߳�ID�������ã�
	std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

	// �ڶ������������������ӵ��̣߳�ִ��accept_thread_proc��
	m_acceptthread.reset(new std::thread(MyReactor::accept_thread_proc, this));

	// �����������������̣߳���WORKER_THREAD_NUM����ִ��worker_thread_proc��
	for (auto& t : m_workerthreads) {
		t.reset(new std::thread(MyReactor::worker_thread_proc, this));
	}

	// ����������main_loop�̣߳�����epoll�¼��ַ���
	std::thread main_thread(&MyReactor::main_loop, this);
	main_thread.detach();  // �����̣߳���������

	return true;
}

/**
 * @brief ����ʼ����������ֹͣ�����̣߳��ͷ���Դ
 * @return �ɹ�����true
 */
bool MyReactor::uninit() {
	// ��һ��������ֹͣ��־���������еȴ����߳�
	m_bStop = true;
	m_acceptcond.notify_all();
	m_workercond.notify_all();

	// �ڶ������ȴ��߳̽����������߳���Դ��
	m_acceptthread->join();
	for (auto& t : m_workerthreads)
	{
		t->join();
	}

	// ����������epoll���Ƴ������׽��֣��ر���Դ
	::epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_listenfd, NULL);
	::shutdown(m_listenfd, SHUT_RDWR);  // �ر��׽��ֵĶ�д
	::close(m_listenfd);                // �رռ����׽���
	::close(m_epollfd);                 // �ر�epollʵ��

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
 * @brief ���¼�ѭ�����ȴ�epoll�¼����ַ�
 * @param p ָ��CMyReactorʵ����ָ��
 * @return ��ʵ�ʷ���ֵ�������̺߳���ǩ����
 */
void* MyReactor::main_loop(void* p) {
	std::cout << "main thread id = " << std::this_thread::get_id() << std::endl;

	MyReactor* pReactor = static_cast<MyReactor*>(p);

	// ѭ���ȴ��¼���ֱ��������ֹͣ
	while (!pReactor->m_bStop) {
		// ���epoll��⵽���¼������1024����
		struct epoll_event ev[1024];
		// �ȴ��¼�����ʱʱ��10ms����������������
		int n = epoll_wait(pReactor->m_epollfd, ev, 1024, 10);
		if (n == 0) {// ��ʱ�����¼��������ȴ�
			continue;
		}
		else if (n < 0) {// ���󣺴�ӡ��־�����
			std::cout << "epoll_wait error" << std::endl;
			continue;
		}

		// �������м�⵽���¼�
		int m = min(n, 1024);  // ȷ����Խ��
		for (int i = 0; i < m; i++) {
			// �¼�1�������׽����������ӣ����ѽ����̴߳���
			if (ev[i].data.fd == pReactor->m_listenfd) {
				pReactor->m_acceptcond.notify_one();
			}
			else {// �¼�2���ͻ����׽�����I/O�¼������������̴߳���
				// ���������ͻ���fd����������б�
				std::unique_lock<std::mutex> lock(pReactor->m_workermutex);
				pReactor->m_listClients.push_back(ev[i].data.fd);
				// ����һ�������̴߳���
				pReactor->m_workercond.notify_one();
			}
		}
	}
	std::cout << "main loop exit ..." << std::endl;
	return NULL;
}

/**
 * @brief �����̴߳������������¿ͻ�������
 * @param pReatcor ָ��CMyReactorʵ����ָ��
 */
void MyReactor::accept_thread_proc(MyReactor* pReactor) {

	std::cout << "accept thread, thread id = " << std::this_thread::get_id() << std::endl;

	// ѭ���ȴ������ӣ�ֱ��������ֹͣ
	while (true) {
		int newfd;	// �¿ͻ����׽���
		struct sockaddr_in clientaddr;
		socklen_t addrlen = sizeof(clientaddr);

		// �������ȴ����߳�֪ͨ���������¼���
		std::unique_lock<std::mutex> lock(pReactor->m_acceptmutex);
		pReactor->m_acceptcond.wait(lock); // �����ȴ�֪ͨ

		// ����������ֹͣ���˳�ѭ��
		if (pReactor->m_bStop) {
			break;
		}

		// ���������ӣ������������׽��֣���ʱһ�������ӣ�
		newfd = ::accept(pReactor->m_listenfd, (struct sockaddr*)&clientaddr, &addrlen);

		// ����ʧ�ܣ�����
		if (newfd == -1) {
			continue;
		}

		// ��ӡ��������Ϣ���ͻ���IP�Ͷ˿ڣ�
		std::cout << "new client connected: " << ::inet_ntoa(clientaddr.sin_addr)
			<< ":" << ::ntohs(clientaddr.sin_port) << std::endl;

		// ��һ���������׽�������Ϊ�����������epoll��Ե������
		int oldflag = ::fcntl(newfd, F_GETFL, 0);  // ��ȡ��ǰ��־
		int newflag = oldflag | O_NONBLOCK;         // ���ӷ�������־
		if (::fcntl(newfd, F_SETFL, newflag) == -1)  // �����±�־
		{
			std::cout << "fcntl error, oldflag =" << oldflag << ", newflag = " << newflag << std::endl;
			continue;
		}

		// �ڶ����������׽��ּ���epoll����ע���¼����Է��ر��¼�����Ե����
		struct epoll_event e;
		memset(&e, 0, sizeof(e));
		e.events = EPOLLIN | EPOLLRDHUP | EPOLLET;  // EPOLLIN�����¼���EPOLLRDHUP���Է��رգ�EPOLLET����Ե����
		e.data.fd = newfd;  // �����ͻ����׽���
		if (::epoll_ctl(pReactor->m_epollfd, EPOLL_CTL_ADD, newfd, &e) == -1)
		{
			std::cout << "epoll_ctl error, fd =" << newfd << std::endl;
		}
	}
	std::cout << "accept thread exit ..." << std::endl;
}

/**
 * @brief �����̴߳�����������ͻ��˶�д�¼�
 * @param pReatcor ָ��CMyReactorʵ����ָ��
 */
void MyReactor::worker_thread_proc(MyReactor* pReactor) {
	std::cout << "new worker thread, thread id = " << std::this_thread::get_id() << std::endl;

	// ѭ������ͻ����¼���ֱ��������ֹͣ
	while (true) {
		int clientfd;	// �ͻ����׽���

		// �������ȴ��ͻ����¼������б���ȡһ���ͻ���fd��
		std::unique_lock<std::mutex> guard(pReactor->m_workermutex);
		// ���б�Ϊ�գ��ȴ�֪ͨ��������ٻ��ѣ���whileѭ����
		while (pReactor->m_listClients.empty()) {
			// ����������ֹͣ���˳��߳�
			if (pReactor->m_bStop) {
				break;
			}
			pReactor->m_workercond.wait(guard);  // �����ȴ�֪ͨ
		}
		// ���б���ȡ��һ���ͻ���fd���Ƴ�
		clientfd = pReactor->m_listClients.front();
		pReactor->m_listClients.pop_front();

		// �����ã�ˢ�±�׼���
		std::cout << std::endl;

		// ��һ�������տͻ�������
		std::string strclientmsg;  // �洢�ͻ�����Ϣ
		char buff[256];            // ���ջ�����
		bool bError = false;       // �����־
		while (true)
		{
			memset(buff, 0, sizeof(buff));
			// �������������ݣ�������Ҫ��ε��ã�
			int nRecv = ::recv(clientfd, buff, 256, 0);

			if (nRecv == -1)  // ���մ���
			{
				// EWOULDBLOCK�������ݿɶ�������������˳�ѭ����
				if (errno == EWOULDBLOCK)
					break;
				// �������󣺹ر�����
				else
				{
					std::cout << "recv error, client disconnected, fd = " << clientfd << std::endl;
					pReactor->close_client(clientfd);
					bError = true;
					break;
				}
			}
			// �Զ˹ر����ӣ��رձ�������
			else if (nRecv == 0)
			{
				std::cout << "peer closed, client disconnected, fd = " << clientfd << std::endl;
				pReactor->close_client(clientfd);
				bError = true;
				break;
			}

			// ���ճɹ����ۼ�����
			strclientmsg += buff;
		}

		// ������������������
		if (bError)
			continue;

		// ��ӡ�ͻ�����Ϣ
		std::cout << "client msg: " << strclientmsg;

		// �ڶ�����������Ӧ�����ʱ���ǩ��
		time_t now = time(NULL);  // ��ǰʱ��
		struct tm* nowstr = localtime(&now);  // ת��Ϊ����ʱ��
		std::ostringstream ostimestr;  // �ַ�������ʽ��ʱ��
		ostimestr << "[" << nowstr->tm_year + 1900 << "-"  // �꣨tm_year�Ǵ�1900��ʼ��ƫ�ƣ�
			<< std::setw(2) << std::setfill('0') << nowstr->tm_mon + 1 << "-"  // �£����㣩
			<< std::setw(2) << std::setfill('0') << nowstr->tm_mday << " "     // �գ����㣩
			<< std::setw(2) << std::setfill('0') << nowstr->tm_hour << ":"     // ʱ�����㣩
			<< std::setw(2) << std::setfill('0') << nowstr->tm_min << ":"      // �֣����㣩
			<< std::setw(2) << std::setfill('0') << nowstr->tm_sec << "]server reply: ";  // �루���㣩

		// �ڿͻ�����Ϣǰ����ʱ���ǩ
		strclientmsg.insert(0, ostimestr.str());

		// ��������������Ӧ���ͻ���
		while (true)
		{
			// �������������ݣ�������Ҫ��ε��ã�
			int nSent = ::send(clientfd, strclientmsg.c_str(), strclientmsg.length(), 0);

			if (nSent == -1)  // ���ʹ���
			{
				// EWOULDBLOCK�������������ȴ�������
				if (errno == EWOULDBLOCK)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));  // ����10ms
					continue;
				}
				// �������󣺹ر�����
				else
				{
					std::cout << "send error, fd = " << clientfd << std::endl;
					pReactor->close_client(clientfd);
					break;
				}
			}

			// ���ͳɹ�����ӡ�ѷ������ݣ��Ƴ��ѷ��Ͳ���
			std::cout << "send: " << strclientmsg;
			strclientmsg.erase(0, nSent);

			// ȫ��������ɣ��˳�ѭ��
			if (strclientmsg.empty())
				break;
		}
	}
}

/**
 * @brief ���������������׽��ֲ���ʼ��epoll
 * @param ip �󶨵�IP��ַ
 * @param port �󶨵Ķ˿ں�
 * @return �ɹ�����true��ʧ�ܷ���false
 */
bool MyReactor::create_server_listener(std::string ip, short port)
{
	// ��һ��������������TCP�׽��֣�SOCK_STREAM��TCP��SOCK_NONBLOCK����������
	m_listenfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (m_listenfd == -1)  // ����ʧ��
		return false;

	// �ڶ����������׽���ѡ������ַ�Ͷ˿ڸ��ã��������������ʱ�˿�ռ�ã�
	int on = 1;
	::setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));  // ��ַ����
	::setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));  // �˿ڸ���

	// ����������IP�Ͷ˿�
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;                // IPv4
	servaddr.sin_addr.s_addr = inet_addr(ip.c_str());     // �󶨵�IP��ַ
	servaddr.sin_port = htons(port);              // �󶨵Ķ˿ڣ�ת��Ϊ�����ֽ���
	if (::bind(m_listenfd, (sockaddr*)&servaddr, sizeof(servaddr)) == -1)  // ��ʧ��
		return false;

	// ���Ĳ�����ʼ������backlog=50�����δ������Ӷ��г��ȣ�
	if (::listen(m_listenfd, 50) == -1)  // ����ʧ��
		return false;

	// ���岽������epollʵ��������1�����ԣ��������0��
	m_epollfd = ::epoll_create(1);
	if (m_epollfd == -1)  // ����ʧ��
		return false;

	// ���������������׽��ּ���epoll����ע���¼��ͶԷ��ر��¼�
	struct epoll_event e;
	memset(&e, 0, sizeof(e));
	e.events = EPOLLIN | EPOLLRDHUP;  // EPOLLIN���������ӣ�EPOLLRDHUP���Է��ر�
	e.data.fd = m_listenfd;           // ���������׽���
	if (::epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &e) == -1)  // ���ʧ��
		return false;

	return true;
}