#pragma once

#include "RingBuffer.h"
#include "UDPSock.h"
#include "RPkg.h"
#include <thread>
#include <chrono>
#include <queue>

struct Timevt {
    std::chrono::time_point<std::chrono::system_clock> tp;
    uint32_t dataSeq;
    uint16_t dataLen;
    uint16_t nTimeout;
    Timevt(uint32_t dataSeq,uint16_t dataLen, uint8_t nTimeout,std::chrono::milliseconds ms)
        :dataSeq(dataSeq),dataLen(dataLen),nTimeout(0), tp(ms + std::chrono::system_clock::now()) {}
};

inline bool operator < (const Timevt& v1, const Timevt& v2) {
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
        wait,
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
    //void setTimer(std::chrono::milliseconds ms, uint8_t nTimeout, uint32_t dataSeq, uint16_t dataLen);
    //void timerProcess();
    //void timeout(const Timevt& evt);
    RPkg* recvPkg();
    void doSend();
    void doWait();
    bool timeToClose();
    bool checkPkg(RPkg*);
    void closeConnection();
    std::chrono::milliseconds getRTO();
    State getState() { return state; }
    void setState(State s);
private:
    std::priority_queue<Timevt> timevts;
    State state;
    std::atomic_bool isClosing=false;
    RingBuffer<char> buffer;
    UDPSock socket;
    IPv4Addr targetAddr;
    IPv4Addr myAddr;
    uint16_t targetPort;
    uint16_t myPort;
    uint16_t MSS=1024;
    uint16_t nTimeout = 0;
    uint16_t maxNTimeout = 10;
    char* sPkgBuf;
    char* rPkgBuf;
    std::thread mgrThrd;
    std::mutex wlock;

    //stop to wait
    uint32_t group=0;
};
