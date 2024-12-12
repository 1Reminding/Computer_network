#include <winsock.h>
#include <iostream>
#include <ctime>   // ����ʱ�������
#include <string>

#pragma comment(lib,"ws2_32.lib")  // ��ӱ���������� ws2_32 ��

using namespace std;

bool offlineMode = false;  // ��ʾ�ͻ����Ƿ�������״̬
std::string userID;  // ʹ���û�������ǳ���ΪuserID
int flag = 1;  // ��ʾ�ͻ����Ƿ����ߣ�0��ʾ�˳���1��ʾ����

// ��Ϣ����
enum Type {
    CHAT = 1,
    EXIT,
    OFFLINE,
    SYSTEM_MSG  // ϵͳ��Ϣ����
};

// �Զ�����Ϣ�ṹ
struct message {
    Type type;
    string msg;
    string name;
    string time;
};

// message����ת��Ϊstring���ͣ��ֶμ���'\n'Ϊ�ָ���
string msgToString(message m) {
    string s;
    s += to_string(m.type) + "\n" + m.name + "\n" + m.msg + "\n" + m.time + "\n";
    return s;
}

// string����ת��Ϊmessage����
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

// ��ʼ��Winsock
int initwsa() {
    WSADATA wsaDATA;
    if (!WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        cout << "---------------------------------------------------" << endl;
        cout << "��ϵͳ��Ϣ��:��ʼ�����绷���ɹ�����" << endl;
        return 1;
    }
    else {
        cout << "---------------------------------------------------" << endl;
        cout << "��ϵͳ��Ϣ��:��ʼ�����绷��ʧ�ܣ���" << endl;
        return 0;
    }
}

// ���ߺ����������
void reconnect(SOCKET& s_server, SOCKADDR_IN& server_addr) {
    while (true) {
        cout << "��ϵͳ��Ϣ��:������������..." << endl;
        s_server = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(s_server, (SOCKADDR*)&server_addr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
            cout << "��ϵͳ��Ϣ��:����ʧ�ܣ�5�������..." << endl;
            Sleep(5000);  // ÿ5������һ��
        }
        else {
            cout << "��ϵͳ��Ϣ��:�����ɹ���" << endl;
            // ����userID������������֪��������
            send(s_server, userID.c_str(), userID.size(), 0);
            offlineMode = false;  // �������ߣ�ȡ������״̬
            break;
        }
    }
}

// ���������Ϣ���߳�
DWORD WINAPI clientaccept(LPVOID IpParameter) {
    SOCKET* sockConnPtr = (SOCKET*)IpParameter;
    SOCKET& sockConn = *sockConnPtr;  // ʹ�� socket ������
    char recvBuf[1000];  // ������Ϣ�Ļ�����
    memset(recvBuf, 0, sizeof(recvBuf));
    int recvLen = 0;

    // ���ͻ��˻�����ʱ������������Ϣ
    while (flag) {
        recvLen = recv(sockConn, recvBuf, 1000, 0);  // ������1000�ֽڵ�����
        if (recvLen <= 0) {
            if (offlineMode) {
                // �����������״̬��������������
                cout << "---------------------------------------------------" << endl;
                cout << "��ϵͳ��Ϣ��:��������Ͽ����ӣ���������..." << endl;
                reconnect(sockConn, *(SOCKADDR_IN*)IpParameter);  // �����Զ�����
            }
            else {
                cout << "---------------------------------------------------" << endl;
                cout << "��ϵͳ��Ϣ��:��������Ͽ�����!" << endl;
                closesocket(sockConn);
                flag = 0;  // ����Ϊ�˳�״̬
                break;
            }
        }
        else {
            string s = recvBuf;
            message r = stringToMsg(s);

            // �����Ϣ�����Ա����
           // cout << "�����ԡ�: �յ���Ϣ����: " << r.type << endl;

            switch (r.type) {
            case CHAT:
                cout << r.time << "��" << r.name << "��:" << r.msg << endl;
                break;
            case OFFLINE:
                cout << "---------------------------------------------------" << endl;
                cout << "��ϵͳ��Ϣ��:�����������! ���� ENTER �ر�����!" << endl;
                flag = 0;
                break;
            case EXIT:
                cout << "---------------------------------------------------" << endl;
                cout << "��ϵͳ��Ϣ��:�����������! ���� ENTER �ر�����!" << endl;
                flag = 0;
                break;
            case SYSTEM_MSG:
                // ����ϵͳ��Ϣ����ʾ��ǰ��������
                cout << "��ϵͳ��Ϣ��: " << r.msg << endl;
                break;
            }
        }
        memset(recvBuf, 0, sizeof(recvBuf));  // ��ս��ջ�����
    }
    return 0;
}


// �ͻ���������
int main() {
    SOCKET s_server;
    SOCKADDR_IN server_addr;

    cout << "---------------------------------------------------" << endl;
    cout << "||                   Mini-We-Chat                ||" << endl;
    cout << "---------------------------------------------------" << endl;
    cout << "��ϵͳ��Ϣ��:�����������ǳƣ�";
    cin >> userID;  // ���û�������ǳ���ΪuserID
    cout << "��ϵͳ��Ϣ��:������Ҫ���ӵ�IP��ַ:";
    char ipaddress[50];
    cin >> ipaddress;

    // ��ʼ�� Winsock
    if (!initwsa()) {
        return 0;
    }

    // ���÷�������ַ
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.S_un.S_addr = inet_addr(ipaddress);
    server_addr.sin_port = htons(5010);
    s_server = socket(AF_INET, SOCK_STREAM, 0);

    // ��������
    if (connect(s_server, (SOCKADDR*)&server_addr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "��ϵͳ��Ϣ��:����������ʧ��!!" << endl;
        closesocket(s_server);
        reconnect(s_server, server_addr);  // ��������
    }
    else {
        cout << "��ϵͳ��Ϣ��:���������ӳɹ�!!" << endl;
        flag = 1;
        cout << "��ϵͳ��Ϣ��:����OFF���ߣ�����EXIT�ر�����!" << endl;
        cout << "---------------------------------------------------" << endl;
        cout << "��ϵͳ��Ϣ��:��ӭ��" << userID << "�����������ң�" << endl;
        // ����userID������������֪ͨ��¼�ɹ�
        send(s_server, userID.c_str(), userID.size(), 0);
    }

    char send_buf[1000];
    memset(send_buf, 0, sizeof(send_buf));
    message m;
    m.name = userID;
    int count = 0;

    // �����̸߳��������Ϣ
    CloseHandle(CreateThread(NULL, 0, clientaccept, (LPVOID)&s_server, 0, 0));

    // ���ͻ��˻�����ʱ����ʱ���Է�����Ϣ
        // ����Ϣ
        while (flag) {
            // ��ȡ��ǰʱ��
            char timeString[100];
            time_t now = time(0);
            tm now_time;
            localtime_s(&now_time, &now);
            strftime(timeString, sizeof(timeString), "%Y��%m��%d�� %H:%M:%S", &now_time);
        
            // ����Ϣ
            char in[1000];
            cin.getline(in, 1000);
            m.msg = in;
            m.time = timeString;

            if (m.msg == "off" || m.msg == "OFF") {
                m.type = OFFLINE;
                offlineMode = true;  // ���ÿͻ��˽�������ģʽ
                send(s_server, msgToString(m).c_str(), msgToString(m).size(), 0);  // ����OFFLINE��Ϣ
                cout << "---------------------------------------------------" << endl;
                cout << "��ϵͳ��Ϣ��: ��ѡ����OFF,��������!" << endl;
            }
            else if (m.msg == "exit" || m.msg == "EXIT") {
                m.type = EXIT;
                flag = 0;  // �����˳���־
                send(s_server, msgToString(m).c_str(), msgToString(m).size(), 0);  // ����EXIT��Ϣ
                cout << "---------------------------------------------------" << endl;
                cout << "��ϵͳ��Ϣ��: ��ѡ����EXIT�����ӹرգ�" << endl;
                break;  // �˳�ѭ�����ر�����
            }
            else {
                m.type = CHAT;
                send(s_server, msgToString(m).c_str(), msgToString(m).size(), 0);  // ����������Ϣ
            }

            memset(send_buf, 0, sizeof(send_buf));  // ��շ��ͻ�����
        }

        // �ر�socket��������Դ
        closesocket(s_server);
        WSACleanup();
        return 0;
    }