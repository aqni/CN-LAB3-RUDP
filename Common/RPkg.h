#pragma once
#include <cstdint>
#include "UDPSock.h"

#pragma pack(1)
struct RPkg
{
    uint32_t ack = 0;      //ȷ�����к�
    uint32_t seq = 0;      //�������к�
    uint16_t window = 0;   //���մ��ڴ�С
    uint16_t checkSum = 0; //У���
    uint16_t bodySize = 0; //У���
    uint8_t flags = 0;     //��־λ
    char data[];

public:
    inline void makeSYN() {flags = F_SYN;}
    inline void makeFIN(uint32_t seq) { this->seq = hToN(seq); flags = F_FIN;};
    inline void makeACK(uint32_t ack){this->ack = hToN(ack);flags |= F_ACK;}
    inline void makeSEQ(uint32_t s, uint16_t bsize) { seq = hToN(s); bodySize = hToN(bsize);}
    inline uint32_t getACK()const { return nToH(ack); }
    inline uint32_t getSEQ()const { return nToH(seq); };
    inline uint16_t getBodySize() const { return nToH(bodySize); }
    inline size_t size() const { return sizeof(*this) + nToH(bodySize);}
    inline void setCheckSum(uint16_t sum) { checkSum = hToN(sum); };
    inline uint16_t calCheckSum(const IPv4Addr& sip, const IPv4Addr& dip, uint16_t sport, uint16_t dport)const;
    static constexpr uint8_t F_FIN = 0x80;
    static constexpr uint8_t F_SYN = 0x40;
    static constexpr uint8_t F_ACK = 0x20;
};
#pragma pack()



inline uint16_t RPkg::calCheckSum(const IPv4Addr& sip, const IPv4Addr& dip, uint16_t sport, uint16_t dport) const
{
    uint16_t* words = (uint16_t*)this;
    uint32_t checksum = 0;
    size_t size = this->size();
    int looplen = size / sizeof(uint16_t);
    for (int i = 0; i < looplen; i++)
        checksum += nToH(words[i]);
    if (size % sizeof(uint16_t) == 1) {
        uint16_t part = ((uint8_t*)this)[size - 1];
        checksum += part << 8;
    }
    checksum = (checksum >> 16) + (checksum & 0x0000FFFF);
    checksum = (checksum >> 16) + checksum;
    return (uint16_t)~checksum;
}

