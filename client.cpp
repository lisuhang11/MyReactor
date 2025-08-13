#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <pthread.h>

// 接收服务器消息的线程函数
void recv_thread(int sockfd) {
    char buff[1024];
    while (true) {
        memset(buff, 0, sizeof(buff));
        int n = recv(sockfd, buff, sizeof(buff) - 1, 0);

        if (n <= 0) {
            if (n < 0) {
                std::cerr << "\n接收数据失败" << std::endl;
            }
            else {
                std::cerr << "\n服务器已断开连接" << std::endl;
            }
            close(sockfd);
            exit(0);
        }

        std::cout << "\n服务器回复: " << buff << std::endl;
        std::cout << "请输入消息（输入q退出）: ";
        std::cout.flush();  // 刷新输出，确保提示显示
    }
}

int main(int argc, char* argv[]) {
    // 检查命令行参数
    if (argc != 3) {
        std::cerr << "用法: " << argv[0] << " <服务器IP> <端口>" << std::endl;
        return 1;
    }

    // 创建客户端套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        std::cerr << "创建套接字失败" << std::endl;
        return 1;
    }

    // 设置服务器地址
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(argv[2]));  // 服务器端口

    // 转换IP地址
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        std::cerr << "无效的IP地址: " << argv[1] << std::endl;
        close(sockfd);
        return 1;
    }

    // 连接服务器
    std::cout << "正在连接 " << argv[1] << ":" << argv[2] << "..." << std::endl;
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        std::cerr << "连接服务器失败" << std::endl;
        close(sockfd);
        return 1;
    }

    std::cout << "成功连接到服务器" << std::endl;

    // 启动接收线程
    std::thread recv_th(recv_thread, sockfd);
    recv_th.detach();  // 分离线程，独立运行

    // 主线程负责发送消息
    std::string msg;
    while (true) {
        std::cout << "请输入消息（输入q退出）: ";
        std::getline(std::cin, msg);

        if (msg == "q" || msg == "Q") {
            std::cout << "正在断开连接..." << std::endl;
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            break;
        }

        // 发送消息到服务器
        int n = send(sockfd, msg.c_str(), msg.size(), 0);
        if (n == -1) {
            std::cerr << "发送消息失败" << std::endl;
            close(sockfd);
            return 1;
        }
    }

    return 0;
}
