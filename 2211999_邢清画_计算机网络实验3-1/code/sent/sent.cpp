#include <winsock.h>
#include <iostream>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

int MAXLEN = 2048;//缓冲区最大长度

//根据数据包的值是否为1与命名对应，如当ACK为1时命名为ACK，ACK和SYN均为1命名为ACK_SYN
unsigned char SYN = 0x1; 
unsigned char ACK = 0x2;
unsigned char ACK_SYN = 0x3;
unsigned char FIN = 0x4;
unsigned char FIN_SYN = 0x5;
unsigned char ACK_FIN = 0x6;
//结束标志
unsigned char END = 0x7;

//最大握手和重传次数限制
int Handshakecount = 10;
int Resentcount = 10;

//初始化成功次数为0
int Handshakesuc = 0;
int Resentsuc = 0;

//客户端等待服务器响应的最长时间
double RETIME = 3500;

//初始化socket,初始化成功执行，初始化失败退出
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
	u_short DataLen;//数据长度 16位
	unsigned char Sign;//后三位表示数据包类型,8位
	unsigned char Seq;//序列号（用于验证是否出错及出错的序列号，便于修正）,8位
	//初始化
	Head(){
		CheckSum = 0;
		DataLen = 0;
		Sign = 0;
		Seq = 0;
	}
};
// 打印发送日志
void PrintSendLog(const Head& head, const string& action)
{
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
	if(action=="发送日志")
	cout << "【" << action << "】标志位=" << SIGN << " 序列号 = " << int(head.Seq) << " 校验和 = " << int(head.CheckSum) << endl;
	if(action=="接收日志")
	cout << "【" << action << "】标志位=" << SIGN << " 序列号 = " << int(head.Seq)<< endl;
}
//计算校验和
u_short CalculateChecksum(u_short* head, int size)
{
	// 计算循环次数，每次处理两个16位数据
	int count = (size + 1) / 2;

	// 动态分配并初始化缓冲区
	u_short* buf = (u_short*)malloc(size + 1);
	memset(buf, 0, size + 1);
	memcpy(buf, head, size);

	// 累加校验和
	u_long checkSum = 0;
	while (count--) {
		checkSum += *buf++; // 将16位数据累加
		if (checkSum & 0xffff0000) { // 如果高16位有进位，处理进位
			checkSum &= 0xffff;
			checkSum++;
		}
	}

	// 返回最终的取反校验和
	return ~(checkSum & 0xffff);
}

//初始化新的包
void NewPacket(Head& head, const unsigned char str, char* buf)
{
	head.Sign = str;
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
	memcpy(buf, &head, sizeof(head));
}

void Threeway_Handshake(SOCKET& socket, SOCKADDR_IN& addr)//三次握手建立连接
{
	Handshakesuc = 0;
	int length = sizeof(addr); 
	//第一次握手
	Head head = Head(); //数据首部
	head.Sign = SYN; //标志设为SYN
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head)); //计算校验和
	char* buff = new char[sizeof(head)]; //缓冲数组
	memcpy(buff, &head, sizeof(head));//将首部放入缓冲数组
	if (sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, length) == SOCKET_ERROR)
	{//发送失败
		cout << "【第一次握手失败】" << endl;
		return;
	}
	cout << "第一次握手成功【SYN】" << endl;
	clock_t handstime = clock(); //记录发送第一次握手时间
	u_long mode = 1;
	ioctlsocket(socket, FIONBIO, &mode); //设置非阻塞模式
	int handscount1 = 0;//记录超时重传次数
	//第二次握手
	while (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &length) <= 0)
	{//等待接收

		if (clock() - handstime > RETIME)//超时重传
		{
			memcpy(buff, &head, sizeof(head));//将首部放入缓冲区
			sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, length); //再次发送
			handstime = clock(); //计时
			cout << "【连接超时！等待重传……】" << endl;
			handscount1++;
			if (handscount1 == Handshakecount) {
				cout << "【等待超时】" << endl;
				return;
			}
		}
	}
	memcpy(&head, buff, sizeof(head)); //ACK正确且检查校验和无误
	if (head.Sign == ACK && CalculateChecksum((u_short*)&head, sizeof(head) == 0))
	{
		cout << "第二次握手成功【SYN ACK】" << endl;
		handscount1 = 0;
	}
	else
	{
		cout << "【第二次握手失败】" << endl;
		return;
	}
	//第三次握手
	head.Sign = ACK_SYN; //ACK=1 SYN=1
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head)); //校验和
	sendto(socket, (char*)&head, sizeof(head), 0, (sockaddr*)&addr, length); 
	bool win = 0; //是否连接成功
	while (clock() - handstime <= RETIME)
	{//等待回应
		if (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &length))
		{//收到报文
			win = 1;
			break;
		}
		//选择重发
		memcpy(buff, &head, sizeof(head));
		sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, length);
		handstime = clock();
		handscount1++;
		if (handscount1 == Handshakecount) {
			cout << "【等待超时】" << endl;
			return;
		}
	}
	if (!win)
	{
		cout << "【第三次握手失败】" << endl;
		return;
	}
	cout << "第三次握手成功【ACK】" << endl;
	Handshakesuc = 1;
	cout << "【连接成功】" << endl;
	cout << "---------------------------------------------------" << endl;

}

void BuildAndSendPacket(SOCKET& socket, SOCKADDR_IN& addr, Head& head, char* data, int length) {
    char* buf = new char[MAXLEN + sizeof(head)];
    memcpy(buf, &head, sizeof(head));          // 拷贝头部
    memcpy(buf + sizeof(head), data, length);  // 拷贝数据
    head.CheckSum = CalculateChecksum((u_short*)buf, sizeof(head) + length);  // 校验和
    memcpy(buf, &head, sizeof(head));          // 更新头部
    sendto(socket, buf, sizeof(head) + length, 0, (sockaddr*)&addr, sizeof(addr));
    delete[] buf;                             // 释放内存
}


//数据段包传输
void Sendpacket(SOCKET& socket, SOCKADDR_IN& addr, char* data, int length, int& seq)
{
	//头部初始化及校验和计算
	Resentsuc = 0;
	int addrlength = sizeof(addr);
	Head head;
	char* buf = new char[MAXLEN + sizeof(head)];
	head.DataLen = length; //使用传入的data的长度定义头部datasize
	//head.Seq = unsigned char(seq);
	head.Seq = static_cast<unsigned char>(seq);
	memcpy(buf, &head, sizeof(head)); //拷贝首部的数据
	memcpy(buf + sizeof(head), data, sizeof(head) + length); //数据data拷贝到缓冲数组
	head.CheckSum = CalculateChecksum((u_short*)buf, sizeof(head) + length);//计算数据部分的校验和
	memcpy(buf, &head, sizeof(head)); //更新后的头部再次拷贝到缓冲数组

	// 输出包大小
	int packet_size = length + sizeof(head);
	cout << "【发送包大小】" << packet_size << " 字节" << endl;

	//发送
	sendto(socket, buf, length + sizeof(head), 0, (sockaddr*)&addr, addrlength);//发送
	PrintSendLog(head,"发送日志"); // 打印日志
	
	clock_t starttime = clock();//记录发送时间
	int sendcount1 = 0;
	//处理超时重传
	while (1)
	{
		sendcount1 = 0;
		u_long mode = 1;
		ioctlsocket(socket, FIONBIO, &mode); //设置非阻塞模式
		//等待接收消息
		while (recvfrom(socket, buf, MAXLEN, 0, (sockaddr*)&addr, &addrlength) <= 0)
		{
			if (clock() - starttime > RETIME) //超时重传
			{
				head.DataLen = length;
				head.Seq = u_char(seq);
				head.Sign = u_char(0x0); //清空发送栈
				memcpy(buf, &head, sizeof(head)); //拷贝首部的数据
				memcpy(buf + sizeof(head), data, sizeof(head) + length); //数据data拷贝到缓冲数组
				head.CheckSum = CalculateChecksum((u_short*)buf, sizeof(head) + length);//计算数据部分的校验和
				memcpy(buf, &head, sizeof(head)); //更新后的头部再次拷贝到缓冲数组
				string Newsign;
				switch (head.Sign) {
				case 1: Newsign = "【SYN】"; break;
				case 2: Newsign = "【ACK】"; break;
				case 3: Newsign = "【SYN ACK】"; break;
				case 4: Newsign = "【FIN】"; break;
				case 5: Newsign = "【FIN SYN】"; break;
				case 6: Newsign = "【FIN ACK】"; break;
				case 7: Newsign = "【END】"; break;
				default: Newsign = "【RESEND】"; break;
				}

				cout << "【超时重传】【发送】标志位 = " << Newsign << " 序列号 = " << int(head.Seq) << endl;
				sendcount1++;
				sendto(socket, buf, length + sizeof(head), 0, (sockaddr*)&addr, addrlength);//重新发送
				starttime = clock();//记录当前发送时间
				if (sendcount1 == Resentcount) {
					return;
				}
			}
		}
		memcpy(&head, buf, sizeof(head));//缓冲区接收到信息，读取
		//检验序列号和ACK均正确
		u_short checknum = CalculateChecksum((u_short*)&head, sizeof(head));
		if (head.Seq == u_short(seq) && head.Sign == ACK)
		{
			PrintSendLog(head,"接收日志"); // 打印日志
			Resentsuc = 1;
			break;
		}
	}

}
//文件传输
void Sendfile(SOCKET& socket, SOCKADDR_IN& addr, char* data, int data_len)
{
	int addrlength = sizeof(addr);
	int bagsum = data_len / MAXLEN; //数据包总数
	if (data_len % MAXLEN) {
		bagsum++; 
	}
	int seq = 0; 
	clock_t starttime = clock();

	// 打开文件进行写入（以追加模式打开）
	std::ofstream logfile("transfer_log.csv", std::ios::app);
	if (!logfile) {
		std::cerr << "无法打开日志文件！" << std::endl;
		return;
	}

	// 如果是第一次写入（文件为空），写入表头
	if (logfile.tellp() == 0) {
		logfile << "传输数据量 (MB), 传输时间 (秒), 吞吐率 (MB/s)\n";
	}

	for (int i = 0; i < bagsum; i++)
	{
		int len;
		if (i == bagsum - 1)
		{//最后一个数据包是向上取整的结果，因此数据长度是剩余所有
			len = data_len - (bagsum - 1) * MAXLEN;
		}
		else
		{//非最后一个数据长度均为maxlength
			len = MAXLEN;
		}
		// 记录每个包的开始时间
		clock_t packet_start_time = clock();

		Sendpacket(socket, addr, data + i * MAXLEN, len, seq);
		
		// 输出当前包的大小
		int packet_size = len + sizeof(Head);
		cout << "【文件传输】包大小 = " << packet_size << " 字节" << endl;

		if (Resentsuc == 0) {
			cout << "【重传失败】" << endl;
			return;
		}
		seq++;
		seq = seq % 256; //序列号在数据包中占8位，从0-255，超过则模256去除
	}
	// 计算传输时间
	double duration = double(clock() - starttime) / CLOCKS_PER_SEC; // 秒数

	// 计算吞吐率
	double throughput = (double)data_len / duration / 1024 / 1024; // 吞吐率 (MB/s)

	// 将数据写入日志文件
	logfile << data_len / 1024.0 / 1024.0 << ", " << duration << ", " << throughput << "\n";
	logfile.flush();  // 确保数据立即写入文件
	// 输出结果
	std::cout << "【传输成功】" << std::endl;
	std::cout << "总数据量: " << data_len / 1024 / 1024 << " MB   " << "传输时间: " << duration << " 秒   " << "吞吐率: " << throughput << " MB/s" << std::endl;
	//发送结束信息
	Head head;
	char* buf = new char[sizeof(head)]; //缓冲数组
	NewPacket(head, END, buf); //调用函数生成ACK=SYN=FIN=1的数据包，表示结束
	sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength);

	clock_t starttime_end = clock(); // 计时
	while (1)
	{
		u_long mode = 1;
		ioctlsocket(socket, FIONBIO, &mode);//设置为非阻塞模式
		while (recvfrom(socket, buf, MAXLEN, 0, (sockaddr*)&addr, &addrlength) <= 0)
		{//等待接收
			if (clock() - starttime > RETIME)
			{//超过了设置的重传时间限制，重新传输数据包
				char* buf = new char[sizeof(head)]; //缓冲数组
				NewPacket(head, END, buf); //调用函数生成ACK=SYN=FIN=1的数据包，表示结束
				cout << "【超时等待重传】" << endl;
				sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength); //继续发送相同的数据包
				starttime = clock(); //新一轮计时
			}
		}
		memcpy(&head, buf, sizeof(head));
		if (head.Sign == END)
		{
			cout << "文件传输结束，等待结束信号..." << endl;
			break;
		}
	}
	u_long mode = 0;
	ioctlsocket(socket, FIONBIO, &mode);//改回阻塞模式
	// 关闭日志文件
	logfile.close();
}

//关闭连接 四次挥手合并为三次挥手
//因为UDP是单向的，同时第二次和第三次可以合并，第三次表示已经接收成功FIN，发送ACK确认）
void Fourway_Wavehand(SOCKET& socket, SOCKADDR_IN& addr)
{
	int addrlength = sizeof(addr);
	Head head;
	char* buff = new char[sizeof(head)];
	//第一次挥手
	head.Sign = FIN;
	//head.checksum = 0;//校验和置0
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
	memcpy(buff, &head, sizeof(head));
	if (sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, addrlength) == SOCKET_ERROR)
	{
		cout << "【第一次挥手失败】" << endl;
		return;
	}
	cout << "第一次挥手【FIN ACK】" << endl;
	clock_t byetime = clock(); //记录发送第一次挥手时间

	u_long mode = 1;
	ioctlsocket(socket, FIONBIO, &mode);

	//第二次挥手
	while (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &addrlength) <= 0)
	{//等待接收
		if (clock() - byetime > RETIME)//超时重传
		{
			memcpy(buff, &head, sizeof(head));//将首部放入缓冲区
			sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, addrlength);
			byetime = clock();
		}
	}
	//进行校验和检验
	memcpy(&head, buff, sizeof(head));
	if (head.Sign == ACK && CalculateChecksum((u_short*)&head, sizeof(head) == 0))
	{
		cout << "第二、三次挥手【FIN ACK】" << endl;
	}
	else
	{
		cout << "【第二、三次挥手失败】" << endl;
		return;
	}

	//第四次挥手
	head.Sign = ACK_FIN;
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));//计算校验和
	memcpy(buff, &head, sizeof(head));
	if (sendto(socket, (char*)&head, sizeof(head), 0, (sockaddr*)&addr, addrlength) == -1)
	{
		cout << "【第四次挥手失败】" << endl;
		return;
	}
	cout << "第四次挥手【ACK】" << endl;
	cout << "【结束连接】" << endl;
	cout << "---------------------------------------------------" << endl;
}



int main() {
	SOCKET s_server;
	SOCKADDR_IN server_addr;//服务端地址 
	cout << "---------------------------------------------------" << endl;
	cout << "||                    Sender-->                  ||" << endl;
	cout << "---------------------------------------------------" << endl;
	if (!initwsa()) {
		return 0;//初始化失败则退出程序
	}
	char ip[100];
	u_short port;
	cout << "请输入本机IP地址：";
	cin >> ip;
	cout << "请输入端口号：";
	cin >> port;
	server_addr.sin_family = AF_INET;//AF_INET表示 IPv4 地址族，AF_INET6表示 IPv6 地址族
	server_addr.sin_addr.S_un.S_addr = inet_addr(ip);
	server_addr.sin_port = htons(port);// htons()将整型变量从主机字节顺序转变成网络字节顺序，填入端口号
	s_server = socket(AF_INET, SOCK_DGRAM, 0);//创建一个基于 IPv4 的数据报套接字，适用于 UDP 协议

	while (1)
	{
		//三次握手建立连接
		Threeway_Handshake(s_server, server_addr);
		if (Handshakesuc == 0) {
			cout << "【连接失败...即将重试...】" << endl;
			continue;
		}
		//传输文件
		string filename;
		cout << "请输入要传输的文件名称：" << endl;
		cin >> filename;

		ifstream in(filename, ifstream::in | ios::binary);//以二进制方式打开文件
		if (!in) {
			cout << "【错误！检查文件是否存在！】" << endl;
			return false;
		}
		//传输文件名
		Sendfile(s_server, server_addr, (char*)(filename.data()), filename.length());
		if (Resentsuc == 0) {
			return 0;
		}
		clock_t starttime = clock();
		int bytesRead;
		char* buff = new char[10000000];
		if (in.is_open()) {
			in.read(buff, 10000000);
			bytesRead = in.gcount(); // 获取实际读取的字节数
			// 处理 bytesRead 字节的数据
		}
		else {
			// 文件打开失败的处理
			cout << "【文件读取出错】" << endl;
			return 0;
		}
		//传输文件
		Sendfile(s_server, server_addr, buff, bytesRead);
		clock_t end = clock();

		//计算性能
		cout << "【FileName】=" << filename << endl;
		cout << "【FileSize】 =" << ((double)bytesRead) / 1024 << "KB" << endl;
		cout << "【TransTime】=" << end - starttime << "ms" << endl;
		cout << "【Throuput】 =" << ((double)bytesRead) / (double(end - starttime)) << "byte/ms" << endl;

		//四次挥手断开连接
		Fourway_Wavehand(s_server, server_addr);
	}


	return 0;
	WSACleanup();
}

