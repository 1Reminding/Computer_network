#include "UDP.h" // 包含 UDP 的定义
#pragma comment(lib, "ws2_32.lib") // 链接 Windows Sockets 库

// 全局变量定义
//uint16_t CLIENT_PORT;
const char* CLIENT_IP = "127.0.0.1";  // 默认值
int Windows_Size;  // 定义全局变量窗口大小

//多线程变量定义
atomic_int Base_Seq(1);//基指针
atomic_int Next_Seq(1);//未确认
atomic_int Head_Seq(0);//三次握手需要三个标志位
atomic_int Count(0);

mutex mtx;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

atomic_bool Resend(false);
atomic_bool Completed(false);

class UDPCLIENT {
private:
    SOCKET Client_Socket;           // 客户端套接字
    sockaddr_in Client_Address;        // 客户端地址
    sockaddr_in Router_Address;        // 目标路由地址
    uint32_t Seq;                  // 客户端当前序列号
    uint32_t file_length;
    socklen_t addr_len = sizeof(Router_Address);

    int Message_Num;                   //发送消息总数

    // 新增：模拟参数
    double packet_loss_rate;    // 丢包率 (0.0 - 1.0)
    int delay_ms;              // 延迟时间(ms)

public:
    UDPCLIENT() : Client_Socket(INVALID_SOCKET), Seq(0), Message_Num(0),
        addr_len(sizeof(Router_Address)),
        packet_loss_rate(0.01),    // 默认10%丢包率,可以输入设置
        delay_ms(2)             // 默认100ms延迟，可以输入设置
    {}
    // 添加设置模拟参数的方法
    void Set_SimulationParams(double loss_rate, int delay) {
        packet_loss_rate = loss_rate;
        delay_ms = delay;
    }

    bool INIT() {
        // 初始化
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[ERROR] WSAStartup 失败！错误代码: " << result << endl;
            return false;
        }
        // 版本匹配
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[ERROR] WinSock 版本不支持！" << endl;
            WSACleanup();
            return false;
        }
        // 创建 UDP 套接字
        Client_Socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (Client_Socket == INVALID_SOCKET) {
            cerr << "[ERROR] 套接字创建失败!错误代码: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[系统] 套接字创建成功!" << endl;
        // 设置非阻塞模式
        u_long mode = 1;
        if (ioctlsocket(Client_Socket, FIONBIO, &mode) != 0) {
            cerr << "[ERROR] 设置非阻塞模式失败! 错误代码: " << WSAGetLastError() << endl;
            closesocket(Client_Socket);
            WSACleanup();
            return false;
        }
        cout << "[系统] 套接字设置为非阻塞模式" << endl;
        // 配置客户端地址
        memset(&Client_Address, 0, sizeof(Client_Address));
        Client_Address.sin_family = AF_INET;
        Client_Address.sin_port = htons(client_port);
        inet_pton(AF_INET, CLIENT_IP, &Client_Address.sin_addr);
        // 绑定客户端地址到套接字
        if (bind(Client_Socket, (sockaddr*)&Client_Address, sizeof(Client_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] 套接字绑定失败!错误代码: " << WSAGetLastError() << endl;
            closesocket(Client_Socket);
            WSACleanup();
            return false;
        }
        cout << "[系统] 套接字绑定到本地地址: 端口 " << client_port << endl;
        // 配置目标路由地址
        memset(&Router_Address, 0, sizeof(Router_Address));
        Router_Address.sin_family = AF_INET;
        Router_Address.sin_port = htons(router_port);
        inet_pton(AF_INET, router_ip, &Router_Address.sin_addr);

        return true;
    }

    bool ThreeWay_Handshake() {
        UDP_PACKET con_msg[3];  // 三次握手消息，包含 SYN、SYN+ACK、ACK 消息

        // 第一次握手
        con_msg[0] = {}; // 清空结构体，确保没有脏数据
        con_msg[0].src_port = client_port;// 设置源端口（客户端端口）
        con_msg[0].dest_port = router_port;
        con_msg[0].Set_SYN();                  // 设置 SYN 标志位，表示请求建立连接
        con_msg[0].seq = ++Seq;                // 设置序列号，序列号递增
        con_msg[0].check = con_msg[0].Calculate_Checksum(); // 计算校验和
        auto msg1_Send_Time = chrono::steady_clock::now(); // 记录第一次握手发送的时间，用于超时重传

        // 发送第一次握手消息（SYN）
        cout << "[日志] 第一次握手：发送 【SYN】..." << endl;
        if (sendto(Client_Socket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] 第一次握手消息发送失败！" << endl;
            return false;
        }

        // 第二次握手
        socklen_t addr_len = sizeof(Router_Address);// 存储路由器地址的长度
        while (true) {
            // 接收 SYN+ACK 消息
            if (recvfrom(Client_Socket, (char*)&con_msg[1], sizeof(con_msg[1]), 0,
                (sockaddr*)&Router_Address, &addr_len) > 0) {
                // 验证收到的消息是否是 SYN+ACK 且有效
                if (con_msg[1].Is_ACK() && con_msg[1].Is_SYN() && con_msg[1].Check_IsValid() &&
                    con_msg[1].ack == con_msg[0].seq) {
                    cout << "[日志] 第二次握手成功！收到 【SYN+ACK】" << endl;
                    break;// 握手成功，跳出循环
                }
                else {
                    cerr << "[ERROR] 第二次握手消息验证失败！" << endl;
                }
            }

            // 如果第二次握手消息超时，重新发送第一次握手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - msg1_Send_Time).count() > TIMEOUT) {
                cout << "[日志] 超时！重传第一次握手消息！" << endl;
                con_msg[0].check = con_msg[0].Calculate_Checksum(); // 重新计算第一次握手消息的校验和
                if (sendto(Client_Socket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
                    (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
                    cerr << "[ERROR] 重传失败！" << endl;
                    return false;
                }
                msg1_Send_Time = now; // 更新发送时间
            }
        }
        // 设置当前序列号为接收到的 SYN+ACK 消息的序列号
        Seq = con_msg[1].seq;
        // 第三次握手
        con_msg[2] = {}; // 清空结构体
        con_msg[2].src_port = client_port;
        con_msg[2].dest_port = router_port;
        con_msg[2].seq = ++Seq;           // 设置序列号
        con_msg[2].ack = con_msg[1].seq;  // 设置确认号，确认收到第二次握手的序列号
        con_msg[2].Set_ACK();             // 设置 ACK 标志位，表示确认收到 SYN+ACK 消息
        con_msg[2].check = con_msg[2].Calculate_Checksum(); // 计算校验和
        // 发送第三次握手消息（ACK）
        cout << "[日志] 第三次握手：发送 【ACK】..." << endl;
        if (sendto(Client_Socket, (char*)&con_msg[2], sizeof(con_msg[2]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] 第三次握手消息发送失败！" << endl;
            return false;
        }
        cout << "[日志] 三次握手完成！连接建立成功！" << endl;
        return true; // 三次握手成功，返回 true
    }

    void Reset() {
        Completed = false;
        Resend = false;

        Next_Seq = 1;
        Base_Seq = 1;

        Head_Seq = 0;
        Message_Num = 0;
    }

    // ACK 处理与快速重传机制
    void Fast_Ack() {//ACK处理线程
        // 用于快速重传机制的计数器和阈值
        int Err_ack_Num = 0;  // 上次收到的 ACK 序列号
        int resend_threshold = 3;  // 设定重复确认的重发阈值，三次重复触发
        int resend_counter = 0;  // 用于统计连续的相同 ACK

        while (true) {
            UDP_PACKET ack_msg;

            // 接收ACK消息
            if (recvfrom(Client_Socket, (char*)&ack_msg, sizeof(ack_msg), 0, (SOCKADDR*)&Router_Address, &addr_len)) {
                // 确保接收到的包是有效的 ACK
                if (ack_msg.Is_ACK() && ack_msg.Check_IsValid()) {
                    lock_guard<mutex> lock(mtx);  // 加锁保护共享变量
                    SetConsoleTextAttribute(hConsole, 10); // 绿色
                    cout << "[校验] 收到ACK校验和: 0x" << hex << ack_msg.check
                        << dec << " (确认号: " << ack_msg.ack << ")" << endl;
                    SetConsoleTextAttribute(hConsole, 7);
                    cout << "[日志] 接收到确认消息，ACK 序列号： " << ack_msg.ack << endl;

                    // 累积确认：更新窗口基序号
                // Base_Seq需要减去Header_Seq才是真实的数据包序号
                    if (ack_msg.ack >= Base_Seq + Head_Seq) {
                        Base_Seq = ack_msg.ack - Head_Seq + 1;//序列从零开始
                    }
                    Print_WindowStatus();
                    // 检查是否传输完成：当收到的ACK等于最后一个数据包的序号
                    if (ack_msg.ack - Head_Seq == Message_Num + 1) {
                        Completed = true;// 设置完成标志
                        return;  // 完成传输，退出线程
                    }

                    // 错误 ACK 重发控制
                    if (Err_ack_Num != ack_msg.ack) {
                        Err_ack_Num = ack_msg.ack;
                        resend_counter = 0;  // 重设计数器
                    }
                    else {
                        resend_counter++;
                        if (resend_counter >= resend_threshold) {
                            Resend = true;  // 达到重发阈值，设置重发标志
                            resend_counter = 0;  // 重设计数器
                        }
                    }
                }
            }
        }
    }

    // 发送文件头信息包
    bool Send_FileHead(UDP_PACKET* data_msg, const string& file_name) {
        // 将文件名复制到数据包的data字段中
        strcpy_s(data_msg[0].data, file_name.c_str());
        // 确保文件名末尾以 '\0' 结尾
        data_msg[0].data[strlen(data_msg[0].data)] = '\0';
        data_msg[0].length = file_length;
        data_msg[0].seq = ++Seq;// 设置序列号，确保每个包的序列号唯一
        // 设置文件头标志
        data_msg[0].Set_CFH();
        data_msg[0].src_port = client_port;
        data_msg[0].dest_port = router_port;
        data_msg[0].check = data_msg[0].Calculate_Checksum();

        SetConsoleTextAttribute(hConsole, 11); // 浅蓝色
        cout << "[发送] 文件头信息包：" << file_name
            << " (序列号: " << data_msg[0].seq << ")" << endl;
        SetConsoleTextAttribute(hConsole, 7);
        // 发送文件头信息包
        if (sendto(Client_Socket, (char*)&data_msg[0], sizeof(data_msg[0]), 0,
            (SOCKADDR*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            // 如果发送失败，输出错误日志并返回 false
            SetConsoleTextAttribute(hConsole, 12); // 红色
            cerr << "[ERROR] 文件头发送失败！错误码：" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        return true;
    }

    // 发送文件数据包
    bool Send_FileData(UDP_PACKET* data_msg, ifstream& file, int next_seq, int last_length) {
        // 根据是否为最后一个数据包来读取数据
        if (next_seq == Message_Num && last_length) {
            // 如果是最后一个数据包，读取剩余的文件数据
            file.read(data_msg[next_seq - 1].data, last_length);
            data_msg[next_seq - 1].length = last_length;// 设置数据包长度为剩余长度
        }
        else {
            // 否则读取固定大小的数据块
            file.read(data_msg[next_seq - 1].data, MAX_LEN);
            data_msg[next_seq - 1].length = MAX_LEN;// 设置数据包长度为最大值
        }

        // 设置数据包的序列号、端口号和校验和等属性
        data_msg[next_seq - 1].seq = ++Seq;
        data_msg[next_seq - 1].src_port = client_port;
        data_msg[next_seq - 1].dest_port = router_port;
        data_msg[next_seq - 1].check = data_msg[next_seq - 1].Calculate_Checksum();

        SetConsoleTextAttribute(hConsole, 14); // 黄色
        cout << "[校验] 发送数据包校验和: 0x" << hex << data_msg[next_seq - 1].check
            << dec << " (序列号: " << data_msg[next_seq - 1].seq << ")" << endl;
        SetConsoleTextAttribute(hConsole, 7);

        // 模拟数据包丢失（根据丢包率随机丢包）
        if ((double)rand() / RAND_MAX < packet_loss_rate) {
            SetConsoleTextAttribute(hConsole, 12); // 红色
            cout << "[模拟] 数据包丢失，序列号：" << data_msg[next_seq - 1].seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return true;  // 丢包后返回true，继续传输
        }

        // 模拟数据包延迟（如果设置了延迟时间）
        if (delay_ms > 0) {
            SetConsoleTextAttribute(hConsole, 14); // 黄色
            cout << "[模拟] 数据包延迟 " << delay_ms << "ms，序列号：" << data_msg[next_seq - 1].seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
            this_thread::sleep_for(chrono::milliseconds(delay_ms));// 模拟延迟
        }
        // 发送数据包到路由器
        if (sendto(Client_Socket, (char*)&data_msg[next_seq - 1], sizeof(data_msg[next_seq - 1]), 0,
            (SOCKADDR*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            // 如果发送失败，输出错误信息并返回 false
            SetConsoleTextAttribute(hConsole, 12);
            cerr << "[ERROR] 数据包发送失败，序列号：" << data_msg[next_seq - 1].seq
                << "，错误码：" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        // 数据包成功发送，输出成功日志
        cout << "[日志] 成功发送数据包，序列号： " << data_msg[next_seq - 1].seq << endl;
        return true;
    }

    // 重传未确认的数据包
    void Resend_Data(UDP_PACKET* data_msg) {
        SetConsoleTextAttribute(hConsole, 12);
        cout << "\n[重传] 开始重传未确认的数据包..." << endl;
        SetConsoleTextAttribute(hConsole, 7);

        // 遍历当前窗口内的所有未确认的数据包，进行重传
        for (int i = 0; i < Next_Seq - Base_Seq; i++) {
            lock_guard<mutex> lock(mtx);// 使用互斥锁，确保线程安全
            int resend_seq = Base_Seq + i - 1; // 计算需要重传的数据包序列号
            // 重新计算数据包的校验和
            data_msg[resend_seq].check = data_msg[resend_seq].Calculate_Checksum();
            // 发送重传的数据包
            if (sendto(Client_Socket, (char*)&data_msg[resend_seq], sizeof(data_msg[resend_seq]), 0,
                (SOCKADDR*)&Router_Address, sizeof(Router_Address)) != SOCKET_ERROR) {

                SetConsoleTextAttribute(hConsole, 14);
                cout << "[重传] 数据包重传成功！序列号：" << resend_seq + Head_Seq + 1 << endl;
                SetConsoleTextAttribute(hConsole, 7);
            }
        }
        Resend = false;// 重传完成，更新标志
    }
    // 格式化文件大小为可读形式（B, KB, MB, GB, TB）
    string FileSize_Form(uint64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = bytes; // 将字节数转为 double 类型，以便处理更大的文件
        // 将字节数转换为适合的单位（B -> KB -> MB -> GB -> TB）
        while (size >= 1024 && unit_index < 4) {
            size /= 1024;
            unit_index++;
        }
        // 使用 stringstream 格式化输出，保留两位小数
        stringstream ss;
        ss << fixed << setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }
    // 打印窗口的状态，包括 Base 和 Next 序列号及其他相关信息
    void Print_WindowStatus() {
        static int last_base = -1; // 上次打印的 Base_Seq
        static int last_next = -1;
        // 如果 Base_Seq 或 Next_Seq 发生变化，更新窗口状态
        if (last_base != Base_Seq || last_next != Next_Seq) {
            SetConsoleTextAttribute(hConsole, 11);
            cout << "[窗口] Base: " << Base_Seq
                << " Next: " << Next_Seq
                << " 未确认: " << Next_Seq - Base_Seq
                << " 窗口剩余空间: " << Windows_Size - (Next_Seq - Base_Seq)
                << "[位置] 【窗口起始位置: " << Base_Seq
                << ", 窗口结束位置: " << Base_Seq + Windows_Size - 1<<"】"
                << endl;
            SetConsoleTextAttribute(hConsole, 7);
            last_base = Base_Seq;
            last_next = Next_Seq;
        }
    }
    // 打印接收进度，包括已接收的数据、总数据、传输速度
    void Print_RecvProgress(uint64_t transferred, uint64_t total, chrono::steady_clock::time_point start_time) {
        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        double speed = (transferred - total) / elapsed / 1024; // KB/s
        int percentage = (int)((transferred - total) * 100.0 / total);// 计算接收进度的百分比

        // 进度条宽度//const int bar_width = 50;//int filled = bar_width * percentage / 100;

        cout << "\r[进度] 【";
        cout << percentage << "%】"
            << FileSize_Form(transferred - total) << "/" << FileSize_Form(total) // 显示已接收和总数据量（格式化）
            << " (" << fixed << setprecision(2) << speed << " KB/s)    " << flush; // 显示接收速度
        cout << endl;
    }
    // 打印传输状态，包括传输的总字节数、传输耗时和平均吞吐率
    void Print_TransStatus(chrono::steady_clock::time_point start_time, uint32_t total_bytes) {
        auto end_time = chrono::steady_clock::now();
        double duration = chrono::duration<double>(end_time - start_time).count();
        double speed = (total_bytes / 1024.0) / duration; // KB/s

        SetConsoleTextAttribute(hConsole, 7); // 绿色
        cout << "\n[FINISH] 文件传输完成！"
            << "\n文件大小：" << FileSize_Form(total_bytes)
            << "\n传输耗时：" << fixed << setprecision(2) << duration << " 秒"
            << "\n平均吞吐率：" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }
    // 发送文件信息的主函数
    bool Send_Message(string file_path) {
        // 打开文件并错误处理
        ifstream file(file_path, ios::binary);// 以二进制模式打开文件
        if (!file.is_open()) {
            cerr << "[ERROR] 无法打开文件：" << file_path << "\n错误原因：" << endl;
            return false;
        }

        // 获取文件信息（文件名和文件大小）
        size_t pos = file_path.find_last_of("/\\");
        string file_name = (pos != string::npos) ? file_path.substr(pos + 1) : file_path;
        file.seekg(0, ios::end);
        file_length = file.tellg(); //cout << file_length;
        file.seekg(0, ios::beg);

        SetConsoleTextAttribute(hConsole, 14); // 黄色
        cout << "\n[Send] File_name：" << file_name
            << "\nFile_size：" << FileSize_Form(file_length) << endl;
        cout << "File_length:" << file_length << endl;
        SetConsoleTextAttribute(hConsole, 7);  // 恢复默认色
        cout << "---------------------------------------------------" << endl;

        // 计算文件分块信息
        int complete_num = file_length / MAX_LEN; // 计算完整数据块数
        int last_length = file_length % MAX_LEN; // 计算最后一个数据块的剩余长度
        Head_Seq = Seq;// 设置文件头的序列号
        Message_Num = complete_num + (last_length != 0); // 总消息数（包括最后一个不完整的数据块）

        // 创建接收确认线程，独立处理ack
        thread ackThread([this]() {
            this->Fast_Ack();
            });
        // 创建数据包数组,数组从0开始，数据包从1开始
        unique_ptr<UDP_PACKET[]> data_msg(new UDP_PACKET[Message_Num + 1]);
        auto start_time = chrono::steady_clock::now();
        uint64_t total_sent_bytes = 0;

        cout << "\n[传输开始] 初始化完成!开始传输...\n" << endl;

        // 主传输循环，直到传输完成
        while (!Completed) {
            // 重传处理，如果需要重传数据包
            if (Resend) {
                Resend_Data(data_msg.get());// 重传未确认的数据包
                continue;// 跳过当前循环，继续重传
            }

            // 正常发送处理
            if (Next_Seq < Base_Seq + Windows_Size && Next_Seq <= Message_Num + 1) {
                lock_guard<mutex> lock(mtx);// 加锁，保证线程安全

                if (Next_Seq == 1) {// 如果是第一个包，发送文件头
                    // 发送文件头,三次握手ack
                    if (!Send_FileHead(data_msg.get(), file_name)) {
                        return false;
                    }
                }
                else {
                    // 发送文件数据
                    if (!Send_FileData(data_msg.get(), file, Next_Seq, last_length)) {
                        return false;
                    }
                }
                Print_WindowStatus();
                // 更新传输速度，后续绘图
                total_sent_bytes += data_msg[Next_Seq - 1].length;
                Print_RecvProgress(total_sent_bytes, file_length, start_time);

                Next_Seq++;

            }

            //流量控制 - 当窗口接近满时延迟发送
            if (Next_Seq - Base_Seq > Windows_Size * 0.8) {
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }
        // 等待确认线程结束
        ackThread.join();

        // 打印传输统计信息
        Print_TransStatus(start_time, file_length);
        // 重置状态
        Reset();
        file.close();
        return true;
    }
    // 四次挥手协议的实现
    bool Four_Wavehand() {
        UDP_PACKET wavehand_packets[4]; // 定义四次挥手消息数组
        socklen_t addr_len = sizeof(Router_Address);
        auto start_time = chrono::steady_clock::now();

        // 初始化挥手消息数组
        memset(wavehand_packets, 0, sizeof(wavehand_packets)); // 清零消息结构体数组

        // 第一次挥手: 发送 FIN 消息
        wavehand_packets[0].src_port = client_port;// 设置源端口
        wavehand_packets[0].dest_port = router_port; // 设置目标端口
        wavehand_packets[0].Set_FIN();// 设置 FIN 标志
        wavehand_packets[0].seq = ++Seq; // 增加序列号
        wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum();
        cout << "[日志] 第一次挥手：发送 【FIN】  序列号：" << wavehand_packets[0].seq << endl;
        // 发送 FIN 消息
        if (sendto(Client_Socket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] 【FIN】 消息发送失败！错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        // 第二次挥手: 等待 ACK 消息
        while (true) {
            // 接收 ACK 消息
            if (recvfrom(Client_Socket, (char*)&wavehand_packets[1], sizeof(wavehand_packets[1]), 0,
                (sockaddr*)&Router_Address, &addr_len) > 0) {
                // 验证是否为有效的 ACK 消息
                if (wavehand_packets[1].Is_ACK() &&
                    wavehand_packets[1].ack == wavehand_packets[0].seq &&
                    wavehand_packets[1].Check_IsValid()) {
                    cout << "[日志] 收到第二次挥手消息 【ACK】确认序列号：" << wavehand_packets[1].ack << endl;
                    break;// 确认成功，退出循环
                }
                else {
                    cerr << "[WARNING] 丢弃无效 【ACK】" << endl;
                }
            }
            // 超时重传第一次挥手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cout << "[日志] 【FIN】 消息超时！重新发送。" << endl;
                wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum(); // 重算校验和
                // 重新发送 FIN 消息
                if (sendto(Client_Socket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
                    (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
                    cerr << "[ERROR] 重传失败！" << endl;
                    return false;
                }
                start_time = now; // 更新计时，重新开始超时检测
            }
        }
        // 第三次挥手: 接收 FIN 消息
        start_time = chrono::steady_clock::now();
        while (true) {
            // 接收 FIN 消息
            if (recvfrom(Client_Socket, (char*)&wavehand_packets[2], sizeof(wavehand_packets[2]), 0,
                (sockaddr*)&Router_Address, &addr_len) > 0) {
                cout << wavehand_packets[2].Is_FIN() << wavehand_packets[2].Check_IsValid();
                // 检查是否为有效的 FIN 消息
                if (wavehand_packets[2].Is_FIN() && wavehand_packets[2].Check_IsValid()) {
                    cout << "[日志] 收到第三次挥手消息【FIN】 序列号：" << wavehand_packets[2].seq << endl;
                    break;// 成功接收到 FIN 消息，退出循环
                }
                else {
                    cerr << "[WARNING] 丢弃无效 【FIN】 " << endl;
                }
            }
            // 超时处理
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cerr << "[日志] 等待 【FIN】 超时！断开连接失败！" << endl;
                return false; // 超时未接收到有效的 FIN 消息，返回失败
            }
        }
        Seq = wavehand_packets[2].seq;
        // 第四次挥手: 发送 ACK 消息
        wavehand_packets[3].src_port = client_port;
        wavehand_packets[3].dest_port = router_port;
        wavehand_packets[3].Set_ACK();
        wavehand_packets[3].ack = wavehand_packets[2].seq;// 设置确认序列号为第三次挥手的 FIN 消息的序列号
        wavehand_packets[3].seq = ++Seq;
        wavehand_packets[3].check = wavehand_packets[3].Calculate_Checksum();
        // 发送第四次挥手的 ACK 消息
        if (sendto(Client_Socket, (char*)&wavehand_packets[3], sizeof(wavehand_packets[3]), 0,
            (sockaddr*)&Router_Address, sizeof(Router_Address)) == SOCKET_ERROR) {
            cerr << "[ERROR] 第四次挥手消息发送失败！错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[日志] 第四次挥手：发送 【ACK】确认序列号：" << wavehand_packets[3].ack << endl;

        //  等待两倍超时时间 以确保消息完成
        cout << "[日志] 等待 2 * TIMEOUT 确保连接断开..." << endl;
        this_thread::sleep_for(chrono::milliseconds(2 * TIMEOUT));
        return true;
    }



    ~UDPCLIENT() {
        if (Client_Socket != INVALID_SOCKET) {
            closesocket(Client_Socket);
            WSACleanup();
            cout << "[日志] 套接字已关闭！资源已释放..." << endl;
        }
    }
};

int main() {
    char ip_input[16];  // 用于存储用户输入的IP地址
    UDPCLIENT sender;
    cout << "---------------------------------------------------" << endl;
    cout << "||                    发送端-------->            ||" << endl;
    cout << "---------------------------------------------------" << endl;
    // 获取用户输入的配置
    cout << "请输入发送端IP地址: ";
    cin >> ip_input;
    CLIENT_IP = ip_input;  // 将用户输入的IP地址赋值给全局变量
    //cout << "请输入发送端端口: ";
   // cin >> CLIENT_PORT;
    cout << "请输入窗口大小: ";
    cin >> Windows_Size;
    // 新增：模拟参数输入
    double loss_rate;
    int delay;
    cout << "请输入丢包率(0.0-1.0): ";
    cin >> loss_rate;
    cout << "请输入延迟时间(ms): ";
    cin >> delay;
    cout << "---------------------------------------------------" << endl;
    sender.Set_SimulationParams(loss_rate, delay);

    if (!sender.INIT()) {
        cerr << "[ERROR] 发送端初始化失败！" << endl;
        return 0;
    }

    if (!sender.ThreeWay_Handshake()) {
        cerr << "[ERROR] 三次握手失败！无法建立连接！" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "---------------------------------------------------" << endl;
        cout << "Please choose an option：\n1. SEND FILE   2. EXIT\nSelect：";
        cin >> choice;


        if (choice == 1) {
            const string BASE_PATH = R"(D:\comput_network\project3-3\Project1\Project1\)";
            string filename;
            //file_path = R"(D:\comput_network\project3-3\Project1\Project1\1.jpg)";
            cout << "请输入文件名:";
            cin >> filename;
            string file_path = BASE_PATH + filename;
            cout << "---------------------------------------------------" << endl;
            if (!sender.Send_Message(file_path)) {
                cerr << "[ERROR] 文件传输失败！" << endl;
            }
        }
        else if (choice == 2) {
            if (!sender.Four_Wavehand()) {
                cerr << "[ERROR] 断开连接失败！" << endl;
            }
            else {
                cout << "[日志] 连接已成功断开！" << endl;
            }
        }
        else {
            cerr << "[WARNING] 输入有误!!!请重新输入:" << endl;
        }
    } while (choice != 2);
    return 0;
}