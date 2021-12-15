#pragma once

#include "RingBuffer.h"
#include "UDPSock.h"
#include "RPkg.h"
#include <vector>
#include <thread>
#include <list>

class RRecv
{
public:
    enum class State
    {
        closed,
        listening,
        recv,
    };
public:
    RRecv(const IPv4Addr& addr, uint16_t port);
    ~RRecv();
    uint32_t recv(char* buf, uint32_t size);
    void close();

private:
    void manager();
    inline size_t maxPkgSize() const { return sizeof(RPkg) + MSS;}
    void setState(State s);
    inline uint16_t getWindow() { return buffer.freeSize(); }
    RPkg* recvPkg();
    void sendAck(uint32_t ack, uint16_t window);
    void deliverData(uint32_t seq,uint16_t size,const char* data);
private:
    std::atomic<State> state;
    RingBuffer<char> buffer;
    std::list<std::pair<uint32_t, uint32_t>> cacheRanges;
    UDPSock socket;
    IPv4Addr targetAddr;
    IPv4Addr myAddr;
    uint16_t targetPort;
    uint16_t myPort;
    uint16_t MSS=SOCK_MSS;
    char* rPkgBuf;
    std::thread mgrThrd;
    uint32_t nextACK=-1;
};
