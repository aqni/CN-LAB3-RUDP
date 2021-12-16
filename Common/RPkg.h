#pragma once
#include <cstdint>
#include "UDPSock.h"

#pragma pack(1)
struct RPkg
{
    uint32_t ack = 0;      //确认序列号
    uint32_t seq = 0;      //发送序列号
    uint16_t window = 0;   //接收窗口大小
    uint16_t checkSum = 0; //校验和
    uint16_t bodySize = 0; //校验和
    uint8_t flags = 0;     //标志位
    char data[];

public:
    inline void makeSYN(uint32_t seq) { this->seq = hToN(seq); bodySize = 0; flags = F_SYN;}
    inline void makeFIN(uint32_t seq) { this->seq = hToN(seq); bodySize = 0; flags = F_FIN;};
    inline void makeACK(uint32_t ack, uint16_t win,uint32_t seq) { this->ack = hToN(ack);  window = hToN(win); flags = F_ACK; this->seq = hToN(seq);}
    inline void makeSEQ(uint32_t s, uint16_t bsize) { seq = hToN(s); bodySize = hToN(bsize); flags = F_SEQ;}
    inline uint32_t getACK()const { return nToH(ack); }
    inline uint32_t getSEQ()const { return nToH(seq); };
    inline uint16_t getBodySize() const { return nToH(bodySize); }
    inline uint16_t getWindow() const { return nToH(window); }
    inline size_t size() const { return sizeof(*this) + nToH(bodySize);}
    inline void setCheckSum(uint16_t sum) { checkSum = hToN(sum); };
    inline uint16_t calCheckSum()const;
    static constexpr uint8_t F_FIN = 0x80;
    static constexpr uint8_t F_SYN = 0x40;
    static constexpr uint8_t F_ACK = 0x20;
    static constexpr uint8_t F_SEQ = 0x10;
};
#pragma pack()



inline uint16_t RPkg::calCheckSum() const
{
    uint16_t* words = (uint16_t*)this;
    uint32_t checksum = 0;
    size_t size = this->size();
    size_t looplen = size / sizeof(uint16_t);
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

