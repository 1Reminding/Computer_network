#include "UDP.h" // ���� UDP �Ķ���
#pragma comment(lib, "ws2_32.lib") // ���� Windows Sockets ��

// ȫ�ֱ�������
//uint16_t CLIENT_PORT;
const char* CLIENT_IP = "127.0.0.1";  // Ĭ��ֵ
int Windows_Size;  // ����ȫ�ֱ������ڴ�С

//���̱߳�������
atomic_int Base_Seq(1);//��ָ��
atomic_int Next_Seq(1);//δȷ��
atomic_int Head_Seq(0);//����������Ҫ������־λ
atomic_int Count(0);

mutex mtx;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

atomic_bool Resend(false);
atomic_bool Completed(false);

class UDPCLIENT {
private:
    SOCKET Client_Socket;           // �ͻ����׽���
    sockaddr_in Client_Address;        // �ͻ��˵�ַ
    sockaddr_in Router_Address;        // Ŀ��·�ɵ�ַ
    uint32_t Seq;                  // �ͻ��˵�ǰ���к�
    uint32_t file_length;
    socklen_t addr_len = sizeof(Router_Address);

    int Message_Num;                   //������Ϣ����

    // ������ģ�����
    double packet_loss_rate;    // ������ (0.0 - 1.0)
    int delay_ms;              // �ӳ�ʱ��(ms)

public:
    UDPCLIENT() : Client_Socket(INVALID_SOCKET), Seq(0), Message_Num(0),
        addr_len(sizeof(Router_Address)),
        packet_loss_rate(0.01),    // Ĭ��10%������,������������
        delay_ms(2)             // Ĭ��100ms�ӳ٣�������������
    {}
    // �������ģ������ķ���
    void Set_SimulationParams(double loss_rate, int delay) {
        packet_loss_rate = loss_rate;
        delay_ms = delay;
    }

    bool INIT() {
        // ��ʼ��
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[ERROR] WSAStartup ʧ�ܣ��������: " << result << endl;
            return false;
        }
        // �汾ƥ��
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[ERROR] WinSock �汾��֧�֣�" << endl;
            WSACleanup();
            return false;
        }
        // ���� UDP �׽���
        Client_Socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (Client_Socket == INVALID_SOCKET) {
            cerr << "[ERROR] �׽��ִ���ʧ��!�������: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[ϵͳ] �׽��ִ����ɹ�!" << endl;
        // ���÷�����ģʽ
        u_long mode = 1;
        if (ioctlsocket(Client_Socket, FIONBIO, &mode) != 0) {
            cerr << "[ERROR] ���÷�����ģʽʧ��! �������: " << WSAGetLastError() << endl;
            closesocket(Client_Socket);
            WSACleanup();
            return false;
        }
        cout << "[ϵͳ] �׽�������Ϊ������ģʽ" << endl;
        // ���ÿͻ��˵�ַ
        memset(&Client_Address, 0, sizeof(Client_Address));
        Client_Address.sin_family = AF_INET;
        Client_Address.sin_port = htons(client_port);
        inet_pton(AF_INET, CLIENT_IP, &Client_Address.sin_addr);
        // �󶨿ͻ��˵�ַ���׽���
        if (bind(Client_Socket, (sockaddr*)&Client_Address, sizeof(Client_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] �׽��ְ�ʧ��!�������: " << WSAGetLastError() << endl;
            closesocket(Client_Socket);
            WSACleanup();
            return false;
        }
        cout << "[ϵͳ] �׽��ְ󶨵����ص�ַ: �˿� " << client_port << endl;
        // ����Ŀ��·�ɵ�ַ
        memset(&Router_Address, 0, sizeof(Router_Address));
        Router_Address.sin_family = AF_INET;
        Router_Address.sin_port = htons(router_port);
        inet_pton(AF_INET, router_ip, &Router_Address.sin_addr);

        return true;
    }

    bool ThreeWay_Handshake() {
        UDP_PACKET con_msg[3];  // ����������Ϣ������ SYN��SYN+ACK��ACK ��Ϣ

        // ��һ������
        con_msg[0] = {}; // ��սṹ�壬ȷ��û��������
        con_msg[0].src_port = client_port;// ����Դ�˿ڣ��ͻ��˶˿ڣ�
        con_msg[0].dest_port = router_port;
        con_msg[0].Set_SYN();                  // ���� SYN ��־λ����ʾ����������
        con_msg[0].seq = ++Seq;                // �������кţ����кŵ���
        con_msg[0].check = con_msg[0].Calculate_Checksum(); // ����У���
        auto msg1_Send_Time = chrono::steady_clock::now(); // ��¼��һ�����ַ��͵�ʱ�䣬���ڳ�ʱ�ش�

        // ���͵�һ��������Ϣ��SYN��
        cout << "[��־] ��һ�����֣����� ��SYN��..." << endl;
        if (sendto(Client_Socket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] ��һ��������Ϣ����ʧ�ܣ�" << endl;
            return false;
        }

        // �ڶ�������
        socklen_t addr_len = sizeof(Router_Address);// �洢·������ַ�ĳ���
        while (true) {
            // ���� SYN+ACK ��Ϣ
            if (recvfrom(Client_Socket, (char*)&con_msg[1], sizeof(con_msg[1]), 0,
                (sockaddr*)&Router_Address, &addr_len) > 0) {
                // ��֤�յ�����Ϣ�Ƿ��� SYN+ACK ����Ч
                if (con_msg[1].Is_ACK() && con_msg[1].Is_SYN() && con_msg[1].Check_IsValid() &&
                    con_msg[1].ack == con_msg[0].seq) {
                    cout << "[��־] �ڶ������ֳɹ����յ� ��SYN+ACK��" << endl;
                    break;// ���ֳɹ�������ѭ��
                }
                else {
                    cerr << "[ERROR] �ڶ���������Ϣ��֤ʧ�ܣ�" << endl;
                }
            }

            // ����ڶ���������Ϣ��ʱ�����·��͵�һ��������Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - msg1_Send_Time).count() > TIMEOUT) {
                cout << "[��־] ��ʱ���ش���һ��������Ϣ��" << endl;
                con_msg[0].check = con_msg[0].Calculate_Checksum(); // ���¼����һ��������Ϣ��У���
                if (sendto(Client_Socket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
                    (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
                    cerr << "[ERROR] �ش�ʧ�ܣ�" << endl;
                    return false;
                }
                msg1_Send_Time = now; // ���·���ʱ��
            }
        }
        // ���õ�ǰ���к�Ϊ���յ��� SYN+ACK ��Ϣ�����к�
        Seq = con_msg[1].seq;
        // ����������
        con_msg[2] = {}; // ��սṹ��
        con_msg[2].src_port = client_port;
        con_msg[2].dest_port = router_port;
        con_msg[2].seq = ++Seq;           // �������к�
        con_msg[2].ack = con_msg[1].seq;  // ����ȷ�Ϻţ�ȷ���յ��ڶ������ֵ����к�
        con_msg[2].Set_ACK();             // ���� ACK ��־λ����ʾȷ���յ� SYN+ACK ��Ϣ
        con_msg[2].check = con_msg[2].Calculate_Checksum(); // ����У���
        // ���͵�����������Ϣ��ACK��
        cout << "[��־] ���������֣����� ��ACK��..." << endl;
        if (sendto(Client_Socket, (char*)&con_msg[2], sizeof(con_msg[2]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] ������������Ϣ����ʧ�ܣ�" << endl;
            return false;
        }
        cout << "[��־] ����������ɣ����ӽ����ɹ���" << endl;
        return true; // �������ֳɹ������� true
    }

    void Reset() {
        Completed = false;
        Resend = false;

        Next_Seq = 1;
        Base_Seq = 1;

        Head_Seq = 0;
        Message_Num = 0;
    }

    // ACK ����������ش�����
    void Fast_Ack() {//ACK�����߳�
        // ���ڿ����ش����Ƶļ���������ֵ
        int Err_ack_Num = 0;  // �ϴ��յ��� ACK ���к�
        int resend_threshold = 3;  // �趨�ظ�ȷ�ϵ��ط���ֵ�������ظ�����
        int resend_counter = 0;  // ����ͳ����������ͬ ACK

        while (true) {
            UDP_PACKET ack_msg;

            // ����ACK��Ϣ
            if (recvfrom(Client_Socket, (char*)&ack_msg, sizeof(ack_msg), 0, (SOCKADDR*)&Router_Address, &addr_len)) {
                // ȷ�����յ��İ�����Ч�� ACK
                if (ack_msg.Is_ACK() && ack_msg.Check_IsValid()) {
                    lock_guard<mutex> lock(mtx);  // ���������������
                    SetConsoleTextAttribute(hConsole, 10); // ��ɫ
                    cout << "[У��] �յ�ACKУ���: 0x" << hex << ack_msg.check
                        << dec << " (ȷ�Ϻ�: " << ack_msg.ack << ")" << endl;
                    SetConsoleTextAttribute(hConsole, 7);
                    cout << "[��־] ���յ�ȷ����Ϣ��ACK ���кţ� " << ack_msg.ack << endl;

                    // �ۻ�ȷ�ϣ����´��ڻ����
                // Base_Seq��Ҫ��ȥHeader_Seq������ʵ�����ݰ����
                    if (ack_msg.ack >= Base_Seq + Head_Seq) {
                        Base_Seq = ack_msg.ack - Head_Seq + 1;//���д��㿪ʼ
                    }
                    Print_WindowStatus();
                    // ����Ƿ�����ɣ����յ���ACK�������һ�����ݰ������
                    if (ack_msg.ack - Head_Seq == Message_Num + 1) {
                        Completed = true;// ������ɱ�־
                        return;  // ��ɴ��䣬�˳��߳�
                    }

                    // ���� ACK �ط�����
                    if (Err_ack_Num != ack_msg.ack) {
                        Err_ack_Num = ack_msg.ack;
                        resend_counter = 0;  // ���������
                    }
                    else {
                        resend_counter++;
                        if (resend_counter >= resend_threshold) {
                            Resend = true;  // �ﵽ�ط���ֵ�������ط���־
                            resend_counter = 0;  // ���������
                        }
                    }
                }
            }
        }
    }

    // �����ļ�ͷ��Ϣ��
    bool Send_FileHead(UDP_PACKET* data_msg, const string& file_name) {
        // ���ļ������Ƶ����ݰ���data�ֶ���
        strcpy_s(data_msg[0].data, file_name.c_str());
        // ȷ���ļ���ĩβ�� '\0' ��β
        data_msg[0].data[strlen(data_msg[0].data)] = '\0';
        data_msg[0].length = file_length;
        data_msg[0].seq = ++Seq;// �������кţ�ȷ��ÿ���������к�Ψһ
        // �����ļ�ͷ��־
        data_msg[0].Set_CFH();
        data_msg[0].src_port = client_port;
        data_msg[0].dest_port = router_port;
        data_msg[0].check = data_msg[0].Calculate_Checksum();

        SetConsoleTextAttribute(hConsole, 11); // ǳ��ɫ
        cout << "[����] �ļ�ͷ��Ϣ����" << file_name
            << " (���к�: " << data_msg[0].seq << ")" << endl;
        SetConsoleTextAttribute(hConsole, 7);
        // �����ļ�ͷ��Ϣ��
        if (sendto(Client_Socket, (char*)&data_msg[0], sizeof(data_msg[0]), 0,
            (SOCKADDR*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            // �������ʧ�ܣ����������־������ false
            SetConsoleTextAttribute(hConsole, 12); // ��ɫ
            cerr << "[ERROR] �ļ�ͷ����ʧ�ܣ������룺" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        return true;
    }

    // �����ļ����ݰ�
    bool Send_FileData(UDP_PACKET* data_msg, ifstream& file, int next_seq, int last_length) {
        // �����Ƿ�Ϊ���һ�����ݰ�����ȡ����
        if (next_seq == Message_Num && last_length) {
            // ��������һ�����ݰ�����ȡʣ����ļ�����
            file.read(data_msg[next_seq - 1].data, last_length);
            data_msg[next_seq - 1].length = last_length;// �������ݰ�����Ϊʣ�೤��
        }
        else {
            // �����ȡ�̶���С�����ݿ�
            file.read(data_msg[next_seq - 1].data, MAX_LEN);
            data_msg[next_seq - 1].length = MAX_LEN;// �������ݰ�����Ϊ���ֵ
        }

        // �������ݰ������кš��˿ںź�У��͵�����
        data_msg[next_seq - 1].seq = ++Seq;
        data_msg[next_seq - 1].src_port = client_port;
        data_msg[next_seq - 1].dest_port = router_port;
        data_msg[next_seq - 1].check = data_msg[next_seq - 1].Calculate_Checksum();

        SetConsoleTextAttribute(hConsole, 14); // ��ɫ
        cout << "[У��] �������ݰ�У���: 0x" << hex << data_msg[next_seq - 1].check
            << dec << " (���к�: " << data_msg[next_seq - 1].seq << ")" << endl;
        SetConsoleTextAttribute(hConsole, 7);

        // ģ�����ݰ���ʧ�����ݶ��������������
        if ((double)rand() / RAND_MAX < packet_loss_rate) {
            SetConsoleTextAttribute(hConsole, 12); // ��ɫ
            cout << "[ģ��] ���ݰ���ʧ�����кţ�" << data_msg[next_seq - 1].seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return true;  // �����󷵻�true����������
        }

        // ģ�����ݰ��ӳ٣�����������ӳ�ʱ�䣩
        if (delay_ms > 0) {
            SetConsoleTextAttribute(hConsole, 14); // ��ɫ
            cout << "[ģ��] ���ݰ��ӳ� " << delay_ms << "ms�����кţ�" << data_msg[next_seq - 1].seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
            this_thread::sleep_for(chrono::milliseconds(delay_ms));// ģ���ӳ�
        }
        // �������ݰ���·����
        if (sendto(Client_Socket, (char*)&data_msg[next_seq - 1], sizeof(data_msg[next_seq - 1]), 0,
            (SOCKADDR*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            // �������ʧ�ܣ����������Ϣ������ false
            SetConsoleTextAttribute(hConsole, 12);
            cerr << "[ERROR] ���ݰ�����ʧ�ܣ����кţ�" << data_msg[next_seq - 1].seq
                << "�������룺" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        // ���ݰ��ɹ����ͣ�����ɹ���־
        cout << "[��־] �ɹ��������ݰ������кţ� " << data_msg[next_seq - 1].seq << endl;
        return true;
    }

    // �ش�δȷ�ϵ����ݰ�
    void Resend_Data(UDP_PACKET* data_msg) {
        SetConsoleTextAttribute(hConsole, 12);
        cout << "\n[�ش�] ��ʼ�ش�δȷ�ϵ����ݰ�..." << endl;
        SetConsoleTextAttribute(hConsole, 7);

        // ������ǰ�����ڵ�����δȷ�ϵ����ݰ��������ش�
        for (int i = 0; i < Next_Seq - Base_Seq; i++) {
            lock_guard<mutex> lock(mtx);// ʹ�û�������ȷ���̰߳�ȫ
            int resend_seq = Base_Seq + i - 1; // ������Ҫ�ش������ݰ����к�
            // ���¼������ݰ���У���
            data_msg[resend_seq].check = data_msg[resend_seq].Calculate_Checksum();
            // �����ش������ݰ�
            if (sendto(Client_Socket, (char*)&data_msg[resend_seq], sizeof(data_msg[resend_seq]), 0,
                (SOCKADDR*)&Router_Address, sizeof(Router_Address)) != SOCKET_ERROR) {

                SetConsoleTextAttribute(hConsole, 14);
                cout << "[�ش�] ���ݰ��ش��ɹ������кţ�" << resend_seq + Head_Seq + 1 << endl;
                SetConsoleTextAttribute(hConsole, 7);
            }
        }
        Resend = false;// �ش���ɣ����±�־
    }
    // ��ʽ���ļ���СΪ�ɶ���ʽ��B, KB, MB, GB, TB��
    string FileSize_Form(uint64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = bytes; // ���ֽ���תΪ double ���ͣ��Ա㴦�������ļ�
        // ���ֽ���ת��Ϊ�ʺϵĵ�λ��B -> KB -> MB -> GB -> TB��
        while (size >= 1024 && unit_index < 4) {
            size /= 1024;
            unit_index++;
        }
        // ʹ�� stringstream ��ʽ�������������λС��
        stringstream ss;
        ss << fixed << setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }
    // ��ӡ���ڵ�״̬������ Base �� Next ���кż����������Ϣ
    void Print_WindowStatus() {
        static int last_base = -1; // �ϴδ�ӡ�� Base_Seq
        static int last_next = -1;
        // ��� Base_Seq �� Next_Seq �����仯�����´���״̬
        if (last_base != Base_Seq || last_next != Next_Seq) {
            SetConsoleTextAttribute(hConsole, 11);
            cout << "[����] Base: " << Base_Seq
                << " Next: " << Next_Seq
                << " δȷ��: " << Next_Seq - Base_Seq
                << " ����ʣ��ռ�: " << Windows_Size - (Next_Seq - Base_Seq)
                << "[λ��] ��������ʼλ��: " << Base_Seq
                << ", ���ڽ���λ��: " << Base_Seq + Windows_Size - 1<<"��"
                << endl;
            SetConsoleTextAttribute(hConsole, 7);
            last_base = Base_Seq;
            last_next = Next_Seq;
        }
    }
    // ��ӡ���ս��ȣ������ѽ��յ����ݡ������ݡ������ٶ�
    void Print_RecvProgress(uint64_t transferred, uint64_t total, chrono::steady_clock::time_point start_time) {
        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        double speed = (transferred - total) / elapsed / 1024; // KB/s
        int percentage = (int)((transferred - total) * 100.0 / total);// ������ս��ȵİٷֱ�

        // ���������//const int bar_width = 50;//int filled = bar_width * percentage / 100;

        cout << "\r[����] ��";
        cout << percentage << "%��"
            << FileSize_Form(transferred - total) << "/" << FileSize_Form(total) // ��ʾ�ѽ��պ�������������ʽ����
            << " (" << fixed << setprecision(2) << speed << " KB/s)    " << flush; // ��ʾ�����ٶ�
        cout << endl;
    }
    // ��ӡ����״̬��������������ֽ����������ʱ��ƽ��������
    void Print_TransStatus(chrono::steady_clock::time_point start_time, uint32_t total_bytes) {
        auto end_time = chrono::steady_clock::now();
        double duration = chrono::duration<double>(end_time - start_time).count();
        double speed = (total_bytes / 1024.0) / duration; // KB/s

        SetConsoleTextAttribute(hConsole, 7); // ��ɫ
        cout << "\n[FINISH] �ļ�������ɣ�"
            << "\n�ļ���С��" << FileSize_Form(total_bytes)
            << "\n�����ʱ��" << fixed << setprecision(2) << duration << " ��"
            << "\nƽ�������ʣ�" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }
    // �����ļ���Ϣ��������
    bool Send_Message(string file_path) {
        // ���ļ���������
        ifstream file(file_path, ios::binary);// �Զ�����ģʽ���ļ�
        if (!file.is_open()) {
            cerr << "[ERROR] �޷����ļ���" << file_path << "\n����ԭ��" << endl;
            return false;
        }

        // ��ȡ�ļ���Ϣ���ļ������ļ���С��
        size_t pos = file_path.find_last_of("/\\");
        string file_name = (pos != string::npos) ? file_path.substr(pos + 1) : file_path;
        file.seekg(0, ios::end);
        file_length = file.tellg(); //cout << file_length;
        file.seekg(0, ios::beg);

        SetConsoleTextAttribute(hConsole, 14); // ��ɫ
        cout << "\n[Send] File_name��" << file_name
            << "\nFile_size��" << FileSize_Form(file_length) << endl;
        cout << "File_length:" << file_length << endl;
        SetConsoleTextAttribute(hConsole, 7);  // �ָ�Ĭ��ɫ
        cout << "---------------------------------------------------" << endl;

        // �����ļ��ֿ���Ϣ
        int complete_num = file_length / MAX_LEN; // �����������ݿ���
        int last_length = file_length % MAX_LEN; // �������һ�����ݿ��ʣ�೤��
        Head_Seq = Seq;// �����ļ�ͷ�����к�
        Message_Num = complete_num + (last_length != 0); // ����Ϣ�����������һ�������������ݿ飩

        // ��������ȷ���̣߳���������ack
        thread ackThread([this]() {
            this->Fast_Ack();
            });
        // �������ݰ�����,�����0��ʼ�����ݰ���1��ʼ
        unique_ptr<UDP_PACKET[]> data_msg(new UDP_PACKET[Message_Num + 1]);
        auto start_time = chrono::steady_clock::now();
        uint64_t total_sent_bytes = 0;

        cout << "\n[���俪ʼ] ��ʼ�����!��ʼ����...\n" << endl;

        // ������ѭ����ֱ���������
        while (!Completed) {
            // �ش����������Ҫ�ش����ݰ�
            if (Resend) {
                Resend_Data(data_msg.get());// �ش�δȷ�ϵ����ݰ�
                continue;// ������ǰѭ���������ش�
            }

            // �������ʹ���
            if (Next_Seq < Base_Seq + Windows_Size && Next_Seq <= Message_Num + 1) {
                lock_guard<mutex> lock(mtx);// ��������֤�̰߳�ȫ

                if (Next_Seq == 1) {// ����ǵ�һ�����������ļ�ͷ
                    // �����ļ�ͷ,��������ack
                    if (!Send_FileHead(data_msg.get(), file_name)) {
                        return false;
                    }
                }
                else {
                    // �����ļ�����
                    if (!Send_FileData(data_msg.get(), file, Next_Seq, last_length)) {
                        return false;
                    }
                }
                Print_WindowStatus();
                // ���´����ٶȣ�������ͼ
                total_sent_bytes += data_msg[Next_Seq - 1].length;
                Print_RecvProgress(total_sent_bytes, file_length, start_time);

                Next_Seq++;

            }

            //�������� - �����ڽӽ���ʱ�ӳٷ���
            if (Next_Seq - Base_Seq > Windows_Size * 0.8) {
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }
        // �ȴ�ȷ���߳̽���
        ackThread.join();

        // ��ӡ����ͳ����Ϣ
        Print_TransStatus(start_time, file_length);
        // ����״̬
        Reset();
        file.close();
        return true;
    }
    // �Ĵλ���Э���ʵ��
    bool Four_Wavehand() {
        UDP_PACKET wavehand_packets[4]; // �����Ĵλ�����Ϣ����
        socklen_t addr_len = sizeof(Router_Address);
        auto start_time = chrono::steady_clock::now();

        // ��ʼ��������Ϣ����
        memset(wavehand_packets, 0, sizeof(wavehand_packets)); // ������Ϣ�ṹ������

        // ��һ�λ���: ���� FIN ��Ϣ
        wavehand_packets[0].src_port = client_port;// ����Դ�˿�
        wavehand_packets[0].dest_port = router_port; // ����Ŀ��˿�
        wavehand_packets[0].Set_FIN();// ���� FIN ��־
        wavehand_packets[0].seq = ++Seq; // �������к�
        wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum();
        cout << "[��־] ��һ�λ��֣����� ��FIN��  ���кţ�" << wavehand_packets[0].seq << endl;
        // ���� FIN ��Ϣ
        if (sendto(Client_Socket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] ��FIN�� ��Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        // �ڶ��λ���: �ȴ� ACK ��Ϣ
        while (true) {
            // ���� ACK ��Ϣ
            if (recvfrom(Client_Socket, (char*)&wavehand_packets[1], sizeof(wavehand_packets[1]), 0,
                (sockaddr*)&Router_Address, &addr_len) > 0) {
                // ��֤�Ƿ�Ϊ��Ч�� ACK ��Ϣ
                if (wavehand_packets[1].Is_ACK() &&
                    wavehand_packets[1].ack == wavehand_packets[0].seq &&
                    wavehand_packets[1].Check_IsValid()) {
                    cout << "[��־] �յ��ڶ��λ�����Ϣ ��ACK��ȷ�����кţ�" << wavehand_packets[1].ack << endl;
                    break;// ȷ�ϳɹ����˳�ѭ��
                }
                else {
                    cerr << "[WARNING] ������Ч ��ACK��" << endl;
                }
            }
            // ��ʱ�ش���һ�λ�����Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cout << "[��־] ��FIN�� ��Ϣ��ʱ�����·��͡�" << endl;
                wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum(); // ����У���
                // ���·��� FIN ��Ϣ
                if (sendto(Client_Socket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
                    (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
                    cerr << "[ERROR] �ش�ʧ�ܣ�" << endl;
                    return false;
                }
                start_time = now; // ���¼�ʱ�����¿�ʼ��ʱ���
            }
        }
        // �����λ���: ���� FIN ��Ϣ
        start_time = chrono::steady_clock::now();
        while (true) {
            // ���� FIN ��Ϣ
            if (recvfrom(Client_Socket, (char*)&wavehand_packets[2], sizeof(wavehand_packets[2]), 0,
                (sockaddr*)&Router_Address, &addr_len) > 0) {
                cout << wavehand_packets[2].Is_FIN() << wavehand_packets[2].Check_IsValid();
                // ����Ƿ�Ϊ��Ч�� FIN ��Ϣ
                if (wavehand_packets[2].Is_FIN() && wavehand_packets[2].Check_IsValid()) {
                    cout << "[��־] �յ������λ�����Ϣ��FIN�� ���кţ�" << wavehand_packets[2].seq << endl;
                    break;// �ɹ����յ� FIN ��Ϣ���˳�ѭ��
                }
                else {
                    cerr << "[WARNING] ������Ч ��FIN�� " << endl;
                }
            }
            // ��ʱ����
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cerr << "[��־] �ȴ� ��FIN�� ��ʱ���Ͽ�����ʧ�ܣ�" << endl;
                return false; // ��ʱδ���յ���Ч�� FIN ��Ϣ������ʧ��
            }
        }
        Seq = wavehand_packets[2].seq;
        // ���Ĵλ���: ���� ACK ��Ϣ
        wavehand_packets[3].src_port = client_port;
        wavehand_packets[3].dest_port = router_port;
        wavehand_packets[3].Set_ACK();
        wavehand_packets[3].ack = wavehand_packets[2].seq;// ����ȷ�����к�Ϊ�����λ��ֵ� FIN ��Ϣ�����к�
        wavehand_packets[3].seq = ++Seq;
        wavehand_packets[3].check = wavehand_packets[3].Calculate_Checksum();
        // ���͵��Ĵλ��ֵ� ACK ��Ϣ
        if (sendto(Client_Socket, (char*)&wavehand_packets[3], sizeof(wavehand_packets[3]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] ���Ĵλ�����Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[��־] ���Ĵλ��֣����� ��ACK��ȷ�����кţ�" << wavehand_packets[3].ack << endl;

        //  �ȴ�������ʱʱ�� ��ȷ����Ϣ���
        cout << "[��־] �ȴ� 2 * TIMEOUT ȷ�����ӶϿ�..." << endl;
        this_thread::sleep_for(chrono::milliseconds(2 * TIMEOUT));
        return true;
    }



    ~UDPCLIENT() {
        if (Client_Socket != INVALID_SOCKET) {
            closesocket(Client_Socket);
            WSACleanup();
            cout << "[��־] �׽����ѹرգ���Դ���ͷ�..." << endl;
        }
    }
};

int main() {
    char ip_input[16];  // ���ڴ洢�û������IP��ַ
    UDPCLIENT sender;
    cout << "---------------------------------------------------" << endl;
    cout << "||                    ���Ͷ�-------->            ||" << endl;
    cout << "---------------------------------------------------" << endl;
    // ��ȡ�û����������
    cout << "�����뷢�Ͷ�IP��ַ: ";
    cin >> ip_input;
    CLIENT_IP = ip_input;  // ���û������IP��ַ��ֵ��ȫ�ֱ���
    //cout << "�����뷢�Ͷ˶˿�: ";
   // cin >> CLIENT_PORT;
    cout << "�����봰�ڴ�С: ";
    cin >> Windows_Size;
    // ������ģ���������
    double loss_rate;
    int delay;
    cout << "�����붪����(0.0-1.0): ";
    cin >> loss_rate;
    cout << "�������ӳ�ʱ��(ms): ";
    cin >> delay;
    cout << "---------------------------------------------------" << endl;
    sender.Set_SimulationParams(loss_rate, delay);

    if (!sender.INIT()) {
        cerr << "[ERROR] ���Ͷ˳�ʼ��ʧ�ܣ�" << endl;
        return 0;
    }

    if (!sender.ThreeWay_Handshake()) {
        cerr << "[ERROR] ��������ʧ�ܣ��޷��������ӣ�" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "---------------------------------------------------" << endl;
        cout << "Please choose an option��\n1. SEND FILE   2. EXIT\nSelect��";
        cin >> choice;


        if (choice == 1) {
            const string BASE_PATH = R"(D:\comput_network\project3-3\Project1\Project1\)";
            string filename;
            //file_path = R"(D:\comput_network\project3-3\Project1\Project1\1.jpg)";
            cout << "�������ļ���:";
            cin >> filename;
            string file_path = BASE_PATH + filename;
            cout << "---------------------------------------------------" << endl;
            if (!sender.Send_Message(file_path)) {
                cerr << "[ERROR] �ļ�����ʧ�ܣ�" << endl;
            }
        }
        else if (choice == 2) {
            if (!sender.Four_Wavehand()) {
                cerr << "[ERROR] �Ͽ�����ʧ�ܣ�" << endl;
            }
            else {
                cout << "[��־] �����ѳɹ��Ͽ���" << endl;
            }
        }
        else {
            cerr << "[WARNING] ��������!!!����������:" << endl;
        }
    } while (choice != 2);
    return 0;
}