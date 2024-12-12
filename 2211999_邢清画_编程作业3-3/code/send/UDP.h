#define UDP_PACKET_H

#include <iostream>
#include <vector>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <winsock2.h>
#include <fstream>
#include <thread>
#include <ws2tcpip.h>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <sstream> 
#include <mutex>
#include <unordered_map>
#include <thread>

using namespace std;

#define MAX_LEN 10240  // 数据最大大小
#define TIMEOUT 1000         // 超时ms
   
#define router_ip "127.0.0.1"      // 目标路由 IP 

#define router_port 8080        // 路由端口
#define client_port 8081          // 客户端端口

extern const char* CLIENT_IP; // 路由 IP 

//extern int Windows_Size;  // 声明窗口大小

// UDP 数据报结构
struct UDP_PACKET {
    uint32_t src_port;       // 源端口
    uint32_t dest_port;      // 目标端口
    uint32_t seq;            // 序列号
    uint32_t ack;            // 确认号
    uint32_t length;         // 数据长度（包括头部和数据）
    uint16_t flag;     // 标志位
    uint16_t check;          // 校验和
    char data[MAX_LEN]; // 数据部分


    // 标志位掩码
    static constexpr uint16_t FLAG_FIN = 0x8000;  // FIN 位
    static constexpr uint16_t FLAG_CFH = 0x4000;  // CFH 位
    static constexpr uint16_t FLAG_ACK = 0x2000;  // ACK 位
    static constexpr uint16_t FLAG_SYN = 0x1000;  // SYN 位

    // 构造函数
    UDP_PACKET() {
        src_port = 0;
        dest_port = 0;
        seq = 0;
        ack = 0;
        length = 0;
        flag = 0;
        check = 0;
        memset(data, 0, MAX_LEN);
    }

    // 设置标志位
    // 设置和检查标志位的函数 - 移除了 UDP_Packet:: 前缀
    void Set_CFH() {
        flag |= FLAG_CFH;
    }

    bool Is_CFH() const {
        return (flag & FLAG_CFH) != 0;
    }

    void Set_ACK() {
        flag |= FLAG_ACK;
    }

    bool Is_ACK() const {
        return (flag & FLAG_ACK) != 0;
    }

    void Set_SYN() {
        flag |= FLAG_SYN;
    }

    bool Is_SYN() const {
        return (flag & FLAG_SYN) != 0;
    }

    void Set_FIN() {
        flag |= FLAG_FIN;
    }

    bool Is_FIN() const {
        return (flag & FLAG_FIN) != 0;
    }
    // 计算校验和
    uint16_t Calculate_Checksum() const {
        // 验证 this 和 data 的有效性
        if (this == nullptr) {
            cerr << "[ERROR] this 指针为空!无法计算校验和。" << endl;
            return 0;
        }
        if (data == nullptr) {
            cerr << "[ERROR] data 指针无效!无法计算校验和。" << endl;
            return 0;
        }

        uint32_t sum = 0;// 初始化累加器

        // 累加 UDP 头部的源端口和目的端口
        sum += src_port;
        sum += dest_port;

        // 累加序列号（高16位和低16位）
        sum += (seq >> 16) & 0xFFFF;// 高16位
        sum += seq & 0xFFFF;

        // 累加确认号（高16位和低16位）
        sum += (ack >> 16) & 0xFFFF;// 高16位
        sum += ack & 0xFFFF;
        sum += length;

        // 累加数据部分，按16位字（大端序）累加
        for (size_t i = 0; i < MAX_LEN - 1 && i + 1 < length; i += 2) {
            uint16_t word = (data[i] << 8) | (data[i + 1] & 0xFF);// 最后一个字节高位，低位补0
            sum += word;
        }
        // 将进位加回低 16 位
        // 当有进位时
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);// 将高16位加到低16位
        }
        return ~sum & 0xFFFF;// 取反并返回低16位作为校验和
    }

    // 校验和验证
    bool Check_IsValid() const {
        return (check & 0xFFFF) == Calculate_Checksum();
    }

    // 打印消息
    void Print_Message() const {
        cout << "UDP 数据包信息:" << endl;
        cout << "  源端口: " << src_port << endl;
        cout << "  目标端口: " << dest_port << endl;
        cout << "  序列号: " << seq << endl;
        cout << "  确认号: " << ack << endl;
        cout << "  长度: " << length << endl;
        cout << "  校验和: " << check << endl;

        cout << "  标志位详情:" << endl;
        cout << "    CFH: " << (Is_CFH() ? "已设置" : "未设置") << endl;
        cout << "    ACK: " << (Is_ACK() ? "已设置" : "未设置") << endl;
        cout << "    SYN: " << (Is_SYN() ? "已设置" : "未设置") << endl;
        cout << "    FIN: " << (Is_FIN() ? "已设置" : "未设置") << endl;

        cout << "  校验和位模式: " << bitset<16>(flag & 0xFFFF) << endl;
    }
};