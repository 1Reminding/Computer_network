#include <winsock.h>
#include <iostream>
#include <map>
#include <string>
#include <mutex>
#include <ctime>
#include <sstream>  // 用于字符串流
#pragma comment(lib,"ws2_32.lib")  // 链接 ws2_32 库

using namespace std;

map<SOCKET, string> clientSockets;  // 存储socket到用户名的映射
mutex clientSocketsMutex;  // 互斥锁，保护对clientSockets的访问
int onlineCount = 0;  // 当前在线人数
mutex onlineCountMutex;  // 保护在线人数的互斥锁

//int flag = 0;  // 判断客户端是否在线

// 消息类型
enum Type {
    CHAT = 1,
    EXIT = 2,
    OFFLINE = 3,
    SYSTEM_MSG = 4  // 系统消息类型
};

// 消息结构
struct message {
    Type type;
    string msg;
    string name;
    string time;
};

// message类型转换为string类型，字段间以'\n'为分隔符
string msgToString(const message& m) {
    stringstream s;
    s << m.type << "\n" << m.name << "\n" << m.msg << "\n" << m.time << "\n";
    return s.str();
}

// string类型转换为message类型
message stringToMsg(const string& s) {
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

// 初始化 Winsock
int initwsa() {
    WSADATA wsaDATA;
    if (!WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        cout << "||【系统消息】:初始化网络环境成功!!" << endl;
        return 1;
    }
    else {
        cout << "||【系统消息】:初始化网络环境失败!!" << endl;
        return 0;
    }
}

// 获取当前时间字符串
string getCurrentTime() {
    time_t now = time(0);
    tm local_time;
    localtime_s(&local_time, &now);
    char timeString[100];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &local_time);
    return string(timeString);
}

// 广播当前在线人数
void broadcastOnlineCount() {
    lock_guard<mutex> lock(clientSocketsMutex);  // 保证广播期间对socket map的安全访问

    stringstream ss;
    ss << "【系统消息】: 当前在线人数：" << onlineCount;
    message systemMessage;
    systemMessage.type = SYSTEM_MSG;
    systemMessage.name = "系统";
    systemMessage.msg = ss.str();
    systemMessage.time = getCurrentTime();
    string systemMsgString = msgToString(systemMessage);

    for (const auto& client : clientSockets) {
        send(client.first, systemMsgString.c_str(), systemMsgString.size(), 0);
    }
}
const int MAX_CLIENTS = 50;  // 设置最大聊天人数

void handleClientConnect(SOCKET s_accept, const string& nickname) {
    {
        lock_guard<mutex> lock(onlineCountMutex);
        if (onlineCount >= MAX_CLIENTS) {
            // 达到最大人数，拒绝新的连接
            cout << "【系统消息】: 当前在线人数已达到上限，拒绝连接!" << endl;
            send(s_accept, "连接已满，无法加入聊天室", 30, 0);
            closesocket(s_accept);  // 关闭新客户端的 socket
            return;
        }
        onlineCount++;  // 新客户端连接，在线人数增加
    }

    {
        lock_guard<mutex> lock(clientSocketsMutex);
        clientSockets[s_accept] = nickname;  // 保存socket与昵称的对应关系
    }

    broadcastOnlineCount();  // 广播当前在线人数
}


// 客户端断开处理
void handleClientDisconnect(SOCKET s_accept) {
    {
        lock_guard<mutex> lock(clientSocketsMutex);
        clientSockets.erase(s_accept);  // 移除socket
    }

    {
        lock_guard<mutex> lock(onlineCountMutex);
        onlineCount--;  // 客户端断开，在线人数减少
    }
    cout << "【系统消息】: 客户端断开连接，当前在线人数：" << onlineCount << endl;
    broadcastOnlineCount();  // 广播当前在线人数
}

// 接收消息线程
DWORD WINAPI serveraccept(LPVOID IpParameter) {
    SOCKET s_accept = *(SOCKET*)IpParameter;
    char recvBuf[1000];
    memset(recvBuf, 0, sizeof(recvBuf));
    int recvLen = 0;
    bool clientOnline = true;  // 维护当前客户端的在线状态
    bool clientOffline = false;  // 用于追踪客户端是否暂时离线

    while (clientOnline) {
        recvLen = recv(s_accept, recvBuf, sizeof(recvBuf), 0);

        if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            // 处理非阻塞错误或暂时不可读情况
            continue;
        }
        else if (recvLen <= 0) {
            if (clientOffline) {
                // 如果客户端是处于 OFFLINE 状态，可能发生超时或未响应，处理为断开
                cout << "【系统消息】: 客户端已超时断开连接!!" << endl;
                handleClientDisconnect(s_accept);
                clientOnline = false;
            }
            else {
                // 客户端没有发送离线消息，直接断开
                cout << "【系统消息】: 客户端断开连接或连接失败!!" << endl;
                handleClientDisconnect(s_accept);
                clientOnline = false;
            }
        }
        else {
            // 正常处理收到的消息
            string s(recvBuf, recvLen);  // 确保处理实际收到的字节
            message r = stringToMsg(s);

            if (r.type == CHAT) {
                cout << r.time << "【" << r.name << "】:" << r.msg << endl;
                // 广播消息给所有客户端
                lock_guard<mutex> lock(clientSocketsMutex);
                for (const auto& client : clientSockets) {
                    send(client.first, recvBuf, recvLen, 0);
                }
            }
            else if (r.type == OFFLINE) {
                // 客户端发送了 OFFLINE 消息，暂时离线
                cout << "【系统消息】: 客户端【" << r.name << "】暂时离线" << endl;
                clientOffline = true;  // 标记客户端为离线
            }
            else if (r.type == EXIT) {
                // 客户端请求退出，处理退出
                cout << "【系统消息】: 客户端【" << r.name << "】已退出" << endl;
                handleClientDisconnect(s_accept);  // 完全处理客户端断开连接
                clientOnline = false;  // 退出该客户端的循环
            }
        }
        memset(recvBuf, 0, sizeof(recvBuf));  // 清空缓冲区
    }
    closesocket(s_accept);  // 关闭该客户端的socket
    return 0;
}

// 发送消息线程
DWORD WINAPI serversend(LPVOID IpParameter) {
    SOCKET s_accept = *(SOCKET*)IpParameter;
    bool clientOnline = true;  // 独立控制每个客户端的在线状态

    while (clientOnline) {
        char send_buf[1000];
        memset(send_buf, 0, sizeof(send_buf));
        message m;
        m.name = "管理员";

        char in[1000];
        cin.getline(in, 1000);
        m.msg = in;
        m.time = getCurrentTime();
        if (m.msg == "off" || m.msg == "OFF") {
            m.type = OFFLINE;
            clientOnline = false;  // 仅当前客户端设置为离线
        }
        else if (m.msg == "exit" || m.msg == "EXIT") {
            m.type = EXIT;
            clientOnline = false;  // 仅当前客户端退出
        }
        else {
            m.type = CHAT;
        }
        strcpy_s(send_buf, msgToString(m).c_str());
        strcpy_s(send_buf, msgToString(m).c_str());

        lock_guard<mutex> lock(clientSocketsMutex);
        for (const auto& client : clientSockets) {
            send(client.first, send_buf, sizeof(send_buf), 0);
        }

        cout << m.time << "【" << m.name << "】:" << m.msg << endl;
        memset(send_buf, 0, sizeof(send_buf));

        if (m.msg == "off" || m.msg == "OFF") {
            cout << "【系统消息】:您选择了OFF,连接离线!" << endl;
            break;
        }
        else if (m.msg == "EXIT" || m.msg == "exit") {
            cout << "【系统消息】:您选择了EXIT，连接关闭!" << endl;
            break;
        }
    }
    closesocket(s_accept);
    return 0;
}

int main() {
    SOCKET s_accept;
    SOCKET s_server;
    SOCKADDR_IN server_addr;
    SOCKADDR_IN accept_addr;

    cout << "---------------------------------------------------" << endl;
    cout << "||               Mini-We-Chat(￣︶￣)            ||" << endl;
    cout << "---------------------------------------------------" << endl;
    if (!initwsa()) {
        return 0;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(5010);

    s_server = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(s_server, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cout << "||【系统消息】: 服务器绑定失败!!" << endl;
        WSACleanup();
        return 0;
    }
    else {
        cout << "||【系统消息】: 服务器绑定成功!!" << endl;
    }

    if (listen(s_server, SOMAXCONN) < 0) {
        cout << "||【系统消息】: 设置监听失败!!" << endl;
        closesocket(s_server);
        WSACleanup();
        return 0;
    }
    else {
        cout << "||【系统消息】: 设置监听成功!!" << endl;
    }
    cout << "||【系统消息】:服务端正在监听连接，请稍后..." << endl;

    while (1) {
        int len = sizeof(SOCKADDR);
        s_accept = accept(s_server, (SOCKADDR*)&accept_addr, &len);

        if (s_accept == SOCKET_ERROR) {
            cout << "||【系统消息】: 连接失败!!" << endl;
            WSACleanup();
            return 0;
        }
        //flag = 1;

        char nickname[256];
        int nickname_len = recv(s_accept, nickname, sizeof(nickname) - 1, 0);  // 接收客户端传来的昵称

        if (nickname_len > 0) {
            nickname[nickname_len] = '\0';  // 确保字符串正确结束
            string userNickname = nickname;
            handleClientConnect(s_accept, userNickname);
        }

        cout << "||【系统消息】: 用户【" << nickname << "】连接成功!" << endl;

        CloseHandle(CreateThread(NULL, 0, serversend, (LPVOID)&s_accept, 0, 0));
        CloseHandle(CreateThread(NULL, 0, serveraccept, (LPVOID)&s_accept, 0, 0));
    }
    WSACleanup();
    return 0;
}
