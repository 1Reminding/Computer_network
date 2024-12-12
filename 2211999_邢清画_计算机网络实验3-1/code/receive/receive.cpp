#include <iostream>
#include <string>
#include <cstring>
#include <winsock.h>
#include<fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

int maxsize = 2048;//传输缓冲区

//根据数据包的值是否为1与命名对应，如当ACK为1时命名为ACK，ACK和SYN均为1命名为ACK_SYN
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

//初始化socket
int initwsa() {
    WSADATA wsaDATA;
    if (!WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        cout << "||【系统通知】:网络环境初始化成功！！    " << endl;
		return 1;
	}
	else {
		cout << "||【系统通知】:网络环境初始化失败！！准备退出！！    " << endl;
		return 0;
	}
}

struct Head
{
    u_short CheckSum;//校验和 16位
    u_short DataLen;//所包含数据长度 16位
    unsigned char Sign;//后三位表示数据包类型,8位
    unsigned char Seq;//序列号（用于验证是否出错及出错的序列号，便于修正）,8位
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
    case 1: SIGN = "【SYN】"; break;
    case 2: SIGN = "【ACK】"; break;
    case 3: SIGN = "【SYN ACK】"; break;
    case 4: SIGN = "【FIN】"; break;
    case 5: SIGN = "【FIN SYN】"; break;
    case 6: SIGN = "【FIN ACK】"; break;
    case 7: SIGN = "【RESEND】"; break;
    case 8: SIGN = "【END】"; break;
    default: SIGN = "【SEND】"; break;
    }
    if(action=="接收")
    cout << "【" << action << "】标志位 = " << SIGN << " 序列号 = " << int(head.Seq) << " 校验和 = " << int(head.CheckSum) << endl;
    if (action == "发送")
    cout << "【" << action << "】标志位 = " << SIGN << " 序列号 = " << int(head.Seq) << endl;
}

//计算校验和
u_short CalculateChecksum(u_short* head, int size)
{
    int count = (size + 1) / 2;//计算循环次数，每次循环计算两个16位的数据
    u_short* buf = (u_short*)malloc(size + 1);//动态分配字符串变量
    memset(buf, 0, size + 1);//数组清空
    memcpy(buf, head, size);
    u_long checkSum = 0;
    while (count--) {
        checkSum += *buf++;
        if (checkSum & 0xffff0000) {//如果相加结果的高十六位大于一，将十六位置零，并将最低位加一
            checkSum &= 0xffff;
            checkSum++;
        }
    }
    return ~(checkSum & 0xffff);//对最后的结果取反
}

//初始化新的包
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
    head.Seq = (unsigned char)seq; //序列号与当前序列号相同
    head.CheckSum = 0; 
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));//重新计算校验和
    memcpy(buf, &head, sizeof(head)); //拷贝到数组
}
void Threeway_Handshake(SOCKET& socket, SOCKADDR_IN& addr)
{
    Handssuc = 0;
    int handscount1 = 0;//记录超时重传次数
    int addrlength = sizeof(addr); 
    Head head; 
    char* buff = new char[sizeof(head)]; 
    //第一次握手信息
    if (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &addrlength) == SOCKET_ERROR)
    {//第一次握手接收失败
        cout << "【第一次握手失败】" << endl;
        return;
    }
    memcpy(&head, buff, sizeof(head)); //拷贝到头部
    if (head.Sign == SYN && CalculateChecksum((u_short*)&head, sizeof(head)) == 0)//校验和和标识符都正确
    {
        cout << "第一次握手成功【SYN】" << endl;
    }
    u_long mode = 1;
    ioctlsocket(socket, FIONBIO, &mode); //设置非阻塞模式
    //第二次握手信息
    head.Sign = ACK; //设置ACK=1
    head.CheckSum = 0;
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
    memcpy(buff, &head, sizeof(head));
    if (sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, addrlength) == SOCKET_ERROR)
    {
        cout << "【第二次握手失败】" << endl;
        return;
    }
    cout << "第二次握手成功【SYN ACK】" << endl;
    clock_t starttime = clock();
    u_long imode = 1;

    ioctlsocket(socket, FIONBIO, &imode);//非阻塞模式
    //第三次握手
    while (clock() - starttime > retime)
    {
        if (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &addrlength))
        {
            cout << "【第三次握手失败】" << endl;
            break;
        }
        handscount1++;
        memcpy(buff, &head, sizeof(head));
        starttime = clock();
        if (handscount1 == Hands_shakecount) {
            cout << "【等待超时】" << endl;
            return;
        }
    }
    cout << "第三次握手成功【ACK】" << endl;
    Handssuc = 1;
    cout << "【已连接发送端】" << endl;
    cout << "---------------------------------------------------" << endl;
}
//接收文件
int Receivefile(SOCKET& socket, SOCKADDR_IN& addr, char* data)
{
    int addrlength = sizeof(addr);
    long int sum = 0;
    Head head;
    char* buf = new char[maxsize + sizeof(head)]; //缓冲数组长度是数据+头部的最大大小
    int seq = 0; 
    while (1)
    {
        int recvlength = recvfrom(socket, buf, sizeof(head) + maxsize, 0, (sockaddr*)&addr, &addrlength);//接收报文长度
       
        if (recvlength == SOCKET_ERROR) {
            // 非阻塞模式下，接收失败时会返回SOCKET_ERROR，并且WSAGetLastError()返回WSAWOULDBLOCK
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // 如果是WSAWOULDBLOCK错误，表示没有数据可以接收，继续循环等待
                continue;
            }
            else {
                cout << "【接收数据包失败！】 错误码：" << error << endl;
                return -1;
            }
        }

        memcpy(&head, buf, sizeof(head)); // 解析数据包头部
        cout << "【接收到数据包】" << " 长度 = " << recvlength << " 字节" << endl;
        if (head.Sign == END && CalculateChecksum((u_short*)&head, sizeof(head)) == 0)//END标志位，校验和为0，结束
        {
            cout << "【传输成功】" << endl;
            break; //结束跳出while循环
        }

        if (head.Sign == static_cast<unsigned char>(0) && CalculateChecksum((u_short*)buf, recvlength - sizeof(head)))//校验和不为0且flag是无符号字符
        {
            //判断收到的数据包是否正确
            if (seq != int(head.Seq))
            {//seq不相等，数据包接收有误
                Newpacketcheck(head, ACK, buf, seq);
                //重新发送ACK
                string SIGN;
                switch (head.Sign) {
                case 1: SIGN = "【SYN】"; break;
                case 2: SIGN = "【ACK】"; break;
                case 3: SIGN = "【SYN ACK】"; break;
                case 4: SIGN = "【FIN】"; break;
                case 5: SIGN = "【FIN SYN】"; break;
                case 6: SIGN = "【FIN ACK】"; break;
                case 7: SIGN = "【END】"; break;
                default: SIGN = "【SEND】"; break;
                }
                // 重新发送ACK
                PrintPacketLog(head, "重新发送");
                sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength);
             
                continue; //丢包
            }
            seq = int(head.Seq);
            seq %= 256;//取模
            
            // 处理正确的数据包
            PrintPacketLog(head, "接收");
            char* buf_data = new char[recvlength - sizeof(head)]; //数组的大小是接收到的报文长度减去头部大小
            memcpy(buf_data, buf + sizeof(head), recvlength - sizeof(head)); //从头部后面开始拷贝，把数据拷贝到缓冲数组
            memcpy(data + sum, buf_data, recvlength - sizeof(head));
            sum = sum + int(head.DataLen);

            //初始化首部
            Newpacketcheck(head, ACK, buf, seq);
            //发送ACK
            sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength);
            PrintPacketLog(head, "发送");
            seq++;//序列号加
            seq %= 256; //超过255要取模
        }
    }
    //发送END信息，结束
    Newpacket(head, END, buf);
    if (sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength) == SOCKET_ERROR)
    {
        cout << "【发送错误】" << endl;
        return -1; //发送错误
    }
    return sum; //返回接收到的数据包字节总数
}

//关闭连接 三次挥手
void Fourway_Wavehand(SOCKET& socket, SOCKADDR_IN& addr)
{
    int addrlength = sizeof(addr);
    Head head;
    char* buf = new char[sizeof(head)];

    //第一次挥手
    while (1)
    {
        recvfrom(socket, buf, sizeof(head) + maxsize, 0, (sockaddr*)&addr, &addrlength);//接收报文长度
        memcpy(&head, buf, sizeof(head));
        if (head.Sign == FIN && CalculateChecksum((u_short*)&head, sizeof(head)) == 0)
        {
            cout << "第一次挥手成功【FIN ACK】" << endl;
            break;
        }
    }

    //第二次挥手
    head.Sign = ACK;
    head.CheckSum = 0;
    head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
    memcpy(buf, &head, sizeof(head));
    if (sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength) == -1)
    {
        cout << "【第二、三次挥手失败】" << endl;
        return;
    }
    cout << "第二、三次挥手成功【FIN ACK】" << endl;
    clock_t starttime = clock();//记录第二次挥手发送时间

    //第三次挥手
    while (recvfrom(socket, buf, sizeof(head), 0, (sockaddr*)&addr, &addrlength) <= 0)
    {
        if (clock() - starttime > retime)
        {
            memcpy(buf, &head, sizeof(head));
            if (sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength) == -1)
            {
                cout << "【第四次挥手失败】" << endl;
                return;
            }
        }
    }
    Head head1; //不修改head结构，使用临时头部head1计算
    memcpy(&head1, buf, sizeof(head));
    if (head1.Sign == ACK_FIN && CalculateChecksum((u_short*)&head1, sizeof(head1) == 0))
    {
        cout << "第四次挥手成功【ACK】" << endl;
    }
    else
    {
        cout << "【连接关闭失败！传输终止】" << endl;
        return;
    }
    cout << "【结束连接】" << endl;
}

int main() {
    cout << "---------------------------------------------------" << endl;
    cout << "||             -->Receiving_End                  ||" << endl;
    cout << "---------------------------------------------------" << endl;
    SOCKET c_client;
    sockaddr_in client_addr;
    // 创建服务器套接字
    if (!initwsa()) {
        return 0;//初始化失败则退出程序
    }
    char ip[100];
    u_short port;
    cout << "请输入本机的IP地址：";
    cin >> ip;
    cout << "请输入端口号：";
    cin >> port;
    cout << "【等待发送端连接...】" << endl;
    // 配置接收端地址
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);//端口号
    client_addr.sin_addr.s_addr = inet_addr(ip);//IP地址
    c_client = socket(AF_INET, SOCK_DGRAM, 0);//使用数据报套接字，适合用于UDP协议。

    //绑定套接字
    bind(c_client, (sockaddr*)&client_addr, sizeof(client_addr));

    while (1)
    {
        int len = sizeof(client_addr);
        //建立连接
        Threeway_Handshake(c_client, client_addr);
        if (Handssuc == 0) {
            cout << "【重新连接】" << endl;
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

        //关闭连接
        Fourway_Wavehand(c_client, client_addr);
        ofstream out(file.data(), ofstream::binary); //输出流采用二进制的方法
        if (out.is_open()) {
            out.write(data, datalength); // 将数据以二进制方式写入文件
            out.close();
        }
        else {
            // 文件打开失败的处理
            cout << "【文件打开出错】" << endl;
            return 0;
        }
        out.close();
        cout << "【文件传输成功】" << endl;
        cout << "---------------------------------------------------" << endl;
    }
    closesocket(c_client); //关闭套接字
    WSACleanup();
    return 0;

}
