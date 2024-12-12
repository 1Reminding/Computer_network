#include <winsock.h>
#include <iostream>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

int MAXLEN = 2048;//��������󳤶�

//�������ݰ���ֵ�Ƿ�Ϊ1��������Ӧ���統ACKΪ1ʱ����ΪACK��ACK��SYN��Ϊ1����ΪACK_SYN
unsigned char SYN = 0x1; 
unsigned char ACK = 0x2;
unsigned char ACK_SYN = 0x3;
unsigned char FIN = 0x4;
unsigned char FIN_SYN = 0x5;
unsigned char ACK_FIN = 0x6;
//������־
unsigned char END = 0x7;

//������ֺ��ش���������
int Handshakecount = 10;
int Resentcount = 10;

//��ʼ���ɹ�����Ϊ0
int Handshakesuc = 0;
int Resentsuc = 0;

//�ͻ��˵ȴ���������Ӧ���ʱ��
double RETIME = 3500;

//��ʼ��socket,��ʼ���ɹ�ִ�У���ʼ��ʧ���˳�
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
	u_short DataLen;//���ݳ��� 16λ
	unsigned char Sign;//����λ��ʾ���ݰ�����,8λ
	unsigned char Seq;//���кţ�������֤�Ƿ������������кţ�����������,8λ
	//��ʼ��
	Head(){
		CheckSum = 0;
		DataLen = 0;
		Sign = 0;
		Seq = 0;
	}
};
// ��ӡ������־
void PrintSendLog(const Head& head, const string& action)
{
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
	if(action=="������־")
	cout << "��" << action << "����־λ=" << SIGN << " ���к� = " << int(head.Seq) << " У��� = " << int(head.CheckSum) << endl;
	if(action=="������־")
	cout << "��" << action << "����־λ=" << SIGN << " ���к� = " << int(head.Seq)<< endl;
}
//����У���
u_short CalculateChecksum(u_short* head, int size)
{
	// ����ѭ��������ÿ�δ�������16λ����
	int count = (size + 1) / 2;

	// ��̬���䲢��ʼ��������
	u_short* buf = (u_short*)malloc(size + 1);
	memset(buf, 0, size + 1);
	memcpy(buf, head, size);

	// �ۼ�У���
	u_long checkSum = 0;
	while (count--) {
		checkSum += *buf++; // ��16λ�����ۼ�
		if (checkSum & 0xffff0000) { // �����16λ�н�λ�������λ
			checkSum &= 0xffff;
			checkSum++;
		}
	}

	// �������յ�ȡ��У���
	return ~(checkSum & 0xffff);
}

//��ʼ���µİ�
void NewPacket(Head& head, const unsigned char str, char* buf)
{
	head.Sign = str;
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
	memcpy(buf, &head, sizeof(head));
}

void Threeway_Handshake(SOCKET& socket, SOCKADDR_IN& addr)//�������ֽ�������
{
	Handshakesuc = 0;
	int length = sizeof(addr); 
	//��һ������
	Head head = Head(); //�����ײ�
	head.Sign = SYN; //��־��ΪSYN
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head)); //����У���
	char* buff = new char[sizeof(head)]; //��������
	memcpy(buff, &head, sizeof(head));//���ײ����뻺������
	if (sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, length) == SOCKET_ERROR)
	{//����ʧ��
		cout << "����һ������ʧ�ܡ�" << endl;
		return;
	}
	cout << "��һ�����ֳɹ���SYN��" << endl;
	clock_t handstime = clock(); //��¼���͵�һ������ʱ��
	u_long mode = 1;
	ioctlsocket(socket, FIONBIO, &mode); //���÷�����ģʽ
	int handscount1 = 0;//��¼��ʱ�ش�����
	//�ڶ�������
	while (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &length) <= 0)
	{//�ȴ�����

		if (clock() - handstime > RETIME)//��ʱ�ش�
		{
			memcpy(buff, &head, sizeof(head));//���ײ����뻺����
			sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, length); //�ٴη���
			handstime = clock(); //��ʱ
			cout << "�����ӳ�ʱ���ȴ��ش�������" << endl;
			handscount1++;
			if (handscount1 == Handshakecount) {
				cout << "���ȴ���ʱ��" << endl;
				return;
			}
		}
	}
	memcpy(&head, buff, sizeof(head)); //ACK��ȷ�Ҽ��У�������
	if (head.Sign == ACK && CalculateChecksum((u_short*)&head, sizeof(head) == 0))
	{
		cout << "�ڶ������ֳɹ���SYN ACK��" << endl;
		handscount1 = 0;
	}
	else
	{
		cout << "���ڶ�������ʧ�ܡ�" << endl;
		return;
	}
	//����������
	head.Sign = ACK_SYN; //ACK=1 SYN=1
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head)); //У���
	sendto(socket, (char*)&head, sizeof(head), 0, (sockaddr*)&addr, length); 
	bool win = 0; //�Ƿ����ӳɹ�
	while (clock() - handstime <= RETIME)
	{//�ȴ���Ӧ
		if (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &length))
		{//�յ�����
			win = 1;
			break;
		}
		//ѡ���ط�
		memcpy(buff, &head, sizeof(head));
		sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, length);
		handstime = clock();
		handscount1++;
		if (handscount1 == Handshakecount) {
			cout << "���ȴ���ʱ��" << endl;
			return;
		}
	}
	if (!win)
	{
		cout << "������������ʧ�ܡ�" << endl;
		return;
	}
	cout << "���������ֳɹ���ACK��" << endl;
	Handshakesuc = 1;
	cout << "�����ӳɹ���" << endl;
	cout << "---------------------------------------------------" << endl;

}

void BuildAndSendPacket(SOCKET& socket, SOCKADDR_IN& addr, Head& head, char* data, int length) {
    char* buf = new char[MAXLEN + sizeof(head)];
    memcpy(buf, &head, sizeof(head));          // ����ͷ��
    memcpy(buf + sizeof(head), data, length);  // ��������
    head.CheckSum = CalculateChecksum((u_short*)buf, sizeof(head) + length);  // У���
    memcpy(buf, &head, sizeof(head));          // ����ͷ��
    sendto(socket, buf, sizeof(head) + length, 0, (sockaddr*)&addr, sizeof(addr));
    delete[] buf;                             // �ͷ��ڴ�
}


//���ݶΰ�����
void Sendpacket(SOCKET& socket, SOCKADDR_IN& addr, char* data, int length, int& seq)
{
	//ͷ����ʼ����У��ͼ���
	Resentsuc = 0;
	int addrlength = sizeof(addr);
	Head head;
	char* buf = new char[MAXLEN + sizeof(head)];
	head.DataLen = length; //ʹ�ô����data�ĳ��ȶ���ͷ��datasize
	//head.Seq = unsigned char(seq);
	head.Seq = static_cast<unsigned char>(seq);
	memcpy(buf, &head, sizeof(head)); //�����ײ�������
	memcpy(buf + sizeof(head), data, sizeof(head) + length); //����data��������������
	head.CheckSum = CalculateChecksum((u_short*)buf, sizeof(head) + length);//�������ݲ��ֵ�У���
	memcpy(buf, &head, sizeof(head)); //���º��ͷ���ٴο�������������

	// �������С
	int packet_size = length + sizeof(head);
	cout << "�����Ͱ���С��" << packet_size << " �ֽ�" << endl;

	//����
	sendto(socket, buf, length + sizeof(head), 0, (sockaddr*)&addr, addrlength);//����
	PrintSendLog(head,"������־"); // ��ӡ��־
	
	clock_t starttime = clock();//��¼����ʱ��
	int sendcount1 = 0;
	//����ʱ�ش�
	while (1)
	{
		sendcount1 = 0;
		u_long mode = 1;
		ioctlsocket(socket, FIONBIO, &mode); //���÷�����ģʽ
		//�ȴ�������Ϣ
		while (recvfrom(socket, buf, MAXLEN, 0, (sockaddr*)&addr, &addrlength) <= 0)
		{
			if (clock() - starttime > RETIME) //��ʱ�ش�
			{
				head.DataLen = length;
				head.Seq = u_char(seq);
				head.Sign = u_char(0x0); //��շ���ջ
				memcpy(buf, &head, sizeof(head)); //�����ײ�������
				memcpy(buf + sizeof(head), data, sizeof(head) + length); //����data��������������
				head.CheckSum = CalculateChecksum((u_short*)buf, sizeof(head) + length);//�������ݲ��ֵ�У���
				memcpy(buf, &head, sizeof(head)); //���º��ͷ���ٴο�������������
				string Newsign;
				switch (head.Sign) {
				case 1: Newsign = "��SYN��"; break;
				case 2: Newsign = "��ACK��"; break;
				case 3: Newsign = "��SYN ACK��"; break;
				case 4: Newsign = "��FIN��"; break;
				case 5: Newsign = "��FIN SYN��"; break;
				case 6: Newsign = "��FIN ACK��"; break;
				case 7: Newsign = "��END��"; break;
				default: Newsign = "��RESEND��"; break;
				}

				cout << "����ʱ�ش��������͡���־λ = " << Newsign << " ���к� = " << int(head.Seq) << endl;
				sendcount1++;
				sendto(socket, buf, length + sizeof(head), 0, (sockaddr*)&addr, addrlength);//���·���
				starttime = clock();//��¼��ǰ����ʱ��
				if (sendcount1 == Resentcount) {
					return;
				}
			}
		}
		memcpy(&head, buf, sizeof(head));//���������յ���Ϣ����ȡ
		//�������кź�ACK����ȷ
		u_short checknum = CalculateChecksum((u_short*)&head, sizeof(head));
		if (head.Seq == u_short(seq) && head.Sign == ACK)
		{
			PrintSendLog(head,"������־"); // ��ӡ��־
			Resentsuc = 1;
			break;
		}
	}

}
//�ļ�����
void Sendfile(SOCKET& socket, SOCKADDR_IN& addr, char* data, int data_len)
{
	int addrlength = sizeof(addr);
	int bagsum = data_len / MAXLEN; //���ݰ�����
	if (data_len % MAXLEN) {
		bagsum++; 
	}
	int seq = 0; 
	clock_t starttime = clock();

	// ���ļ�����д�루��׷��ģʽ�򿪣�
	std::ofstream logfile("transfer_log.csv", std::ios::app);
	if (!logfile) {
		std::cerr << "�޷�����־�ļ���" << std::endl;
		return;
	}

	// ����ǵ�һ��д�루�ļ�Ϊ�գ���д���ͷ
	if (logfile.tellp() == 0) {
		logfile << "���������� (MB), ����ʱ�� (��), ������ (MB/s)\n";
	}

	for (int i = 0; i < bagsum; i++)
	{
		int len;
		if (i == bagsum - 1)
		{//���һ�����ݰ�������ȡ���Ľ����������ݳ�����ʣ������
			len = data_len - (bagsum - 1) * MAXLEN;
		}
		else
		{//�����һ�����ݳ��Ⱦ�Ϊmaxlength
			len = MAXLEN;
		}
		// ��¼ÿ�����Ŀ�ʼʱ��
		clock_t packet_start_time = clock();

		Sendpacket(socket, addr, data + i * MAXLEN, len, seq);
		
		// �����ǰ���Ĵ�С
		int packet_size = len + sizeof(Head);
		cout << "���ļ����䡿����С = " << packet_size << " �ֽ�" << endl;

		if (Resentsuc == 0) {
			cout << "���ش�ʧ�ܡ�" << endl;
			return;
		}
		seq++;
		seq = seq % 256; //���к������ݰ���ռ8λ����0-255��������ģ256ȥ��
	}
	// ���㴫��ʱ��
	double duration = double(clock() - starttime) / CLOCKS_PER_SEC; // ����

	// ����������
	double throughput = (double)data_len / duration / 1024 / 1024; // ������ (MB/s)

	// ������д����־�ļ�
	logfile << data_len / 1024.0 / 1024.0 << ", " << duration << ", " << throughput << "\n";
	logfile.flush();  // ȷ����������д���ļ�
	// ������
	std::cout << "������ɹ���" << std::endl;
	std::cout << "��������: " << data_len / 1024 / 1024 << " MB   " << "����ʱ��: " << duration << " ��   " << "������: " << throughput << " MB/s" << std::endl;
	//���ͽ�����Ϣ
	Head head;
	char* buf = new char[sizeof(head)]; //��������
	NewPacket(head, END, buf); //���ú�������ACK=SYN=FIN=1�����ݰ�����ʾ����
	sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength);

	clock_t starttime_end = clock(); // ��ʱ
	while (1)
	{
		u_long mode = 1;
		ioctlsocket(socket, FIONBIO, &mode);//����Ϊ������ģʽ
		while (recvfrom(socket, buf, MAXLEN, 0, (sockaddr*)&addr, &addrlength) <= 0)
		{//�ȴ�����
			if (clock() - starttime > RETIME)
			{//���������õ��ش�ʱ�����ƣ����´������ݰ�
				char* buf = new char[sizeof(head)]; //��������
				NewPacket(head, END, buf); //���ú�������ACK=SYN=FIN=1�����ݰ�����ʾ����
				cout << "����ʱ�ȴ��ش���" << endl;
				sendto(socket, buf, sizeof(head), 0, (sockaddr*)&addr, addrlength); //����������ͬ�����ݰ�
				starttime = clock(); //��һ�ּ�ʱ
			}
		}
		memcpy(&head, buf, sizeof(head));
		if (head.Sign == END)
		{
			cout << "�ļ�����������ȴ������ź�..." << endl;
			break;
		}
	}
	u_long mode = 0;
	ioctlsocket(socket, FIONBIO, &mode);//�Ļ�����ģʽ
	// �ر���־�ļ�
	logfile.close();
}

//�ر����� �Ĵλ��ֺϲ�Ϊ���λ���
//��ΪUDP�ǵ���ģ�ͬʱ�ڶ��κ͵����ο��Ժϲ��������α�ʾ�Ѿ����ճɹ�FIN������ACKȷ�ϣ�
void Fourway_Wavehand(SOCKET& socket, SOCKADDR_IN& addr)
{
	int addrlength = sizeof(addr);
	Head head;
	char* buff = new char[sizeof(head)];
	//��һ�λ���
	head.Sign = FIN;
	//head.checksum = 0;//У�����0
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));
	memcpy(buff, &head, sizeof(head));
	if (sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, addrlength) == SOCKET_ERROR)
	{
		cout << "����һ�λ���ʧ�ܡ�" << endl;
		return;
	}
	cout << "��һ�λ��֡�FIN ACK��" << endl;
	clock_t byetime = clock(); //��¼���͵�һ�λ���ʱ��

	u_long mode = 1;
	ioctlsocket(socket, FIONBIO, &mode);

	//�ڶ��λ���
	while (recvfrom(socket, buff, sizeof(head), 0, (sockaddr*)&addr, &addrlength) <= 0)
	{//�ȴ�����
		if (clock() - byetime > RETIME)//��ʱ�ش�
		{
			memcpy(buff, &head, sizeof(head));//���ײ����뻺����
			sendto(socket, buff, sizeof(head), 0, (sockaddr*)&addr, addrlength);
			byetime = clock();
		}
	}
	//����У��ͼ���
	memcpy(&head, buff, sizeof(head));
	if (head.Sign == ACK && CalculateChecksum((u_short*)&head, sizeof(head) == 0))
	{
		cout << "�ڶ������λ��֡�FIN ACK��" << endl;
	}
	else
	{
		cout << "���ڶ������λ���ʧ�ܡ�" << endl;
		return;
	}

	//���Ĵλ���
	head.Sign = ACK_FIN;
	head.CheckSum = CalculateChecksum((u_short*)&head, sizeof(head));//����У���
	memcpy(buff, &head, sizeof(head));
	if (sendto(socket, (char*)&head, sizeof(head), 0, (sockaddr*)&addr, addrlength) == -1)
	{
		cout << "�����Ĵλ���ʧ�ܡ�" << endl;
		return;
	}
	cout << "���Ĵλ��֡�ACK��" << endl;
	cout << "���������ӡ�" << endl;
	cout << "---------------------------------------------------" << endl;
}



int main() {
	SOCKET s_server;
	SOCKADDR_IN server_addr;//����˵�ַ 
	cout << "---------------------------------------------------" << endl;
	cout << "||                    Sender-->                  ||" << endl;
	cout << "---------------------------------------------------" << endl;
	if (!initwsa()) {
		return 0;//��ʼ��ʧ�����˳�����
	}
	char ip[100];
	u_short port;
	cout << "�����뱾��IP��ַ��";
	cin >> ip;
	cout << "������˿ںţ�";
	cin >> port;
	server_addr.sin_family = AF_INET;//AF_INET��ʾ IPv4 ��ַ�壬AF_INET6��ʾ IPv6 ��ַ��
	server_addr.sin_addr.S_un.S_addr = inet_addr(ip);
	server_addr.sin_port = htons(port);// htons()�����ͱ����������ֽ�˳��ת��������ֽ�˳������˿ں�
	s_server = socket(AF_INET, SOCK_DGRAM, 0);//����һ������ IPv4 �����ݱ��׽��֣������� UDP Э��

	while (1)
	{
		//�������ֽ�������
		Threeway_Handshake(s_server, server_addr);
		if (Handshakesuc == 0) {
			cout << "������ʧ��...��������...��" << endl;
			continue;
		}
		//�����ļ�
		string filename;
		cout << "������Ҫ������ļ����ƣ�" << endl;
		cin >> filename;

		ifstream in(filename, ifstream::in | ios::binary);//�Զ����Ʒ�ʽ���ļ�
		if (!in) {
			cout << "�����󣡼���ļ��Ƿ���ڣ���" << endl;
			return false;
		}
		//�����ļ���
		Sendfile(s_server, server_addr, (char*)(filename.data()), filename.length());
		if (Resentsuc == 0) {
			return 0;
		}
		clock_t starttime = clock();
		int bytesRead;
		char* buff = new char[10000000];
		if (in.is_open()) {
			in.read(buff, 10000000);
			bytesRead = in.gcount(); // ��ȡʵ�ʶ�ȡ���ֽ���
			// ���� bytesRead �ֽڵ�����
		}
		else {
			// �ļ���ʧ�ܵĴ���
			cout << "���ļ���ȡ����" << endl;
			return 0;
		}
		//�����ļ�
		Sendfile(s_server, server_addr, buff, bytesRead);
		clock_t end = clock();

		//��������
		cout << "��FileName��=" << filename << endl;
		cout << "��FileSize�� =" << ((double)bytesRead) / 1024 << "KB" << endl;
		cout << "��TransTime��=" << end - starttime << "ms" << endl;
		cout << "��Throuput�� =" << ((double)bytesRead) / (double(end - starttime)) << "byte/ms" << endl;

		//�Ĵλ��ֶϿ�����
		Fourway_Wavehand(s_server, server_addr);
	}


	return 0;
	WSACleanup();
}

