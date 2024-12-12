#include "UDP.h" // ���� UDP �Ķ���
#include <vector>  // ���� vector ��ͷ�ļ�
#pragma comment(lib, "ws2_32.lib") // ���� Windows Sockets ��

// ȫ�ֱ�������
//uint16_t SERVER_PORT;
const char* SERVER_IP = "127.0.0.1";  // Ĭ��ֵ

atomic<int> Header_Seq(0);//����������Ҫ������־λ

HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

atomic_int Sleep_Time(0);
float Last_Time = 0;

// �ļ�д�뻺����
class File_Writetobuf {
private:

    vector<char> buf;
    size_t position_now;
    ofstream file;

public:// ���캯������ʼ���ļ����ͻ�������С
    File_Writetobuf(const string& filename, size_t buffer_size)
        : buf(buffer_size), position_now(0) {// ��ʼ����������д��λ��
        file.open(filename, ios::binary);// ���ļ����Զ�����ģʽ
    }
    // д�����ݵ�������
    void write(const char* data, size_t length) {
        while (length > 0) {
            size_t space = buf.size() - position_now;// ���㵱ǰ������ʣ��ռ�
            size_t to_write = min(space, length);// ѡ��Ҫд����ֽ���
            // �����ݿ�����������
            memcpy(&buf[position_now], data, to_write);
            position_now += to_write; // ���µ�ǰд��λ��
            data += to_write;// �ƶ�Դ����ָ��
            length -= to_write;// ����ʣ��д���ֽ���

            if (position_now == buf.size()) {// ������������ˣ�ˢ�����ݵ��ļ�
                flush();
            }
        }
    }
    // ˢ�»������е����ݵ��ļ�
    void flush() {
        if (position_now > 0) {
            file.write(buf.data(), position_now);
            position_now = 0;// ���õ�ǰд��λ��
        }
    }
    // ����������ȷ���ڶ�������ʱд���������ݲ��ر��ļ�
    ~File_Writetobuf() {
        flush();// ˢ�»������е�����
        file.close();
    }
};

class UDPSERVER {
private:
    SOCKET Server_Socket;           // ���������׽���
    sockaddr_in Server_Address;     // ��������ַ
    sockaddr_in Router_Address;     // ·������ַ
    socklen_t Router_Address_Len;   //·�ɵ�ַ����
    uint32_t Seq;           // ��ǰ���к�
    uint32_t File_Len;      //�ļ�����
    int Message_Num;         //��Ϣ��

public:
    UDPSERVER() : Server_Socket(INVALID_SOCKET), Seq(0), Router_Address_Len(sizeof(Router_Address)), File_Len(0), Message_Num(0) {}

    bool INIT() {
        // ��ʼ�� Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[ERROR] WSAStartup ʧ�ܣ��������: " << result << endl;
            return false;
        }

        // ���汾�Ƿ�ƥ��
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[ERROR] WinSock �汾��֧�֣�" << endl;
            WSACleanup();
            return false;
        }

        // ���� UDP �׽���
        Server_Socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (Server_Socket == INVALID_SOCKET) {
            cerr << "[ERROR] �׽��ִ���ʧ�ܣ��������: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[ϵͳ] �׽��ִ����ɹ���" << endl;

        // ���÷�����ģʽ
        u_long mode = 1;
        if (ioctlsocket(Server_Socket, FIONBIO, &mode) != 0) {
            cerr << "[ERROR] ���÷�����ģʽʧ�ܣ��������: " << WSAGetLastError() << endl;
            closesocket(Server_Socket);
            WSACleanup();
            return false;
        }

        cout << "[ϵͳ] �׽�������Ϊ������ģʽ" << endl;

        // ���÷�������ַ
        memset(&Server_Address, 0, sizeof(Server_Address));
        Server_Address.sin_family = AF_INET;
        Server_Address.sin_port = htons(server_port);  // ʹ�ù̶��ķ������˿�
        inet_pton(AF_INET, SERVER_IP, &Server_Address.sin_addr);

        // �󶨵�ַ���׽���
        if (bind(Server_Socket, (sockaddr*)&Server_Address, sizeof(Server_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] �׽��ְ�ʧ�ܣ��������: " << WSAGetLastError() << endl;
            closesocket(Server_Socket);
            WSACleanup();
            return false;
        }

        cout << "[ϵͳ] �������׽��ְ󶨵����ص�ַ: �˿� " << server_port << endl;

        // ����Ŀ��·�ɵ�ַ
        memset(&Router_Address, 0, sizeof(Router_Address));
        Router_Address.sin_family = AF_INET;
        Router_Address.sin_port = htons(router_port);
        inet_pton(AF_INET, router_ip, &Router_Address.sin_addr);

        return true;
    }

    bool ThreeWay_Handshake() {//�������ֽ�������

        UDP_PACKET handshakePackets[3]; // ����������Ϣ
        socklen_t routerAddrLen = sizeof(Router_Address);// ·������ַ�ṹ�峤��

        memset(handshakePackets, 0, sizeof(handshakePackets)); // ������Ϣ�ṹ��
        auto Start_Time = chrono::steady_clock::now(); // ��¼��ʼʱ�䣬���ڳ�ʱ�ж�

        // ��һ�����֣����� SYN ��Ϣ
        while (true) {

            memset(&handshakePackets[0], 0, sizeof(handshakePackets[0])); // �����һ��������Ϣ
            if (recvfrom(Server_Socket, (char*)&handshakePackets[0], sizeof(handshakePackets[0]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {// ���տͻ��˵� SYN
                if (handshakePackets[0].Is_SYN() && handshakePackets[0].Check_IsValid()) {// ����Ƿ�����Ч�� SYN
                    cout << "[��־] ��һ�����ֳɹ�! �յ���SYN�� ���кţ�" << handshakePackets[0].seq << endl;
                    Seq = handshakePackets[0].seq;// ��¼�ͻ��˷��͵����к�

                    // ���õڶ������ֵ���Ϣ
                    handshakePackets[1].src_port = server_port;   // �������˵Ķ˿�
                    handshakePackets[1].dest_port = router_port; // ·�����Ķ˿�
                    handshakePackets[1].seq = ++Seq; // �������к�
                    handshakePackets[1].ack = handshakePackets[0].seq;     // ȷ�Ͽͻ������к�
                    handshakePackets[1].Set_SYN();//���ñ�־
                    handshakePackets[1].Set_ACK();
                    // ���͵ڶ���������Ϣ��SYN+ACK��
                    if (sendto(Server_Socket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                        (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[ERROR] �ڶ���������Ϣ����ʧ��!������룺" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[��־] �ڶ������֣����� ��SYN+ACK�� ���кţ�" << handshakePackets[1].seq
                        << "��ȷ�����кţ�" << handshakePackets[1].ack << endl;
                    break;// ��һ�����ֳɹ�������ѭ��
                }
                else {
                    cerr << "[WARNING] ������Ч��SYN��" << endl;// ������Ч�� SYN ��Ϣ
                }
            }
        }

        // �ȴ�������������Ϣ
        Start_Time = chrono::steady_clock::now();// ��¼��ʼʱ�䣬���ڳ�ʱ�ж�
        while (true) {
            // ���յ�����������Ϣ��ACK��
            if (recvfrom(Server_Socket, (char*)&handshakePackets[2], sizeof(handshakePackets[2]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {
                if (handshakePackets[2].Is_ACK() &&
                    handshakePackets[2].ack == handshakePackets[1].seq &&// ȷ�� ACK ���к�
                    handshakePackets[2].Check_IsValid()) {// ����Ƿ�����Ч�� ACK ��Ϣ
                    Seq = handshakePackets[2].seq;// �������к�
                    cout << "[��־] ���������ֳɹ����յ� ��ACK�� ȷ�����кţ�" << handshakePackets[2].ack << endl;
                    return true; // ���ӽ����ɹ������� true
                }
                else {
                    cerr << "[WARNING] ������Ч ��ACK��" << endl;
                }
            }

            // ��ʱ�������������ʱʱ��δ�յ�������������Ϣ�����·��͵ڶ���������Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - Start_Time).count() > TIMEOUT) {
                cout << "[��־] �ȴ�������������Ϣ��ʱ�����·��͡�SYN+ACK��" << endl;

                // ���¼���У���
                handshakePackets[1].check = handshakePackets[1].Calculate_Checksum();
                // ���·��͵ڶ���������Ϣ��SYN+ACK��
                if (sendto(Server_Socket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                    (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
                    cerr << "[ERROR] �ش� ��SYN+ACK��ʧ�ܣ�������룺" << WSAGetLastError() << endl;
                    return false;
                }
                Start_Time = now;
            }
        }
    }
    // �ļ���С��ʽ�������������ʺϵĵ�λ�͸�ʽ�����ַ���
    string FileSize_Form(uint64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = bytes;
        // �����ֽڴ�Сѡ���ʵ��ĵ�λ
        while (size >= 1024 && unit_index < 4) {
            size /= 1024;
            unit_index++;
        }
        // ��ʽ������ļ���С
        stringstream ss;
        ss << fixed << setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }

    // �����ļ�ͷ����Ϣ��ȷ�ϣ������ļ������ļ���С
    bool Recv_FileHead(char* file_name, UDP_PACKET& rec_msg, int& Waiting_Seq, socklen_t routerAddrLen) {
        // ��¼��ʼʱ�����ڳ�ʱ���
        auto start_time = chrono::steady_clock::now();

        while (true) {
            // ��������·��������Ϣ
            if (recvfrom(Server_Socket, (char*)&rec_msg, sizeof(rec_msg), 0,
                (SOCKADDR*)&Router_Address, &routerAddrLen) > 0) {

                // �ж��Ƿ�Ϊ��Ч���ļ�ͷ��Ϣ�������к���ȴ������к�ƥ��
                if (rec_msg.Is_CFH() && rec_msg.Check_IsValid() && rec_msg.seq == Waiting_Seq) {
                    File_Len = rec_msg.length;
                    strcpy_s(file_name, MAX_LEN, rec_msg.data);// ���ļ��������ļ���������

                    SetConsoleTextAttribute(hConsole, 14);
                    cout << "[Receive] "
                        << "\nFile_Name��" << file_name
                        << "\nFile_Size��" << FileSize_Form(File_Len) << endl;
                    SetConsoleTextAttribute(hConsole, 7);
                    cout << "---------------------------------------------------" << endl;
                    // ����������ȷ�ϰ���ȷ���յ��ļ�ͷ��Ϣ
                    UDP_PACKET ack_packet;
                    ack_packet.ack = rec_msg.seq;// ����ȷ�Ϻ�Ϊ���յ������к�
                    ack_packet.Set_ACK();// ����ACK��־
                    ack_packet.check = ack_packet.Calculate_Checksum();// ����У���

                    // ����ȷ�ϰ�
                    if (sendto(Server_Socket, (char*)&ack_packet, sizeof(ack_packet), 0,
                        (SOCKADDR*)&Router_Address, routerAddrLen) > 0) {
                        Waiting_Seq++;// ���ӵȴ������кţ�׼��������һ�����ݰ�
                        return true;
                    }
                }
                // ����ļ�ͷ��Ч����ΪCFH��Ϣ�������ظ�ACK
                else if (rec_msg.Is_CFH() && rec_msg.Check_IsValid()) {
                    Send_DuplicateACK(Waiting_Seq - 1);// �����ظ�ȷ�ϰ�
                }
            }

            // ��ʱ��飬��������趨�ĳ�ʱʱ�䣬���·����ظ�ACK�����ش�
            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > TIMEOUT) {
                SetConsoleTextAttribute(hConsole, 12);
                cout << "[Timeout] �ȴ��ļ�ͷ��ʱ!�����ش�" << endl;
                SetConsoleTextAttribute(hConsole, 7);
                Send_DuplicateACK(Waiting_Seq - 1);
                start_time = chrono::steady_clock::now();// ���¼�¼��ʼʱ��
            }
        }
    }
    // �����ظ�ACK��������֪ͨ���շ���Ҫ�ش�����
    void Send_DuplicateACK(int seq) {
        // ����һ��ȷ�ϰ���ACK��
        UDP_PACKET ack_packet;

        // ����ȷ�Ϻ�Ϊ��������к�
        ack_packet.ack = seq;

        // ����ACK��־����ʾ����һ��ȷ�ϰ�
        ack_packet.Set_ACK();
        ack_packet.check = ack_packet.Calculate_Checksum();

        // ����ACK����·����
        if (sendto(Server_Socket, (char*)&ack_packet, sizeof(ack_packet), 0,
            (SOCKADDR*)&Router_Address, sizeof(Router_Address)) > 0) {
            // ������ͳɹ�����ӡ���͵�ACK���кţ���ʾ�ش�����
            SetConsoleTextAttribute(hConsole, 12);
            cout << "[�ش�] �����ظ�ACK ���кţ�" << seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
        }
    }

    enum class Recv_Result {
        Success,
        Timeout,
        Error
    };

    // ����ʱ�Ľ������ݰ�����������һ��UDP���ݰ����ڳ�ʱ֮ǰ���ؽ��ս��
    Recv_Result Recv_TimeoutPacket(UDP_PACKET& packet, socklen_t routerAddrLen, int timeout_ms) {
        auto start_time = chrono::steady_clock::now();

        // ����ѭ�������Խ������ݰ�
        while (true) {
            // ���Խ������ݰ�������ɹ����յ�����
            if (recvfrom(Server_Socket, (char*)&packet, sizeof(packet), 0,
                (SOCKADDR*)&Router_Address, &routerAddrLen) > 0) {
                // ������ݰ����ճɹ������سɹ�״̬
                return Recv_Result::Success;
            }
            // ����Ƿ�ʱ�������ǰʱ���ȥ��ʼʱ�䳬����ָ���ĳ�ʱʱ��
            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > timeout_ms) {
                // �����ʱ�����س�ʱ״̬
                return Recv_Result::Timeout;
            }
        }
    }

    // ������յ������ݰ�������Ƿ�Ϊ��������ţ������½���״̬
    bool Handle_ReceivedPacket(const UDP_PACKET& packet, int& Waiting_Seq,
        File_Writetobuf& writer, uint64_t& total_received) {

        // ��ӡ���յ������ݰ���Ϣ�������źͱ�־λ��
        SetConsoleTextAttribute(hConsole, 11); // ǳ��ɫ
        cout << "[����] ���к�: " << packet.seq << " (Ԥ�����к�: " << Waiting_Seq << ")";
        // ����Ƿ�Ϊ���򵽴�
        if (packet.seq != Waiting_Seq) {
            SetConsoleTextAttribute(hConsole, 12); // ��ɫ
            cout << " [���򵽴�]";
        }
        // ��ӡ��־λ
        cout << " ��־:ACK ";
        if (packet.Is_ACK()) cout << "ACK ";// ����� ACK ��־����� ACK���Դ�����
        if (packet.Is_SYN()) cout << "SYN ";
        if (packet.Is_FIN()) cout << "FIN ";
        if (packet.Is_CFH()) cout << "CFH ";
        cout << "У���: 0x" << hex << packet.check << dec;
        if (!packet.Check_IsValid()) cout << " (У��ʧ��)";
        cout << endl;
        SetConsoleTextAttribute(hConsole, 7);

        // ������ݰ��Ƿ���Ч�������ȷ
        if (packet.Check_IsValid() && packet.seq == Waiting_Seq) {
            // ����ȷ�� ACK ��
            UDP_PACKET ack_packet;
            ack_packet.ack = packet.seq; // ����ȷ�����Ϊ���յ������
            ack_packet.Set_ACK();// ���� ACK ��־
            ack_packet.check = ack_packet.Calculate_Checksum();// ����У���

            // ����ȷ�ϰ�
            if (sendto(Server_Socket, (char*)&ack_packet, sizeof(ack_packet), 0,
                (SOCKADDR*)&Router_Address, sizeof(Router_Address)) > 0) {

                // д����յ�������
                writer.write(packet.data, packet.length);// ������д�뻺����
                total_received += packet.length;// �����ѽ��յ��ֽ���
                Waiting_Seq++; // ���µȴ�����һ�����к�
                return true;// ���سɹ�����ʾ�����굱ǰ���ݰ�
            }
        }
        // �����Ų��ԣ���У��ɹ��������ظ�ȷ�� ACK
        else if (packet.Check_IsValid()) {
            Send_DuplicateACK(Waiting_Seq - 1);// ������һ�����кŵ��ظ� ACK
        }
        return false;// ����ʧ�ܣ���ʾ���ݰ�δ����
    }
    // ���浱ǰ���յ����ֽ��������ڶϵ�ָ�
    void Save_CheckPoint(const string& file_path, uint64_t bytes_received) {
        string checkpoint_path = file_path + ".checkpoint";
        // �� checkpoint �ļ�����д��
        ofstream checkpoint(checkpoint_path);
        if (checkpoint.is_open()) {
            // ���ѽ��յ��ֽ���д���ļ�
            checkpoint << bytes_received;
            checkpoint.close();
        }
    }

    // ��ӡ���ս�����Ϣ���������յ��ֽ��������ֽ������ٶȵ�
    void Print_RecvProgress(uint64_t received, uint64_t total,
        chrono::steady_clock::time_point start_time) {

        static int last_percentage = 0;  // ��¼�ϴδ�ӡ�İٷֱ�
        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        double speed = received / elapsed / 1024; // ��������ٶȣ���λΪKB/s
        int percentage = static_cast<int>(received * 100.0 / total);

        // ÿ��5%��ӡһ�ν��ȣ����ߵ��������ʱ��ӡ100%
        if (percentage >= last_percentage + 5 || percentage == 100) {
            cout << "[���ս���] " << "��ɶȣ�"<<percentage << "% "
                <<"�ѽ��գ�" << FileSize_Form(received) << "/" << FileSize_Form(total)// ��ʾ�ѽ��պ����ļ���С
                <<"���ʣ�" << " (" << fixed << setprecision(2) << speed << " KB/s)" << endl;// ��ʾ�����ٶ�
            last_percentage = percentage;
        }
    }

    void Print_RecvStatus(chrono::steady_clock::time_point start_time,
        uint64_t total_bytes, const string& file_path) {

        auto end_time = chrono::steady_clock::now();
        double duration = chrono::duration<double>(end_time - start_time).count();
        double speed = (total_bytes / 1024.0) / duration; // KB/s

        SetConsoleTextAttribute(hConsole, 7);
        cout << "\n[Finish] �ļ��������"
            << "\n����λ�ã�" << file_path
            << "\n�ļ���С��" << FileSize_Form(total_bytes)
            << "\n�ܺ�ʱ��" << fixed << setprecision(2) << duration << " ��"
            << "\nƽ�������ʣ�" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    // �����ļ�����Ϣ�����������������ļ�ͷ�����ݰ��Ͷϵ���������
    bool Recv_Message(const string& outputDir) {
        Header_Seq = Seq;
        char file_name[MAX_LEN] = {};
        UDP_PACKET Message;
        int Waiting_Seq = Header_Seq + 1;// ��ʼ���ȴ������к�,�ȴ�����һλ�Ǳ�־λ֮��һλ
        socklen_t routerAddrLen = sizeof(Router_Address);
        uint64_t total_received_bytes = 0;

        // �����ļ�ͷ
        if (!Recv_FileHead(file_name, Message, Waiting_Seq, routerAddrLen)) {
            return false;
        }

        // ׼���ļ�д��
        string filePath = outputDir + "/" + string(file_name);
        File_Writetobuf fileWriter(filePath, 1024 * 1024); // �����ļ�д�뻺������1MB ������

        auto start_time = chrono::steady_clock::now();//��ʼʱ��
        auto last_progress_update = start_time;//�ϴν��ȸ��µ�ʱ��

        cout << "\n[���տ�ʼ] ��ʼ�����ļ�����...\n" << endl;

        // ������ѭ����ֱ�����յ��ֽ����ﵽ�ļ���С
        while (total_received_bytes < File_Len) {
            UDP_PACKET packet;
            // ���ý��մ���ʱ�ĺ������������ݰ�
            auto receive_result = Recv_TimeoutPacket(packet, routerAddrLen, TIMEOUT);

            if (Handle_ReceivedPacket(packet, Waiting_Seq, fileWriter, total_received_bytes)) {
                // ���½�����ʾ
                auto current_time = chrono::steady_clock::now();
                if (chrono::duration_cast<chrono::milliseconds>(current_time - last_progress_update).count() >= 100) {
                    Print_RecvProgress(total_received_bytes, File_Len, start_time);
                    last_progress_update = current_time;// �����ϴν��ȸ��µ�ʱ��
                }
            }

            // ����Ƿ���Ҫ����ϵ�������Ϣ��// ÿ����5MB������ʱ������һ�ζϵ���Ϣ
            if (total_received_bytes % (5 * 1024 * 1024) == 0) { // ÿ5MB����һ��
                Save_CheckPoint(filePath, total_received_bytes); // ����ϵ�
            }
        }

        // ��ɽ��գ����ͳ����Ϣ
        fileWriter.flush();
        Print_RecvStatus(start_time, total_received_bytes, filePath);

        Seq = Waiting_Seq - 1;// �������кţ��ڵȴ����ǰһλ
        return true;
    }

    // �Ĵλ��ֺ�����ģ�����ӹرյ��Ĵλ��ֹ���
    bool FourWay_Wavehand() {
        UDP_PACKET Is_FIN[4];        // �Ĵλ�����Ϣ
        socklen_t routerAddrLen = sizeof(Router_Address);// ·������ַ�ṹ�峤��
        auto startTime = chrono::steady_clock::now();

        // ��ʼ��������Ϣ
        memset(Is_FIN, 0, sizeof(Is_FIN)); // ������Ϣ�ṹ������

        // ��һ�λ���: ���� FIN ��Ϣ
        while (true) {
            if (recvfrom(Server_Socket, (char*)&Is_FIN[0], sizeof(Is_FIN[0]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {// �������Կͻ��˵� FIN ��Ϣ
                if (Is_FIN[0].Is_FIN() && Is_FIN[0].Check_IsValid()) {// ����Ƿ�Ϊ��Ч�� FIN ��Ϣ
                    cout << "[��־] �յ���һ�λ�����Ϣ��FIN�� ���кţ�" << Is_FIN[0].seq << endl;
                    break;// ���ճɹ����˳�ѭ��
                }
                else {
                    cerr << "[WARNING] ������Ч��FIN��" << endl;
                }
            }
        }
        Seq = Is_FIN[0].seq;// �������к�


        // �ڶ��λ���: ���� ACK ��Ϣ
        memset(&Is_FIN[2], 0, sizeof(Is_FIN[1]));// ����ڶ��λ�����Ϣ�ṹ��
        Is_FIN[1].src_port = server_port;// ����Դ�˿�Ϊ�������˿�
        Is_FIN[1].dest_port = router_port;
        Is_FIN[1].Set_ACK();
        Is_FIN[1].ack = Is_FIN[0].seq;// ȷ�Ͽͻ��˵� FIN ��Ϣ���к�
        Is_FIN[1].seq = ++Seq;
        Is_FIN[1].check = Is_FIN[1].Calculate_Checksum();

        // ���� ACK ��Ϣ
        if (sendto(Server_Socket, (char*)&Is_FIN[1], sizeof(Is_FIN[1]), 0,
            (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[ERROR] �ڶ��λ�����Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[��־] �ڶ��λ��֣����͡�ACK�� ȷ�����кţ�" << Is_FIN[1].ack << endl;
        Seq = Is_FIN[1].seq;

        // �����λ���: ���� FIN ��Ϣ
        memset(&Is_FIN[2], 0, sizeof(Is_FIN[2]));// ��������λ�����Ϣ�ṹ��
        Is_FIN[2].src_port = server_port;
        Is_FIN[2].dest_port = router_port;
        Is_FIN[2].seq = ++Seq;
        Is_FIN[2].ack = Is_FIN[1].seq; // ȷ�ϵڶ��λ�����Ϣ�����к�
        Is_FIN[2].Set_FIN();
        Is_FIN[2].Set_ACK();
        Is_FIN[2].check = Is_FIN[2].Calculate_Checksum();
        startTime = chrono::steady_clock::now();
        cout << "[��־] �����λ��֣����� ��FIN�� ���кţ�" << Is_FIN[2].seq << endl;

        // ���� FIN ��Ϣ
        if (sendto(Server_Socket, (char*)&Is_FIN[2], sizeof(Is_FIN[2]), 0,
            (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[ERROR] �����λ�����Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }

        while (true) {
            // ���Ĵλ���: ���� ACK ��Ϣ
            if (recvfrom(Server_Socket, (char*)&Is_FIN[3], sizeof(Is_FIN[3]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {// �������Կͻ��˵� ACK ��Ϣ
                if (Is_FIN[3].Is_ACK() &&
                    Is_FIN[3].ack == Is_FIN[2].seq &&// ȷ�ϵ����λ�����Ϣ�����к�
                    Is_FIN[3].Check_IsValid()) {
                    cout << "[��־] �յ����Ĵλ�����Ϣ ��ACK�� ȷ�����кţ�" << Is_FIN[3].ack << endl;
                    break;
                }
                else {
                    cerr << "[WARNING] ������Ч ��ACK�� ��Ϣ" << endl;
                }
            }

            // ��ʱ����: �����ʱδ���յ����Ĵλ�����Ϣ���ش������λ�����Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - startTime).count() > TIMEOUT) {
                cout << "[��־] ��FIN�� ��ʱ�����·���" << endl;
                Is_FIN[2].check = Is_FIN[2].Calculate_Checksum(); // ����У���
                if (sendto(Server_Socket, (char*)&Is_FIN[2], sizeof(Is_FIN[2]), 0,
                    (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
                    cerr << "[ERROR] �ش�ʧ�ܡ�" << endl;
                    return false;
                }
                startTime = now;//����
            }
        }

        return true;
    }


    ~UDPSERVER() {
        if (Server_Socket != INVALID_SOCKET) {
            closesocket(Server_Socket);
            WSACleanup();
            cout << "[��־] �׽����ѹر� ��Դ���ͷ�" << endl;
        }
    }
};


int main() {
    char ip_input[16];  // ���ڴ洢�û������IP��ַ
    cout << "---------------------------------------------------" << endl;
    cout << "||             ------->���ն�                    ||" << endl;
    cout << "---------------------------------------------------" << endl;
    // ��ȡ�û����������
    cout << "��������ն�IP��ַ: ";
    cin >> ip_input;
    SERVER_IP = ip_input;  // ���û������IP��ַ��ֵ��ȫ�ֱ���
   // cout << "��������ն˶˿�: ";
   // cin >> SERVER_PORT;
    
    UDPSERVER receiver;
    if (!receiver.INIT()) {
        cerr << "[ERROR] ���ն˳�ʼ��ʧ�ܣ�" << endl;
        return 0;
    }

    cout << "[��־] ��ʼ����ϣ��ȴ����Ͷ�����..." << endl;

    if (!receiver.ThreeWay_Handshake()) {
        cerr << "[ERROR] ��������ʧ�ܣ��޷��������ӣ�" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "---------------------------------------------------" << endl;
        cout << "Please choose an option��\n1. SAVE FILE   2. EXIT\nSelect��";
        cin >> choice;

        if (choice == 1) {
            string output_dir;
           // cout << "Ŀ�����Ŀ¼��";
            //cin >> output_dir;
            output_dir = R"(D:\comput_network\project3-3\Project2\Project2)";
            cout << "---------------------------------------------------" << endl;
            cout << "[��־] ���ڵȴ����Ͷ˷����ļ�..." << endl;
            if (!receiver.Recv_Message(output_dir)) {
                cerr << "[ERROR] �ļ�����ʧ�ܣ�" << endl;
            }
        }
        else if (choice == 2) {
            cout << "[��־] �ȴ����Ͷ˽��в���..." << endl;
            if (!receiver.FourWay_Wavehand()) {
                cerr << "[ERROR] �Ͽ�����ʧ�ܣ�" << endl;
            }
            else {
                cout << "[��־] �Ͽ����ӳɹ���" << endl;
            }
        }
        else {
            cerr << "[WARNING] �������󣡣������������룺" << endl;
        }
    } while (choice != 2);

    return 0;
}