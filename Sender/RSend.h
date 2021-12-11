#pragma once

#include "RingBuffer.h"
#include "UDPSock.h"
#include "RPkg.h"
#include <thread>
#include <chrono>
#include <queue>
#include <algorithm>
#include <mutex>

//

struct Timevt {
    std::chrono::time_point<std::chrono::system_clock> tp;
    uint32_t dataSeq;
    uint16_t dataLen;
    uint8_t nTimeout;
    uint8_t flag;
    Timevt(uint32_t dataSeq,uint16_t dataLen, std::chrono::milliseconds ms,uint8_t nTimeout,uint8_t flags)
        :dataSeq(dataSeq),dataLen(dataLen),nTimeout(nTimeout), tp(ms + std::chrono::system_clock::now()),flag(flags) {}
};

inline bool operator > (const Timevt& v1, const Timevt& v2) {
    return v1.tp > v2.tp;
}

class RSend
{
public:
    enum class State
    {
        closed,
        closing,
        connecting,
        send,
    };

public:
    RSend(const IPv4Addr& addr, uint16_t port);
    ~RSend();
    uint32_t send(const char* data, uint32_t size);
    void close();

private:
    inline size_t maxPkgSize() const { return sizeof(RPkg) + MSS; }
    void manager();
    void connect();
    RPkg* recvACK();
    uint32_t sendSeq(uint32_t seq,uint16_t size);
    void sendSyn(uint32_t seq);
    void sendFin(uint32_t seq);
    void trySendPkg();
    void wait();
    void closeConnection();
    std::chrono::milliseconds getRTO();
    inline void setTimer(uint32_t seq, uint16_t size, uint8_t nTimeout, uint8_t flags);
    std::chrono::milliseconds handleTimer(); //����ʱ������������һ��ʱ����ʱ��
    void processTimeout(const Timevt& evt);
    void setState(State s);
private:
    std::priority_queue<Timevt,std::vector<Timevt>,std::greater<Timevt>> timevts;
    std::mutex timerMtx;
    State state;
    std::atomic_bool isClosing=false;
    RingBuffer<char> buffer;
    UDPSock socket;
    IPv4Addr targetAddr;
    IPv4Addr myAddr;
    uint16_t targetPort;
    uint16_t myPort;
    uint16_t MSS=10240;
    uint16_t nTimeout = 0;
    uint16_t maxNTimeout = 6;
    char* rPkgBuf;
    std::thread mgrThrd;

    //stop to wait
    uint32_t nextSeq = 0;
    uint32_t window = 0;
    unsigned repeatACK = 0;
    uint32_t lastAck = 0;
};
