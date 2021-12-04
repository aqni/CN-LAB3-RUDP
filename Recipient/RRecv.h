#pragma once

#include "RingBuffer.h"
#include "UDPSock.h"
#include "RPkg.h"

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
    uint32_t recv(char* buf, uint32_t size);
    void close();

private:
    void manager();
    inline size_t maxPkgSize() const { return sizeof(RPkg) + MSS;}
    void listen();
private:
    std::atomic<State> state;
    RingBuffer<char> buffer;
    UDPSock socket;
    IPv4Addr targetAddr;
    IPv4Addr myAddr;
    uint16_t targetPort;
    uint16_t myPort;
    uint16_t MSS=1024;
    char* sPkgBuf;
    char* rPkgBuf;
    std::thread mgrThrd;
    //ack
    uint32_t group = 0;
};

//#include "UDPConn.h"
//#include <iostream>
//
//#define LOG
//
//#ifdef LOG
//#undef LOG
//
//#define LOG(stmt) \
//    do            \
//    {             \
//        stmt;     \
//    } while (0)
//#else
//#define LOG(stmt) \
//    do            \
//    {             \
//    } while (0)
//#endif
//
//using namespace std;
//using State = UDPConn::State;
//
//string to_string(State s)
//{
//#define ENUM_CHIP_TYPE_CASE(x) \
//    case x:                    \
//        return (#x);
//    switch (s)
//    {
//    default:
//        throw s;
//        ENUM_CHIP_TYPE_CASE(State::closed)
//            ENUM_CHIP_TYPE_CASE(State::listening)
//            ENUM_CHIP_TYPE_CASE(State::connecting)
//    }
//#undef ENUM_CHIP_TYPE_CASE
//}
//
//UDPConn::UDPConn() : buffer(Size::BYTE_32K), socket()
//{
//    MSS = 1440;
//    state = State::closed;
//    LOG(cout << "Create: " << to_string(state) << endl);
//    LOG(cout << LINE << endl);
//}
//
//void UDPConn::listen(const string& ipv4Addr, uint16_t port)
//{
//    LOG(cout << "listen: " << to_string(state) << ARROW << to_string(State::listening) << endl);
//    state = State::listening;
//    addr = IPv4Addr(ipv4Addr);
//    this->port = port;
//    socket.bind(addr, this->port);
//    char buf[sizeof(PkgHeader)];
//    size_t n = socket.recvfrom(buf, sizeof(buf), addr, this->port);
//    PkgHeader* pph = (PkgHeader*)buf;
//    if (!(n == sizeof(buf) && (pph->flags & PkgHeader::F_SYN)))
//        throw;
//    // PkgHeader kp=PkgHeader{
//    //     .ack=
//    // };
//}
//
//uint32_t UDPConn::send(const char* data, uint32_t size)
//{
//    return buffer.push(data, size);
//}