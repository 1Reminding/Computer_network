#include <winsock.h>
#include <iostream>
#include <ctime>   // 用于时间戳处理
#include <string>

#pragma comment(lib,"ws2_32.lib")  // 添加编译命令，链接 ws2_32 库

using namespace std;

bool offlineMode = false;  // 表示客户端是否处于离线状态
std::string userID;  // 使用用户输入的昵称作为userID
int flag = 1;  // 表示客户端是否在线，0表示退出，1表示在线

// 消息类型
enum Type {
    CHAT = 1,
    EXIT,
    OFFLINE,
    SYSTEM_MSG  // 系统消息类型
};

// 自定义消息结构
struct message {
    Type type;
    string msg;
    string name;
    string time;
};

// message类型转换为string类型，字段间以'\n'为分隔符
string msgToString(message m) {
    string s;
    s += to_string(m.type) + "\n" + m.name + "\n" + m.msg + "\n" + m.time + "\n";
    return s;
}

// string类型转换为message类型
message stringToMsg(string s) {
    message m;
    int pos1 = s.find('\n');
    int pos2 = s.find('\n', pos1 + 1);
    int pos3 = s.find('\n', pos2 + 1);
    int pos4 = s.find('\n', pos3 + 1);

    m.type = static_cast<Type>(stoi(s.substr(0, pos1)));
    m.name = s.substr(pos1 + 1, pos2 - pos1 - 1);
    m.msg = s.substr(pos2 + 1, pos3 - pos2 - 1);
    m.time = s.substr(pos3 + 1, pos4 - pos3 - 1);

    return m;
}

// 初始化Winsock
int initwsa() {
    WSADATA wsaDATA;
    if (!WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        cout << "---------------------------------------------------" << endl;
        cout << "【系统消息】:初始化网络环境成功！！" << endl;
        return 1;
    }
    else {
        cout << "---------------------------------------------------" << endl;
        cout << "【系统消息】:初始化网络环境失败！！" << endl;
        return 0;
    }
}

// 断线后的重连函数
void reconnect(SOCKET& s_server, SOCKADDR_IN& server_addr) {
    while (true) {
        cout << "【系统消息】:尝试重新连接..." << endl;
        s_server = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(s_server, (SOCKADDR*)&server_addr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
            cout << "【系统消息】:重连失败，5秒后重试..." << endl;
            Sleep(5000);  // 每5秒重试一次
        }
        else {
            cout << "【系统消息】:重连成功！" << endl;
            // 发送userID给服务器，告知这是重连
            send(s_server, userID.c_str(), userID.size(), 0);
            offlineMode = false;  // 重新上线，取消离线状态
            break;
        }
    }
}

// 负责接收消息的线程
DWORD WINAPI clientaccept(LPVOID IpParameter) {
    SOCKET* sockConnPtr = (SOCKET*)IpParameter;
    SOCKET& sockConn = *sockConnPtr;  // 使用 socket 的引用
    char recvBuf[1000];  // 接收消息的缓冲区
    memset(recvBuf, 0, sizeof(recvBuf));
    int recvLen = 0;

    // 当客户端还在线时，持续接收消息
    while (flag) {
        recvLen = recv(sockConn, recvBuf, 1000, 0);  // 最多接收1000字节的数据
        if (recvLen <= 0) {
            if (offlineMode) {
                // 如果处于离线状态，继续尝试重连
                cout << "---------------------------------------------------" << endl;
                cout << "【系统消息】:与服务器断开连接，尝试重连..." << endl;
                reconnect(sockConn, *(SOCKADDR_IN*)IpParameter);  // 调用自动重连
            }
            else {
                cout << "---------------------------------------------------" << endl;
                cout << "【系统消息】:与服务器断开连接!" << endl;
                closesocket(sockConn);
                flag = 0;  // 设置为退出状态
                break;
            }
        }
        else {
            string s = recvBuf;
            message r = stringToMsg(s);

            // 输出消息类型以便调试
           // cout << "【调试】: 收到消息类型: " << r.type << endl;

            switch (r.type) {
            case CHAT:
                cout << r.time << "【" << r.name << "】:" << r.msg << endl;
                break;
            case OFFLINE:
                cout << "---------------------------------------------------" << endl;
                cout << "【系统消息】:服务端已离线! 按下 ENTER 关闭连接!" << endl;
                flag = 0;
                break;
            case EXIT:
                cout << "---------------------------------------------------" << endl;
                cout << "【系统消息】:服务端已下线! 按下 ENTER 关闭连接!" << endl;
                flag = 0;
                break;
            case SYSTEM_MSG:
                // 处理系统消息，显示当前在线人数
                cout << "【系统消息】: " << r.msg << endl;
                break;
            }
        }
        memset(recvBuf, 0, sizeof(recvBuf));  // 清空接收缓冲区
    }
    return 0;
}


// 客户端主函数
int main() {
    SOCKET s_server;
    SOCKADDR_IN server_addr;

    cout << "---------------------------------------------------" << endl;
    cout << "||                   Mini-We-Chat                ||" << endl;
    cout << "---------------------------------------------------" << endl;
    cout << "【系统消息】:请输入您的昵称：";
    cin >> userID;  // 将用户输入的昵称作为userID
    cout << "【系统消息】:请输入要连接的IP地址:";
    char ipaddress[50];
    cin >> ipaddress;

    // 初始化 Winsock
    if (!initwsa()) {
        return 0;
    }

    // 设置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.S_un.S_addr = inet_addr(ipaddress);
    server_addr.sin_port = htons(5010);
    s_server = socket(AF_INET, SOCK_STREAM, 0);

    // 发起连接
    if (connect(s_server, (SOCKADDR*)&server_addr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "【系统消息】:服务器连接失败!!" << endl;
        closesocket(s_server);
        reconnect(s_server, server_addr);  // 尝试重连
    }
    else {
        cout << "【系统消息】:服务器连接成功!!" << endl;
        flag = 1;
        cout << "【系统消息】:输入OFF离线，输入EXIT关闭连接!" << endl;
        cout << "---------------------------------------------------" << endl;
        cout << "【系统消息】:欢迎【" << userID << "】加入聊天室！" << endl;
        // 发送userID给服务器，以通知登录成功
        send(s_server, userID.c_str(), userID.size(), 0);
    }

    char send_buf[1000];
    memset(send_buf, 0, sizeof(send_buf));
    message m;
    m.name = userID;
    int count = 0;

    // 创建线程负责接收消息
    CloseHandle(CreateThread(NULL, 0, clientaccept, (LPVOID)&s_server, 0, 0));

    // 当客户端还在线时，随时可以发送消息
        // 发消息
        while (flag) {
            // 获取当前时间
            char timeString[100];
            time_t now = time(0);
            tm now_time;
            localtime_s(&now_time, &now);
            strftime(timeString, sizeof(timeString), "%Y年%m月%d日 %H:%M:%S", &now_time);
        
            // 发消息
            char in[1000];
            cin.getline(in, 1000);
            m.msg = in;
            m.time = timeString;

            if (m.msg == "off" || m.msg == "OFF") {
                m.type = OFFLINE;
                offlineMode = true;  // 设置客户端进入离线模式
                send(s_server, msgToString(m).c_str(), msgToString(m).size(), 0);  // 发送OFFLINE消息
                cout << "---------------------------------------------------" << endl;
                cout << "【系统消息】: 您选择了OFF,连接离线!" << endl;
            }
            else if (m.msg == "exit" || m.msg == "EXIT") {
                m.type = EXIT;
                flag = 0;  // 设置退出标志
                send(s_server, msgToString(m).c_str(), msgToString(m).size(), 0);  // 发送EXIT消息
                cout << "---------------------------------------------------" << endl;
                cout << "【系统消息】: 您选择了EXIT，连接关闭！" << endl;
                break;  // 退出循环，关闭连接
            }
            else {
                m.type = CHAT;
                send(s_server, msgToString(m).c_str(), msgToString(m).size(), 0);  // 发送聊天消息
            }

            memset(send_buf, 0, sizeof(send_buf));  // 清空发送缓冲区
        }

        // 关闭socket和清理资源
        closesocket(s_server);
        WSACleanup();
        return 0;
    }