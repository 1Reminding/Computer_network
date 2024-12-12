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

#define MAX_LEN 10240  // ��������С
#define TIMEOUT 1000         // ��ʱms
   
#define router_ip "127.0.0.1"      // Ŀ��·�� IP 

#define router_port 8080        // ·�ɶ˿�
#define client_port 8081          // �ͻ��˶˿�

extern const char* CLIENT_IP; // ·�� IP 

//extern int Windows_Size;  // �������ڴ�С

// UDP ���ݱ��ṹ
struct UDP_PACKET {
    uint32_t src_port;       // Դ�˿�
    uint32_t dest_port;      // Ŀ��˿�
    uint32_t seq;            // ���к�
    uint32_t ack;            // ȷ�Ϻ�
    uint32_t length;         // ���ݳ��ȣ�����ͷ�������ݣ�
    uint16_t flag;     // ��־λ
    uint16_t check;          // У���
    char data[MAX_LEN]; // ���ݲ���


    // ��־λ����
    static constexpr uint16_t FLAG_FIN = 0x8000;  // FIN λ
    static constexpr uint16_t FLAG_CFH = 0x4000;  // CFH λ
    static constexpr uint16_t FLAG_ACK = 0x2000;  // ACK λ
    static constexpr uint16_t FLAG_SYN = 0x1000;  // SYN λ

    // ���캯��
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

    // ���ñ�־λ
    // ���úͼ���־λ�ĺ��� - �Ƴ��� UDP_Packet:: ǰ׺
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
    // ����У���
    uint16_t Calculate_Checksum() const {
        // ��֤ this �� data ����Ч��
        if (this == nullptr) {
            cerr << "[ERROR] this ָ��Ϊ��!�޷�����У��͡�" << endl;
            return 0;
        }
        if (data == nullptr) {
            cerr << "[ERROR] data ָ����Ч!�޷�����У��͡�" << endl;
            return 0;
        }

        uint32_t sum = 0;// ��ʼ���ۼ���

        // �ۼ� UDP ͷ����Դ�˿ں�Ŀ�Ķ˿�
        sum += src_port;
        sum += dest_port;

        // �ۼ����кţ���16λ�͵�16λ��
        sum += (seq >> 16) & 0xFFFF;// ��16λ
        sum += seq & 0xFFFF;

        // �ۼ�ȷ�Ϻţ���16λ�͵�16λ��
        sum += (ack >> 16) & 0xFFFF;// ��16λ
        sum += ack & 0xFFFF;
        sum += length;

        // �ۼ����ݲ��֣���16λ�֣�������ۼ�
        for (size_t i = 0; i < MAX_LEN - 1 && i + 1 < length; i += 2) {
            uint16_t word = (data[i] << 8) | (data[i + 1] & 0xFF);// ���һ���ֽڸ�λ����λ��0
            sum += word;
        }
        // ����λ�ӻص� 16 λ
        // ���н�λʱ
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);// ����16λ�ӵ���16λ
        }
        return ~sum & 0xFFFF;// ȡ�������ص�16λ��ΪУ���
    }

    // У�����֤
    bool Check_IsValid() const {
        return (check & 0xFFFF) == Calculate_Checksum();
    }

    // ��ӡ��Ϣ
    void Print_Message() const {
        cout << "UDP ���ݰ���Ϣ:" << endl;
        cout << "  Դ�˿�: " << src_port << endl;
        cout << "  Ŀ��˿�: " << dest_port << endl;
        cout << "  ���к�: " << seq << endl;
        cout << "  ȷ�Ϻ�: " << ack << endl;
        cout << "  ����: " << length << endl;
        cout << "  У���: " << check << endl;

        cout << "  ��־λ����:" << endl;
        cout << "    CFH: " << (Is_CFH() ? "������" : "δ����") << endl;
        cout << "    ACK: " << (Is_ACK() ? "������" : "δ����") << endl;
        cout << "    SYN: " << (Is_SYN() ? "������" : "δ����") << endl;
        cout << "    FIN: " << (Is_FIN() ? "������" : "δ����") << endl;

        cout << "  У���λģʽ: " << bitset<16>(flag & 0xFFFF) << endl;
    }
};