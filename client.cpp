#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <pthread.h>

// ���շ�������Ϣ���̺߳���
void recv_thread(int sockfd) {
    char buff[1024];
    while (true) {
        memset(buff, 0, sizeof(buff));
        int n = recv(sockfd, buff, sizeof(buff) - 1, 0);

        if (n <= 0) {
            if (n < 0) {
                std::cerr << "\n��������ʧ��" << std::endl;
            }
            else {
                std::cerr << "\n�������ѶϿ�����" << std::endl;
            }
            close(sockfd);
            exit(0);
        }

        std::cout << "\n�������ظ�: " << buff << std::endl;
        std::cout << "��������Ϣ������q�˳���: ";
        std::cout.flush();  // ˢ�������ȷ����ʾ��ʾ
    }
}

int main(int argc, char* argv[]) {
    // ��������в���
    if (argc != 3) {
        std::cerr << "�÷�: " << argv[0] << " <������IP> <�˿�>" << std::endl;
        return 1;
    }

    // �����ͻ����׽���
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        std::cerr << "�����׽���ʧ��" << std::endl;
        return 1;
    }

    // ���÷�������ַ
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(argv[2]));  // �������˿�

    // ת��IP��ַ
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        std::cerr << "��Ч��IP��ַ: " << argv[1] << std::endl;
        close(sockfd);
        return 1;
    }

    // ���ӷ�����
    std::cout << "�������� " << argv[1] << ":" << argv[2] << "..." << std::endl;
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        std::cerr << "���ӷ�����ʧ��" << std::endl;
        close(sockfd);
        return 1;
    }

    std::cout << "�ɹ����ӵ�������" << std::endl;

    // ���������߳�
    std::thread recv_th(recv_thread, sockfd);
    recv_th.detach();  // �����̣߳���������

    // ���̸߳�������Ϣ
    std::string msg;
    while (true) {
        std::cout << "��������Ϣ������q�˳���: ";
        std::getline(std::cin, msg);

        if (msg == "q" || msg == "Q") {
            std::cout << "���ڶϿ�����..." << std::endl;
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            break;
        }

        // ������Ϣ��������
        int n = send(sockfd, msg.c_str(), msg.size(), 0);
        if (n == -1) {
            std::cerr << "������Ϣʧ��" << std::endl;
            close(sockfd);
            return 1;
        }
    }

    return 0;
}
