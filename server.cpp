#include "myreactor.h"
#include <iostream>
#include <signal.h>

// ȫ�ַ�����ʵ��ָ�룬�����źŴ������е���uninit
MyReactor* g_pReactor = nullptr;

// �źŴ�����������Ctrl+C�����Źرշ�����
void signal_handler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\n�յ��ж��źţ����ڹرշ�����..." << std::endl;
        if (g_pReactor != nullptr) {
            g_pReactor->uninit();
        }
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    // ����Ĭ��IP�Ͷ˿�
    std::string ip = "0.0.0.0";  // ������������ӿ�
    short port = 8080;

    // ���������в�������ѡ��./server [ip] [port]��
    if (argc >= 2) {
        ip = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
    }

    // ע���źŴ�����������Ctrl+C
    signal(SIGINT, signal_handler);

    // ��������ʼ��������
    MyReactor reactor;
    g_pReactor = &reactor;

    if (!reactor.init(ip, port)) {
        std::cerr << "��������ʼ��ʧ��" << std::endl;
        return -1;
    }

    std::cout << "������������������ " << ip << ":" << port << "����Ctrl+C�ر�" << std::endl;

    // ���߳̽���ѭ�������ֳ������У�
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
