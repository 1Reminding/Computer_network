#include <winsock.h>
#include <iostream>
#include <map>
#include <string>
#include <mutex>
#include <ctime>
#include <sstream>  // �����ַ�����
#pragma comment(lib,"ws2_32.lib")  // ���� ws2_32 ��

using namespace std;

map<SOCKET, string> clientSockets;  // �洢socket���û�����ӳ��
mutex clientSocketsMutex;  // ��������������clientSockets�ķ���
int onlineCount = 0;  // ��ǰ��������
mutex onlineCountMutex;  // �������������Ļ�����

//int flag = 0;  // �жϿͻ����Ƿ�����

// ��Ϣ����
enum Type {
    CHAT = 1,
    EXIT = 2,
    OFFLINE = 3,
    SYSTEM_MSG = 4  // ϵͳ��Ϣ����
};

// ��Ϣ�ṹ
struct message {
    Type type;
    string msg;
    string name;
    string time;
};

// message����ת��Ϊstring���ͣ��ֶμ���'\n'Ϊ�ָ���
string msgToString(const message& m) {
    stringstream s;
    s << m.type << "\n" << m.name << "\n" << m.msg << "\n" << m.time << "\n";
    return s.str();
}

// string����ת��Ϊmessage����
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

// ��ʼ�� Winsock
int initwsa() {
    WSADATA wsaDATA;
    if (!WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        cout << "||��ϵͳ��Ϣ��:��ʼ�����绷���ɹ�!!" << endl;
        return 1;
    }
    else {
        cout << "||��ϵͳ��Ϣ��:��ʼ�����绷��ʧ��!!" << endl;
        return 0;
    }
}

// ��ȡ��ǰʱ���ַ���
string getCurrentTime() {
    time_t now = time(0);
    tm local_time;
    localtime_s(&local_time, &now);
    char timeString[100];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &local_time);
    return string(timeString);
}

// �㲥��ǰ��������
void broadcastOnlineCount() {
    lock_guard<mutex> lock(clientSocketsMutex);  // ��֤�㲥�ڼ��socket map�İ�ȫ����

    stringstream ss;
    ss << "��ϵͳ��Ϣ��: ��ǰ����������" << onlineCount;
    message systemMessage;
    systemMessage.type = SYSTEM_MSG;
    systemMessage.name = "ϵͳ";
    systemMessage.msg = ss.str();
    systemMessage.time = getCurrentTime();
    string systemMsgString = msgToString(systemMessage);

    for (const auto& client : clientSockets) {
        send(client.first, systemMsgString.c_str(), systemMsgString.size(), 0);
    }
}
const int MAX_CLIENTS = 50;  // ���������������

void handleClientConnect(SOCKET s_accept, const string& nickname) {
    {
        lock_guard<mutex> lock(onlineCountMutex);
        if (onlineCount >= MAX_CLIENTS) {
            // �ﵽ����������ܾ��µ�����
            cout << "��ϵͳ��Ϣ��: ��ǰ���������Ѵﵽ���ޣ��ܾ�����!" << endl;
            send(s_accept, "�����������޷�����������", 30, 0);
            closesocket(s_accept);  // �ر��¿ͻ��˵� socket
            return;
        }
        onlineCount++;  // �¿ͻ������ӣ�������������
    }

    {
        lock_guard<mutex> lock(clientSocketsMutex);
        clientSockets[s_accept] = nickname;  // ����socket���ǳƵĶ�Ӧ��ϵ
    }

    broadcastOnlineCount();  // �㲥��ǰ��������
}


// �ͻ��˶Ͽ�����
void handleClientDisconnect(SOCKET s_accept) {
    {
        lock_guard<mutex> lock(clientSocketsMutex);
        clientSockets.erase(s_accept);  // �Ƴ�socket
    }

    {
        lock_guard<mutex> lock(onlineCountMutex);
        onlineCount--;  // �ͻ��˶Ͽ���������������
    }
    cout << "��ϵͳ��Ϣ��: �ͻ��˶Ͽ����ӣ���ǰ����������" << onlineCount << endl;
    broadcastOnlineCount();  // �㲥��ǰ��������
}

// ������Ϣ�߳�
DWORD WINAPI serveraccept(LPVOID IpParameter) {
    SOCKET s_accept = *(SOCKET*)IpParameter;
    char recvBuf[1000];
    memset(recvBuf, 0, sizeof(recvBuf));
    int recvLen = 0;
    bool clientOnline = true;  // ά����ǰ�ͻ��˵�����״̬
    bool clientOffline = false;  // ����׷�ٿͻ����Ƿ���ʱ����

    while (clientOnline) {
        recvLen = recv(s_accept, recvBuf, sizeof(recvBuf), 0);

        if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            // ����������������ʱ���ɶ����
            continue;
        }
        else if (recvLen <= 0) {
            if (clientOffline) {
                // ����ͻ����Ǵ��� OFFLINE ״̬�����ܷ�����ʱ��δ��Ӧ������Ϊ�Ͽ�
                cout << "��ϵͳ��Ϣ��: �ͻ����ѳ�ʱ�Ͽ�����!!" << endl;
                handleClientDisconnect(s_accept);
                clientOnline = false;
            }
            else {
                // �ͻ���û�з���������Ϣ��ֱ�ӶϿ�
                cout << "��ϵͳ��Ϣ��: �ͻ��˶Ͽ����ӻ�����ʧ��!!" << endl;
                handleClientDisconnect(s_accept);
                clientOnline = false;
            }
        }
        else {
            // ���������յ�����Ϣ
            string s(recvBuf, recvLen);  // ȷ������ʵ���յ����ֽ�
            message r = stringToMsg(s);

            if (r.type == CHAT) {
                cout << r.time << "��" << r.name << "��:" << r.msg << endl;
                // �㲥��Ϣ�����пͻ���
                lock_guard<mutex> lock(clientSocketsMutex);
                for (const auto& client : clientSockets) {
                    send(client.first, recvBuf, recvLen, 0);
                }
            }
            else if (r.type == OFFLINE) {
                // �ͻ��˷����� OFFLINE ��Ϣ����ʱ����
                cout << "��ϵͳ��Ϣ��: �ͻ��ˡ�" << r.name << "����ʱ����" << endl;
                clientOffline = true;  // ��ǿͻ���Ϊ����
            }
            else if (r.type == EXIT) {
                // �ͻ��������˳��������˳�
                cout << "��ϵͳ��Ϣ��: �ͻ��ˡ�" << r.name << "�����˳�" << endl;
                handleClientDisconnect(s_accept);  // ��ȫ����ͻ��˶Ͽ�����
                clientOnline = false;  // �˳��ÿͻ��˵�ѭ��
            }
        }
        memset(recvBuf, 0, sizeof(recvBuf));  // ��ջ�����
    }
    closesocket(s_accept);  // �رոÿͻ��˵�socket
    return 0;
}

// ������Ϣ�߳�
DWORD WINAPI serversend(LPVOID IpParameter) {
    SOCKET s_accept = *(SOCKET*)IpParameter;
    bool clientOnline = true;  // ��������ÿ���ͻ��˵�����״̬

    while (clientOnline) {
        char send_buf[1000];
        memset(send_buf, 0, sizeof(send_buf));
        message m;
        m.name = "����Ա";

        char in[1000];
        cin.getline(in, 1000);
        m.msg = in;
        m.time = getCurrentTime();
        if (m.msg == "off" || m.msg == "OFF") {
            m.type = OFFLINE;
            clientOnline = false;  // ����ǰ�ͻ�������Ϊ����
        }
        else if (m.msg == "exit" || m.msg == "EXIT") {
            m.type = EXIT;
            clientOnline = false;  // ����ǰ�ͻ����˳�
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

        cout << m.time << "��" << m.name << "��:" << m.msg << endl;
        memset(send_buf, 0, sizeof(send_buf));

        if (m.msg == "off" || m.msg == "OFF") {
            cout << "��ϵͳ��Ϣ��:��ѡ����OFF,��������!" << endl;
            break;
        }
        else if (m.msg == "EXIT" || m.msg == "exit") {
            cout << "��ϵͳ��Ϣ��:��ѡ����EXIT�����ӹر�!" << endl;
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
    cout << "||               Mini-We-Chat(�����)            ||" << endl;
    cout << "---------------------------------------------------" << endl;
    if (!initwsa()) {
        return 0;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(5010);

    s_server = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(s_server, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cout << "||��ϵͳ��Ϣ��: ��������ʧ��!!" << endl;
        WSACleanup();
        return 0;
    }
    else {
        cout << "||��ϵͳ��Ϣ��: �������󶨳ɹ�!!" << endl;
    }

    if (listen(s_server, SOMAXCONN) < 0) {
        cout << "||��ϵͳ��Ϣ��: ���ü���ʧ��!!" << endl;
        closesocket(s_server);
        WSACleanup();
        return 0;
    }
    else {
        cout << "||��ϵͳ��Ϣ��: ���ü����ɹ�!!" << endl;
    }
    cout << "||��ϵͳ��Ϣ��:��������ڼ������ӣ����Ժ�..." << endl;

    while (1) {
        int len = sizeof(SOCKADDR);
        s_accept = accept(s_server, (SOCKADDR*)&accept_addr, &len);

        if (s_accept == SOCKET_ERROR) {
            cout << "||��ϵͳ��Ϣ��: ����ʧ��!!" << endl;
            WSACleanup();
            return 0;
        }
        //flag = 1;

        char nickname[256];
        int nickname_len = recv(s_accept, nickname, sizeof(nickname) - 1, 0);  // ���տͻ��˴������ǳ�

        if (nickname_len > 0) {
            nickname[nickname_len] = '\0';  // ȷ���ַ�����ȷ����
            string userNickname = nickname;
            handleClientConnect(s_accept, userNickname);
        }

        cout << "||��ϵͳ��Ϣ��: �û���" << nickname << "�����ӳɹ�!" << endl;

        CloseHandle(CreateThread(NULL, 0, serversend, (LPVOID)&s_accept, 0, 0));
        CloseHandle(CreateThread(NULL, 0, serveraccept, (LPVOID)&s_accept, 0, 0));
    }
    WSACleanup();
    return 0;
}
