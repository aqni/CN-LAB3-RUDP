#pragma once
#include <winsock2.h>
#include <cstdint>
#include <stddef.h>
#include <string>
#include <chrono>

struct IPv4Addr
{
    uint32_t addr;
    IPv4Addr() = default;
    IPv4Addr(const std::string& ip);
public:
    std::string to_string() const;
};


class UDPSock
{
public:
    UDPSock();
    UDPSock(const UDPSock&) = delete;
    UDPSock(UDPSock&&) = delete;
    ~UDPSock();
    void bind(const IPv4Addr& ip, uint16_t port);
    size_t recvfrom(char* buf, size_t bufSize, IPv4Addr& ip, uint16_t& port,std::chrono::milliseconds ms);
    size_t sendto(const char* buf, size_t bufSize, const IPv4Addr& ip, uint16_t port);
    void getMyaddrPort(IPv4Addr& ip, uint16_t& port);
private:
    SOCKET socket;
};

uint32_t hToN(uint32_t);
uint16_t hToN(uint16_t);
uint32_t nToH(uint32_t);
uint16_t nToH(uint16_t);