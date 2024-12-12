#include <iostream>
#include <string>
#include <cstring>
#include <winsock.h>
#include<fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

int maxsize = 2048;//���仺����

//�������ݰ���ֵ�Ƿ�Ϊ1��������Ӧ���統ACKΪ1ʱ����ΪACK��ACK��SYN��Ϊ1����ΪACK_SYN
unsigned char SYN = 0x1; 
unsigned char ACK = 0x2;
unsigned char ACK_SYN = 0x3;
unsigned char FIN = 0x4;
unsigned char FIN_SYN = 0x5;
unsigned char ACK_FIN = 0x6;
unsigned char END = 0x7;

int Handssuc = 0;
int Hands_shakecount = 10;

double retime = 3500;

//��ʼ��socket
int initwsa() {
    WSADATA wsaDATA;
    if (!WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        cout << "||��ϵͳ֪ͨ��:���绷����ʼ���ɹ�����    " << endl;
		return 1;
	}
	else {
		cout << "||��ϵͳ֪ͨ��:���绷����ʼ��ʧ�ܣ���׼���˳�����    " << endl;
		return 0;
	}
}

struct Head
{
    u_short CheckSum;//У��� 16λ
    u_short DataLen;//���������ݳ��� 16λ
    unsigned char Sign;//����λ��ʾ���ݰ�����,8λ
    unsigned char Seq;//���кţ�������֤�Ƿ������������кţ�����������,8λ
    Head()
    {
        CheckSum = 0;
        DataLen = 0;
        Sign = 0;
        Seq = 0;
    }
};
void PrintPacketLog(const Head& head, const string& action)
{
    string SIGN;
    switch (head.Sign) {
    case 1: SIGN = "��SYN��"; break;
    case 2: SIGN = "��ACK��"; break;
    case 3: SIGN = "��SYN ACK��"; break;
    case 4: SIGN = "��FIN��"; break;
    case 5: SIGN = "��FIN SYN��"; break;
    case 6: SIGN = "��FIN ACK��"; break;
    case 7: SIGN = "��RESEND��"; break;
    case 8: SIGN = "��END��"; break;
    default: SIGN = "��SEND��"; break;
    }
    if(action=="����")
    cout << "��" << action << "����־λ = " << SIGN << " ���к� = " << int(head.Seq) << " У��� = " << int(head.CheckSum) << endl;
    if (action == "����")
    cout << "��" << action << "����־λ = " << SIGN << " ���к� = " << int(head.Seq) << endl;
}

//����У���
u_short CalculateChecksum(u_short* head, int size)
{
    int count = (size + 1) / 2;//����ѭ��������ÿ��ѭ����������16λ������
    u_short* buf = (u_short*)malloc(size + 1);//��̬�����ַ�������
    memset(buf, 0, size + 1);//�������
    memcpy(buf, head, size);
    u_long checkSum = 0;
    while (count--) {
        checkSum += *buf++;
        if (checkSum & 0xffff0000) {//�����ӽ���ĸ�ʮ��λ����һ����ʮ��λ���㣬�������λ��һ
            checkSum &= 0xffff;
            checkSum++;
        }
    }
    return ~(checkSum & 0xffff);//�����Ľ��ȡ��
}

//��ʼ���µİ�
void Newpacket(Head& head, const unsigned char str, char* buf)
{
    head.Sign = str;
    head.CheckSum = 0;
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
    memcpy(buf, &head, sizeof(head));
}
void Newpacketcheck(Head& head, const unsigned char str, char* buf, int seq)
{
    head.Sign = str; 
    head.DataLen = 0; 
    head.Seq = (unsigned char)seq; //���к��뵱ǰ���к���ͬ
    head.CheckSum = 0; 
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));//���¼���У���
    memcpy(buf, &head, sizeof(head)); //����������
}
void Threeway_Handshake(SOCKET& socket, SOCKADDR_IN& addr)
{
    Handssuc = 0;
    int handscount1 = 0;//��¼��ʱ�ش�����
    int addrlength = sizeof(addr); 
    Head head; 
    char* buff = new char[sizeof(head)]; 
    //��һ��������Ϣ
    if (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &addrlength) == SOCKET_ERROR)
    {//��һ�����ֽ���ʧ��
        cout << "����һ������ʧ�ܡ�" << endl;
        return;
    }
    memcpy(&head, buff, sizeof(head)); //������ͷ��
    if (head.Sign == SYN && CalculateChecksum((u_short*)&head, sizeof(head)) == 0)//У��ͺͱ�ʶ������ȷ
    {
        cout << "��һ�����ֳɹ���SYN��" << endl;
    }
    u_long mode = 1;
    ioctlsocket(socket, FIONBIO, &mode); //���÷�����ģʽ
    //�ڶ���������Ϣ
    head.Sign = ACK; //����ACK=1
    head.CheckSum = 0;
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
    memcpy(buff, &head, sizeof(head));
    if (sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, addrlength) == SOCKET_ERROR)
    {
        cout << "���ڶ�������ʧ�ܡ�" << endl;
        return;
    }
    cout << "�ڶ������ֳɹ���SYN ACK��" << endl;
    clock_t starttime = clock();
    u_long imode = 1;

    ioctlsocket(socket, FIONBIO, &imode);//������ģʽ
    //����������
    while (clock() - starttime > retime)
    {
        if (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &addrlength))
        {
            cout << "������������ʧ�ܡ�" << endl;
            break;
        }
        handscount1++;
        memcpy(buff, &head, sizeof(head));
        starttime = clock();
        if (handscount1 == Hands_shakecount) {
            cout << "���ȴ���ʱ��" << endl;
            return;
        }
    }
    cout << "���������ֳɹ���ACK��" << endl;
    Handssuc = 1;
    cout << "�������ӷ��Ͷˡ�" << endl;
    cout << "---------------------------------------------------" << endl;
}
//�����ļ�
int Receivefile(SOCKET& socket, SOCKADDR_IN& addr, char* data)
{
    int addrlength = sizeof(addr);
    long int sum = 0;
    Head head;
    char* buf = new char[maxsize + sizeof(head)]; //�������鳤��������+ͷ��������С
    int seq = 0; 
    while (1)
    {
        int recvlength = recvfrom(socket, buf, sizeof(head) + maxsize, 0, (sockaddr*)&addr, &addrlength);//���ձ��ĳ���
       
        if (recvlength == SOCKET_ERROR) {
            // ������ģʽ�£�����ʧ��ʱ�᷵��SOCKET_ERROR������WSAGetLastError()����WSAWOULDBLOCK
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // �����WSAWOULDBLOCK���󣬱�ʾû�����ݿ��Խ��գ�����ѭ���ȴ�
                continue;
            }
            else {
                cout << "���������ݰ�ʧ�ܣ��� �����룺" << error << endl;
                return -1;
            }
        }

        memcpy(&head, buf, sizeof(head)); // �������ݰ�ͷ��
        cout << "�����յ����ݰ���" << " ���� = " << recvlength << " �ֽ�" << endl;
        if (head.Sign == END && CalculateChecksum((u_short*)&head, sizeof(head)) == 0)//END��־λ��У���Ϊ0������
        {
            cout << "������ɹ���" << endl;
            break; //��������whileѭ��
        }

        if (head.Sign == static_cast<unsigned char>(0) && CalculateChecksum((u_short*)buf, recvlength - sizeof(head)))//У��Ͳ�Ϊ0��flag���޷����ַ�
        {
            //�ж��յ������ݰ��Ƿ���ȷ
            if (seq != int(head.Seq))
            {//seq����ȣ����ݰ���������
                Newpacketcheck(head, ACK, buf, seq);
                //���·���ACK
                string SIGN;
                switch (head.Sign) {
                case 1: SIGN = "��SYN��"; break;
                case 2: SIGN = "��ACK��"; break;
                case 3: SIGN = "��SYN ACK��"; break;
                case 4: SIGN = "��FIN��"; break;
                case 5: SIGN = "��FIN SYN��"; break;
                case 6: SIGN = "��FIN ACK��"; break;
                case 7: SIGN = "��END��"; break;
                default: SIGN = "��SEND��"; break;
                }
                // ���·���ACK
                PrintPacketLog(head, "���·���");
                sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength);
             
                continue; //����
            }
            seq = int(head.Seq);
            seq %= 256;//ȡģ
            
            // ������ȷ�����ݰ�
            PrintPacketLog(head, "����");
            char* buf_data = new char[recvlength - sizeof(head)]; //����Ĵ�С�ǽ��յ��ı��ĳ��ȼ�ȥͷ����С
            memcpy(buf_data, buf + sizeof(head), recvlength - sizeof(head)); //��ͷ�����濪ʼ�����������ݿ�������������
            memcpy(data + sum, buf_data, recvlength - sizeof(head));
            sum = sum + int(head.DataLen);

            //��ʼ���ײ�
            Newpacketcheck(head, ACK, buf, seq);
            //����ACK
            sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength);
            PrintPacketLog(head, "����");
            seq++;//���кż�
            seq %= 256; //����255Ҫȡģ
        }
    }
    //����END��Ϣ������
    Newpacket(head, END, buf);
    if (sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength) == SOCKET_ERROR)
    {
        cout << "�����ʹ���" << endl;
        return -1; //���ʹ���
    }
    return sum; //���ؽ��յ������ݰ��ֽ�����
}

//�ر����� ���λ���
void Fourway_Wavehand(SOCKET& socket, SOCKADDR_IN& addr)
{
    int addrlength = sizeof(addr);
    Head head;
    char* buf = new char[sizeof(head)];

    //��һ�λ���
    while (1)
    {
        recvfrom(socket, buf, sizeof(head) + maxsize, 0, (sockaddr*)&addr, &addrlength);//���ձ��ĳ���
        memcpy(&head, buf, sizeof(head));
        if (head.Sign == FIN && CalculateChecksum((u_short*)&head, sizeof(head)) == 0)
        {
            cout << "��һ�λ��ֳɹ���FIN ACK��" << endl;
            break;
        }
    }

    //�ڶ��λ���
    head.Sign = ACK;
    head.CheckSum = 0;
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
    memcpy(buf, &head, sizeof(head));
    if (sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength) == -1)
    {
        cout << "���ڶ������λ���ʧ�ܡ�" << endl;
        return;
    }
    cout << "�ڶ������λ��ֳɹ���FIN ACK��" << endl;
    clock_t starttime = clock();//��¼�ڶ��λ��ַ���ʱ��

    //�����λ���
    while (recvfrom(socket, buf, sizeof(head), 0, (sockaddr*)&addr, &addrlength) <= 0)
    {
        if (clock() - starttime > retime)
        {
            memcpy(buf, &head, sizeof(head));
            if (sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength) == -1)
            {
                cout << "�����Ĵλ���ʧ�ܡ�" << endl;
                return;
            }
        }
    }
    Head head1; //���޸�head�ṹ��ʹ����ʱͷ��head1����
    memcpy(&head1, buf, sizeof(head));
    if (head1.Sign == ACK_FIN && CalculateChecksum((u_short*)&head1, sizeof(head1) == 0))
    {
        cout << "���Ĵλ��ֳɹ���ACK��" << endl;
    }
    else
    {
        cout << "�����ӹر�ʧ�ܣ�������ֹ��" << endl;
        return;
    }
    cout << "���������ӡ�" << endl;
}

int main() {
    cout << "---------------------------------------------------" << endl;
    cout << "||             -->Receiving_End                  ||" << endl;
    cout << "---------------------------------------------------" << endl;
    SOCKET c_client;
    sockaddr_in client_addr;
    // �����������׽���
    if (!initwsa()) {
        return 0;//��ʼ��ʧ�����˳�����
    }
    char ip[100];
    u_short port;
    cout << "�����뱾����IP��ַ��";
    cin >> ip;
    cout << "������˿ںţ�";
    cin >> port;
    cout << "���ȴ����Ͷ�����...��" << endl;
    // ���ý��ն˵�ַ
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);//�˿ں�
    client_addr.sin_addr.s_addr = inet_addr(ip);//IP��ַ
    c_client = socket(AF_INET, SOCK_DGRAM, 0);//ʹ�����ݱ��׽��֣��ʺ�����UDPЭ�顣

    //���׽���
    bind(c_client, (sockaddr*)&client_addr, sizeof(client_addr));

    while (1)
    {
        int len = sizeof(client_addr);
        //��������
        Threeway_Handshake(c_client, client_addr);
        if (Handssuc == 0) {
            cout << "���������ӡ�" << endl;
            continue;
        }
        char* name = new char[20];
        char* data = new char[100000000];
        int filenamelength = Receivefile(c_client, client_addr, name);
        int datalength = Receivefile(c_client, client_addr, data);
        string file;
        for (int i = 0; i < filenamelength; i++)
        {
            file = file + name[i];
        }

        //�ر�����
        Fourway_Wavehand(c_client, client_addr);
        ofstream out(file.data(), ofstream::binary); //��������ö����Ƶķ���
        if (out.is_open()) {
            out.write(data, datalength); // �������Զ����Ʒ�ʽд���ļ�
            out.close();
        }
        else {
            // �ļ���ʧ�ܵĴ���
            cout << "���ļ��򿪳���" << endl;
            return 0;
        }
        out.close();
        cout << "���ļ�����ɹ���" << endl;
        cout << "---------------------------------------------------" << endl;
    }
    closesocket(c_client); //�ر��׽���
    WSACleanup();
    return 0;

}
