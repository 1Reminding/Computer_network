#include "UDP.h" // 包含 UDP 的定义
#include <vector>  // 包含 vector 的头文件
#pragma comment(lib, "ws2_32.lib") // 链接 Windows Sockets 库

// 全局变量定义
//uint16_t SERVER_PORT;
const char* SERVER_IP = "127.0.0.1";  // 默认值

atomic<int> Header_Seq(0);//三次握手需要三个标志位

HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

atomic_int Sleep_Time(0);
float Last_Time = 0;

// 文件写入缓冲类
class File_Writetobuf {
private:

    vector<char> buf;
    size_t position_now;
    ofstream file;

public:// 构造函数，初始化文件名和缓冲区大小
    File_Writetobuf(const string& filename, size_t buffer_size)
        : buf(buffer_size), position_now(0) {// 初始化缓冲区和写入位置
        file.open(filename, ios::binary);// 打开文件，以二进制模式
    }
    // 写入数据到缓冲区
    void write(const char* data, size_t length) {
        while (length > 0) {
            size_t space = buf.size() - position_now;// 计算当前缓冲区剩余空间
            size_t to_write = min(space, length);// 选择要写入的字节数
            // 将数据拷贝到缓冲区
            memcpy(&buf[position_now], data, to_write);
            position_now += to_write; // 更新当前写入位置
            data += to_write;// 移动源数据指针
            length -= to_write;// 更新剩余写入字节数

            if (position_now == buf.size()) {// 如果缓冲区满了，刷新数据到文件
                flush();
            }
        }
    }
    // 刷新缓冲区中的数据到文件
    void flush() {
        if (position_now > 0) {
            file.write(buf.data(), position_now);
            position_now = 0;// 重置当前写入位置
        }
    }
    // 析构函数，确保在对象销毁时写入所有数据并关闭文件
    ~File_Writetobuf() {
        flush();// 刷新缓冲区中的数据
        file.close();
    }
};

class UDPSERVER {
private:
    SOCKET Server_Socket;           // 服务器端套接字
    sockaddr_in Server_Address;     // 服务器地址
    sockaddr_in Router_Address;     // 路由器地址
    socklen_t Router_Address_Len;   //路由地址长度
    uint32_t Seq;           // 当前序列号
    uint32_t File_Len;      //文件长度
    int Message_Num;         //消息数

public:
    UDPSERVER() : Server_Socket(INVALID_SOCKET), Seq(0), Router_Address_Len(sizeof(Router_Address)), File_Len(0), Message_Num(0) {}

    bool INIT() {
        // 初始化 Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[ERROR] WSAStartup 失败！错误代码: " << result << endl;
            return false;
        }

        // 检查版本是否匹配
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[ERROR] WinSock 版本不支持！" << endl;
            WSACleanup();
            return false;
        }

        // 创建 UDP 套接字
        Server_Socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (Server_Socket == INVALID_SOCKET) {
            cerr << "[ERROR] 套接字创建失败！错误代码: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[系统] 套接字创建成功！" << endl;

        // 设置非阻塞模式
        u_long mode = 1;
        if (ioctlsocket(Server_Socket, FIONBIO, &mode) != 0) {
            cerr << "[ERROR] 设置非阻塞模式失败！错误代码: " << WSAGetLastError() << endl;
            closesocket(Server_Socket);
            WSACleanup();
            return false;
        }

        cout << "[系统] 套接字设置为非阻塞模式" << endl;

        // 配置服务器地址
        memset(&Server_Address, 0, sizeof(Server_Address));
        Server_Address.sin_family = AF_INET;
        Server_Address.sin_port = htons(server_port);  // 使用固定的服务器端口
        inet_pton(AF_INET, SERVER_IP, &Server_Address.sin_addr);

        // 绑定地址到套接字
        if (bind(Server_Socket, (sockaddr*)&Server_Address, sizeof(Server_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] 套接字绑定失败！错误代码: " << WSAGetLastError() << endl;
            closesocket(Server_Socket);
            WSACleanup();
            return false;
        }

        cout << "[系统] 服务器套接字绑定到本地地址: 端口 " << server_port << endl;

        // 配置目标路由地址
        memset(&Router_Address, 0, sizeof(Router_Address));
        Router_Address.sin_family = AF_INET;
        Router_Address.sin_port = htons(router_port);
        inet_pton(AF_INET, router_ip, &Router_Address.sin_addr);

        return true;
    }

    bool ThreeWay_Handshake() {//三次握手建立连接

        UDP_PACKET handshakePackets[3]; // 三次握手消息
        socklen_t routerAddrLen = sizeof(Router_Address);// 路由器地址结构体长度

        memset(handshakePackets, 0, sizeof(handshakePackets)); // 清零消息结构体
        auto Start_Time = chrono::steady_clock::now(); // 记录开始时间，用于超时判断

        // 第一次握手：接收 SYN 消息
        while (true) {

            memset(&handshakePackets[0], 0, sizeof(handshakePackets[0])); // 清零第一次握手消息
            if (recvfrom(Server_Socket, (char*)&handshakePackets[0], sizeof(handshakePackets[0]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {// 接收客户端的 SYN
                if (handshakePackets[0].Is_SYN() && handshakePackets[0].Check_IsValid()) {// 检查是否是有效的 SYN
                    cout << "[日志] 第一次握手成功! 收到【SYN】 序列号：" << handshakePackets[0].seq << endl;
                    Seq = handshakePackets[0].seq;// 记录客户端发送的序列号

                    // 设置第二次握手的消息
                    handshakePackets[1].src_port = server_port;   // 服务器端的端口
                    handshakePackets[1].dest_port = router_port; // 路由器的端口
                    handshakePackets[1].seq = ++Seq; // 增加序列号
                    handshakePackets[1].ack = handshakePackets[0].seq;     // 确认客户端序列号
                    handshakePackets[1].Set_SYN();//设置标志
                    handshakePackets[1].Set_ACK();
                    // 发送第二次握手消息【SYN+ACK】
                    if (sendto(Server_Socket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                        (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[ERROR] 第二次握手消息发送失败!错误代码：" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[日志] 第二次握手：发送 【SYN+ACK】 序列号：" << handshakePackets[1].seq
                        << "，确认序列号：" << handshakePackets[1].ack << endl;
                    break;// 第一次握手成功，跳出循环
                }
                else {
                    cerr << "[WARNING] 丢弃无效【SYN】" << endl;// 丢弃无效的 SYN 消息
                }
            }
        }

        // 等待第三次握手消息
        Start_Time = chrono::steady_clock::now();// 记录开始时间，用于超时判断
        while (true) {
            // 接收第三次握手消息【ACK】
            if (recvfrom(Server_Socket, (char*)&handshakePackets[2], sizeof(handshakePackets[2]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {
                if (handshakePackets[2].Is_ACK() &&
                    handshakePackets[2].ack == handshakePackets[1].seq &&// 确认 ACK 序列号
                    handshakePackets[2].Check_IsValid()) {// 检查是否是有效的 ACK 消息
                    Seq = handshakePackets[2].seq;// 更新序列号
                    cout << "[日志] 第三次握手成功！收到 【ACK】 确认序列号：" << handshakePackets[2].ack << endl;
                    return true; // 连接建立成功，返回 true
                }
                else {
                    cerr << "[WARNING] 丢弃无效 【ACK】" << endl;
                }
            }

            // 超时处理：如果超过超时时间未收到第三次握手消息，重新发送第二次握手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - Start_Time).count() > TIMEOUT) {
                cout << "[日志] 等待第三次握手消息超时！重新发送【SYN+ACK】" << endl;

                // 重新计算校验和
                handshakePackets[1].check = handshakePackets[1].Calculate_Checksum();
                // 重新发送第二次握手消息【SYN+ACK】
                if (sendto(Server_Socket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                    (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
                    cerr << "[ERROR] 重传 【SYN+ACK】失败！错误代码：" << WSAGetLastError() << endl;
                    return false;
                }
                Start_Time = now;
            }
        }
    }
    // 文件大小格式化函数，返回适合的单位和格式化的字符串
    string FileSize_Form(uint64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = bytes;
        // 根据字节大小选择适当的单位
        while (size >= 1024 && unit_index < 4) {
            size /= 1024;
            unit_index++;
        }
        // 格式化输出文件大小
        stringstream ss;
        ss << fixed << setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }

    // 接收文件头部信息并确认，处理文件名和文件大小
    bool Recv_FileHead(char* file_name, UDP_PACKET& rec_msg, int& Waiting_Seq, socklen_t routerAddrLen) {
        // 记录开始时间用于超时检测
        auto start_time = chrono::steady_clock::now();

        while (true) {
            // 接收来自路由器的消息
            if (recvfrom(Server_Socket, (char*)&rec_msg, sizeof(rec_msg), 0,
                (SOCKADDR*)&Router_Address, &routerAddrLen) > 0) {

                // 判断是否为有效的文件头信息并且序列号与等待的序列号匹配
                if (rec_msg.Is_CFH() && rec_msg.Check_IsValid() && rec_msg.seq == Waiting_Seq) {
                    File_Len = rec_msg.length;
                    strcpy_s(file_name, MAX_LEN, rec_msg.data);// 将文件名存入文件名缓冲区

                    SetConsoleTextAttribute(hConsole, 14);
                    cout << "[Receive] "
                        << "\nFile_Name：" << file_name
                        << "\nFile_Size：" << FileSize_Form(File_Len) << endl;
                    SetConsoleTextAttribute(hConsole, 7);
                    cout << "---------------------------------------------------" << endl;
                    // 创建并发送确认包，确认收到文件头信息
                    UDP_PACKET ack_packet;
                    ack_packet.ack = rec_msg.seq;// 设置确认号为接收到的序列号
                    ack_packet.Set_ACK();// 设置ACK标志
                    ack_packet.check = ack_packet.Calculate_Checksum();// 计算校验和

                    // 发送确认包
                    if (sendto(Server_Socket, (char*)&ack_packet, sizeof(ack_packet), 0,
                        (SOCKADDR*)&Router_Address, routerAddrLen) > 0) {
                        Waiting_Seq++;// 增加等待的序列号，准备接收下一个数据包
                        return true;
                    }
                }
                // 如果文件头无效但仍为CFH消息，则发送重复ACK
                else if (rec_msg.Is_CFH() && rec_msg.Check_IsValid()) {
                    Send_DuplicateACK(Waiting_Seq - 1);// 发送重复确认包
                }
            }

            // 超时检查，如果超过设定的超时时间，重新发送重复ACK请求重传
            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > TIMEOUT) {
                SetConsoleTextAttribute(hConsole, 12);
                cout << "[Timeout] 等待文件头超时!请求重传" << endl;
                SetConsoleTextAttribute(hConsole, 7);
                Send_DuplicateACK(Waiting_Seq - 1);
                start_time = chrono::steady_clock::now();// 重新记录开始时间
            }
        }
    }
    // 发送重复ACK包，用于通知接收方需要重传数据
    void Send_DuplicateACK(int seq) {
        // 创建一个确认包（ACK）
        UDP_PACKET ack_packet;

        // 设置确认号为传入的序列号
        ack_packet.ack = seq;

        // 设置ACK标志，表示这是一个确认包
        ack_packet.Set_ACK();
        ack_packet.check = ack_packet.Calculate_Checksum();

        // 发送ACK包到路由器
        if (sendto(Server_Socket, (char*)&ack_packet, sizeof(ack_packet), 0,
            (SOCKADDR*)&Router_Address, sizeof(Router_Address)) > 0) {
            // 如果发送成功，打印发送的ACK序列号，提示重传操作
            SetConsoleTextAttribute(hConsole, 12);
            cout << "[重传] 发送重复ACK 序列号：" << seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
        }
    }

    enum class Recv_Result {
        Success,
        Timeout,
        Error
    };

    // 带超时的接收数据包函数，接收一个UDP数据包并在超时之前返回接收结果
    Recv_Result Recv_TimeoutPacket(UDP_PACKET& packet, socklen_t routerAddrLen, int timeout_ms) {
        auto start_time = chrono::steady_clock::now();

        // 无限循环，尝试接收数据包
        while (true) {
            // 尝试接收数据包，如果成功接收到数据
            if (recvfrom(Server_Socket, (char*)&packet, sizeof(packet), 0,
                (SOCKADDR*)&Router_Address, &routerAddrLen) > 0) {
                // 如果数据包接收成功，返回成功状态
                return Recv_Result::Success;
            }
            // 检查是否超时，如果当前时间减去开始时间超过了指定的超时时间
            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > timeout_ms) {
                // 如果超时，返回超时状态
                return Recv_Result::Timeout;
            }
        }
    }

    // 处理接收到的数据包，检查是否为期望的序号，并更新接收状态
    bool Handle_ReceivedPacket(const UDP_PACKET& packet, int& Waiting_Seq,
        File_Writetobuf& writer, uint64_t& total_received) {

        // 打印接收到的数据包信息，输出序号和标志位等
        SetConsoleTextAttribute(hConsole, 11); // 浅蓝色
        cout << "[接收] 序列号: " << packet.seq << " (预期序列号: " << Waiting_Seq << ")";
        // 检查是否为乱序到达
        if (packet.seq != Waiting_Seq) {
            SetConsoleTextAttribute(hConsole, 12); // 红色
            cout << " [乱序到达]";
        }
        // 打印标志位
        cout << " 标志:ACK ";
        if (packet.Is_ACK()) cout << "ACK ";// 如果是 ACK 标志，输出 ACK，以此类推
        if (packet.Is_SYN()) cout << "SYN ";
        if (packet.Is_FIN()) cout << "FIN ";
        if (packet.Is_CFH()) cout << "CFH ";
        cout << "校验和: 0x" << hex << packet.check << dec;
        if (!packet.Check_IsValid()) cout << " (校验失败)";
        cout << endl;
        SetConsoleTextAttribute(hConsole, 7);

        // 检查数据包是否有效且序号正确
        if (packet.Check_IsValid() && packet.seq == Waiting_Seq) {
            // 发送确认 ACK 包
            UDP_PACKET ack_packet;
            ack_packet.ack = packet.seq; // 设置确认序号为接收到的序号
            ack_packet.Set_ACK();// 设置 ACK 标志
            ack_packet.check = ack_packet.Calculate_Checksum();// 计算校验和

            // 发送确认包
            if (sendto(Server_Socket, (char*)&ack_packet, sizeof(ack_packet), 0,
                (SOCKADDR*)&Router_Address, sizeof(Router_Address)) > 0) {

                // 写入接收到的数据
                writer.write(packet.data, packet.length);// 将数据写入缓冲区
                total_received += packet.length;// 更新已接收的字节数
                Waiting_Seq++; // 更新等待的下一个序列号
                return true;// 返回成功，表示处理完当前数据包
            }
        }
        // 如果序号不对，但校验成功，发送重复确认 ACK
        else if (packet.Check_IsValid()) {
            Send_DuplicateACK(Waiting_Seq - 1);// 发送上一个序列号的重复 ACK
        }
        return false;// 返回失败，表示数据包未处理
    }
    // 保存当前接收到的字节数，用于断点恢复
    void Save_CheckPoint(const string& file_path, uint64_t bytes_received) {
        string checkpoint_path = file_path + ".checkpoint";
        // 打开 checkpoint 文件进行写入
        ofstream checkpoint(checkpoint_path);
        if (checkpoint.is_open()) {
            // 将已接收的字节数写入文件
            checkpoint << bytes_received;
            checkpoint.close();
        }
    }

    // 打印接收进度信息，包括接收的字节数、总字节数、速度等
    void Print_RecvProgress(uint64_t received, uint64_t total,
        chrono::steady_clock::time_point start_time) {

        static int last_percentage = 0;  // 记录上次打印的百分比
        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        double speed = received / elapsed / 1024; // 计算接收速度，单位为KB/s
        int percentage = static_cast<int>(received * 100.0 / total);

        // 每隔5%打印一次进度，或者当接收完成时打印100%
        if (percentage >= last_percentage + 5 || percentage == 100) {
            cout << "[接收进度] " << "完成度："<<percentage << "% "
                <<"已接收：" << FileSize_Form(received) << "/" << FileSize_Form(total)// 显示已接收和总文件大小
                <<"速率：" << " (" << fixed << setprecision(2) << speed << " KB/s)" << endl;// 显示接收速度
            last_percentage = percentage;
        }
    }

    void Print_RecvStatus(chrono::steady_clock::time_point start_time,
        uint64_t total_bytes, const string& file_path) {

        auto end_time = chrono::steady_clock::now();
        double duration = chrono::duration<double>(end_time - start_time).count();
        double speed = (total_bytes / 1024.0) / duration; // KB/s

        SetConsoleTextAttribute(hConsole, 7);
        cout << "\n[Finish] 文件接收完成"
            << "\n保存位置：" << file_path
            << "\n文件大小：" << FileSize_Form(total_bytes)
            << "\n总耗时：" << fixed << setprecision(2) << duration << " 秒"
            << "\n平均吞吐率：" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    // 接收文件的消息处理函数，包括接收文件头、数据包和断点续传功能
    bool Recv_Message(const string& outputDir) {
        Header_Seq = Seq;
        char file_name[MAX_LEN] = {};
        UDP_PACKET Message;
        int Waiting_Seq = Header_Seq + 1;// 初始化等待的序列号,等待的下一位是标志位之后一位
        socklen_t routerAddrLen = sizeof(Router_Address);
        uint64_t total_received_bytes = 0;

        // 接收文件头
        if (!Recv_FileHead(file_name, Message, Waiting_Seq, routerAddrLen)) {
            return false;
        }

        // 准备文件写入
        string filePath = outputDir + "/" + string(file_name);
        File_Writetobuf fileWriter(filePath, 1024 * 1024); // 创建文件写入缓冲区，1MB 缓冲区

        auto start_time = chrono::steady_clock::now();//开始时间
        auto last_progress_update = start_time;//上次进度更新的时间

        cout << "\n[接收开始] 开始接收文件数据...\n" << endl;

        // 主接收循环，直到接收的字节数达到文件大小
        while (total_received_bytes < File_Len) {
            UDP_PACKET packet;
            // 调用接收带超时的函数来接收数据包
            auto receive_result = Recv_TimeoutPacket(packet, routerAddrLen, TIMEOUT);

            if (Handle_ReceivedPacket(packet, Waiting_Seq, fileWriter, total_received_bytes)) {
                // 更新进度显示
                auto current_time = chrono::steady_clock::now();
                if (chrono::duration_cast<chrono::milliseconds>(current_time - last_progress_update).count() >= 100) {
                    Print_RecvProgress(total_received_bytes, File_Len, start_time);
                    last_progress_update = current_time;// 更新上次进度更新的时间
                }
            }

            // 检查是否需要保存断点续传信息，// 每接收5MB的数据时，保存一次断点信息
            if (total_received_bytes % (5 * 1024 * 1024) == 0) { // 每5MB保存一次
                Save_CheckPoint(filePath, total_received_bytes); // 保存断点
            }
        }

        // 完成接收，输出统计信息
        fileWriter.flush();
        Print_RecvStatus(start_time, total_received_bytes, filePath);

        Seq = Waiting_Seq - 1;// 更新序列号，在等待序号前一位
        return true;
    }

    // 四次挥手函数，模拟连接关闭的四次挥手过程
    bool FourWay_Wavehand() {
        UDP_PACKET Is_FIN[4];        // 四次挥手消息
        socklen_t routerAddrLen = sizeof(Router_Address);// 路由器地址结构体长度
        auto startTime = chrono::steady_clock::now();

        // 初始化挥手消息
        memset(Is_FIN, 0, sizeof(Is_FIN)); // 清零消息结构体数组

        // 第一次挥手: 接收 FIN 消息
        while (true) {
            if (recvfrom(Server_Socket, (char*)&Is_FIN[0], sizeof(Is_FIN[0]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {// 接收来自客户端的 FIN 消息
                if (Is_FIN[0].Is_FIN() && Is_FIN[0].Check_IsValid()) {// 检查是否为有效的 FIN 消息
                    cout << "[日志] 收到第一次挥手消息【FIN】 序列号：" << Is_FIN[0].seq << endl;
                    break;// 接收成功，退出循环
                }
                else {
                    cerr << "[WARNING] 丢弃无效【FIN】" << endl;
                }
            }
        }
        Seq = Is_FIN[0].seq;// 更新序列号


        // 第二次挥手: 发送 ACK 消息
        memset(&Is_FIN[2], 0, sizeof(Is_FIN[1]));// 清零第二次挥手消息结构体
        Is_FIN[1].src_port = server_port;// 设置源端口为服务器端口
        Is_FIN[1].dest_port = router_port;
        Is_FIN[1].Set_ACK();
        Is_FIN[1].ack = Is_FIN[0].seq;// 确认客户端的 FIN 消息序列号
        Is_FIN[1].seq = ++Seq;
        Is_FIN[1].check = Is_FIN[1].Calculate_Checksum();

        // 发送 ACK 消息
        if (sendto(Server_Socket, (char*)&Is_FIN[1], sizeof(Is_FIN[1]), 0,
            (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[ERROR] 第二次挥手消息发送失败！错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[日志] 第二次挥手：发送【ACK】 确认序列号：" << Is_FIN[1].ack << endl;
        Seq = Is_FIN[1].seq;

        // 第三次挥手: 发送 FIN 消息
        memset(&Is_FIN[2], 0, sizeof(Is_FIN[2]));// 清零第三次挥手消息结构体
        Is_FIN[2].src_port = server_port;
        Is_FIN[2].dest_port = router_port;
        Is_FIN[2].seq = ++Seq;
        Is_FIN[2].ack = Is_FIN[1].seq; // 确认第二次挥手消息的序列号
        Is_FIN[2].Set_FIN();
        Is_FIN[2].Set_ACK();
        Is_FIN[2].check = Is_FIN[2].Calculate_Checksum();
        startTime = chrono::steady_clock::now();
        cout << "[日志] 第三次挥手：发送 【FIN】 序列号：" << Is_FIN[2].seq << endl;

        // 发送 FIN 消息
        if (sendto(Server_Socket, (char*)&Is_FIN[2], sizeof(Is_FIN[2]), 0,
            (sockaddr*)&Router_Address, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[ERROR] 第三次挥手消息发送失败！错误代码：" << WSAGetLastError() << endl;
            return false;
        }

        while (true) {
            // 第四次挥手: 接收 ACK 消息
            if (recvfrom(Server_Socket, (char*)&Is_FIN[3], sizeof(Is_FIN[3]), 0,
                (sockaddr*)&Router_Address, &routerAddrLen) > 0) {// 接收来自客户端的 ACK 消息
                if (Is_FIN[3].Is_ACK() &&
                    Is_FIN[3].ack == Is_FIN[2].seq &&// 确认第三次挥手消息的序列号
                    Is_FIN[3].Check_IsValid()) {
                    cout << "[日志] 收到第四次挥手消息 【ACK】 确认序列号：" << Is_FIN[3].ack << endl;
                    break;
                }
                else {
                    cerr << "[WARNING] 丢弃无效 【ACK】 消息" << endl;
                }
            }

            // 超时处理: 如果超时未接收到第四次挥手消息，重传第三次挥手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - startTime).count() > TIMEOUT) {
                cout << "[日志] 【FIN】 超时！重新发送" << endl;
                Is_FIN[2].check = Is_FIN[2].Calculate_Checksum(); // 重算校验和
                if (sendto(Server_Socket, (char*)&Is_FIN[2], sizeof(Is_FIN[2]), 0,
                    (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
                    cerr << "[ERROR] 重传失败。" << endl;
                    return false;
                }
                startTime = now;//重置
            }
        }

        return true;
    }


    ~UDPSERVER() {
        if (Server_Socket != INVALID_SOCKET) {
            closesocket(Server_Socket);
            WSACleanup();
            cout << "[日志] 套接字已关闭 资源已释放" << endl;
        }
    }
};


int main() {
    char ip_input[16];  // 用于存储用户输入的IP地址
    cout << "---------------------------------------------------" << endl;
    cout << "||             ------->接收端                    ||" << endl;
    cout << "---------------------------------------------------" << endl;
    // 获取用户输入的配置
    cout << "请输入接收端IP地址: ";
    cin >> ip_input;
    SERVER_IP = ip_input;  // 将用户输入的IP地址赋值给全局变量
   // cout << "请输入接收端端口: ";
   // cin >> SERVER_PORT;
    
    UDPSERVER receiver;
    if (!receiver.INIT()) {
        cerr << "[ERROR] 接收端初始化失败！" << endl;
        return 0;
    }

    cout << "[日志] 初始化完毕，等待发送端连接..." << endl;

    if (!receiver.ThreeWay_Handshake()) {
        cerr << "[ERROR] 三次握手失败！无法建立连接！" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "---------------------------------------------------" << endl;
        cout << "Please choose an option：\n1. SAVE FILE   2. EXIT\nSelect：";
        cin >> choice;

        if (choice == 1) {
            string output_dir;
           // cout << "目标接收目录：";
            //cin >> output_dir;
            output_dir = R"(D:\comput_network\project3-3\Project2\Project2)";
            cout << "---------------------------------------------------" << endl;
            cout << "[日志] 正在等待发送端发送文件..." << endl;
            if (!receiver.Recv_Message(output_dir)) {
                cerr << "[ERROR] 文件接收失败！" << endl;
            }
        }
        else if (choice == 2) {
            cout << "[日志] 等待发送端进行操作..." << endl;
            if (!receiver.FourWay_Wavehand()) {
                cerr << "[ERROR] 断开连接失败！" << endl;
            }
            else {
                cout << "[日志] 断开连接成功！" << endl;
            }
        }
        else {
            cerr << "[WARNING] 输入有误！！！请重新输入：" << endl;
        }
    } while (choice != 2);

    return 0;
}