#include "myreactor.h"
#include <iostream>
#include <signal.h>

// 全局服务器实例指针，用于信号处理函数中调用uninit
MyReactor* g_pReactor = nullptr;

// 信号处理函数：捕获Ctrl+C，优雅关闭服务器
void signal_handler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\n收到中断信号，正在关闭服务器..." << std::endl;
        if (g_pReactor != nullptr) {
            g_pReactor->uninit();
        }
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    // 设置默认IP和端口
    std::string ip = "0.0.0.0";  // 监听所有网络接口
    short port = 8080;

    // 解析命令行参数（可选：./server [ip] [port]）
    if (argc >= 2) {
        ip = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
    }

    // 注册信号处理函数，捕获Ctrl+C
    signal(SIGINT, signal_handler);

    // 创建并初始化服务器
    MyReactor reactor;
    g_pReactor = &reactor;

    if (!reactor.init(ip, port)) {
        std::cerr << "服务器初始化失败" << std::endl;
        return -1;
    }

    std::cout << "服务器已启动，监听 " << ip << ":" << port << "，按Ctrl+C关闭" << std::endl;

    // 主线程进入循环（保持程序运行）
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
