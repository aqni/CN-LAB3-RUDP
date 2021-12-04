#include "UDPSock.h"
#include <winsock2.h>
#include <WS2tcpip.h>
#include <cstdio>
#include <stdexcept>
#pragma comment (lib, "ws2_32")

using namespace std;

class WSA
{
public:
    WSA();
    ~WSA();

private:
    WORD wVersionrequested;
    WSADATA wsaData;
};
WSA::WSA()
{
    wVersionrequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionrequested, &wsaData) != 0)
    {
        printf("[ERR]:Failed to load Winsock!\n");
        exit(EXIT_FAILURE);
    }
}

WSA::~WSA()
{
    ::WSACleanup();
}

static WSA wsa;

UDPSock::UDPSock()
{
    socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) {
        printf("[ERR]:Failed to create socket!\n");
        exit(EXIT_FAILURE);
    }
    //取消UDP的icmp报文重置 原因及解决见：https://www.cnblogs.com/cnpirate/p/4059137.html
    BOOL bEnalbeConnRestError = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(socket, _WSAIOW(IOC_VENDOR, 12), &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), \
        NULL, 0, &dwBytesReturned, NULL, NULL);
}

UDPSock::~UDPSock()
{
    closesocket(socket);
}

void UDPSock::bind(const IPv4Addr& ip, uint16_t port)
{
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip.addr;
    int rval = ::bind(socket, (SOCKADDR*)&addr, sizeof(addr));
    if (rval == SOCKET_ERROR) {
        printf("[ERR]:Failed to bind stream socket!\n");
        exit(EXIT_FAILURE);
    }
}

size_t UDPSock::recvfrom(char* buf, size_t bufSize, IPv4Addr& ip, uint16_t& port, std::chrono::milliseconds ms)
{
    auto nms = ms.count();
    if (SOCKET_ERROR==::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&nms, sizeof(nms))) {
        printf("[ERR]:Failed to set timeout of socket!\n");
        exit(EXIT_FAILURE);
    }
    sockaddr_in addr = {0};
    int length = sizeof(sockaddr);
    int rval = ::recvfrom(socket, (char*)buf, bufSize, 0, (SOCKADDR*)&addr, &length);
    if (rval <= 0)
    {
        int errcode = WSAGetLastError();
        if (errcode != WSAETIMEDOUT) {
            printf("[ERR]:Recvfrom failed, the error code = %d\n", errcode);
            exit(EXIT_FAILURE);
        }
        rval = 0;
    }
    ip.addr = addr.sin_addr.s_addr;
    port = ntohs(addr.sin_port);
    return rval;
}


size_t UDPSock::sendto(const char* buf, size_t bufSize, const IPv4Addr& ip, uint16_t port)
{
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip.addr;
    int rval = ::sendto(socket, (char*)buf, bufSize, 0, (SOCKADDR*)&addr, sizeof(addr));
    if (rval <= 0) {
        printf("Write error or failed to send the message!\n");
        throw rval;
    }
    return rval;
}

void UDPSock::getMyaddrPort(IPv4Addr& ip, uint16_t& port)
{
    struct sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (SOCKET_ERROR ==::getsockname(socket, (struct sockaddr*)&addr, &addrLen) == -1) {
        printf("[ERR]:getsockname error: (errno: %d))\n", errno);
        exit(EXIT_FAILURE);
    }
    ip.addr = addr.sin_addr.s_addr;
    port =nToH(addr.sin_port);
}

IPv4Addr::IPv4Addr(const string& ip)
{
    struct in_addr p;
     int ret =inet_pton(AF_INET, ip.c_str(),&p);
     if (ret == 0 || errno == EAFNOSUPPORT) {
         throw runtime_error("Can't parse IP:" + ip);
     }
     addr=p.s_addr;
}

string IPv4Addr::to_string() const
{
    in_addr a;
    a.s_addr = addr;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a, str, sizeof(str));
    return string(str);
}

uint32_t hToN(uint32_t v) { return htonl(v); }
uint16_t hToN(uint16_t v) { return htons(v); }
uint32_t nToH(uint32_t v) { return ntohl(v); }
uint16_t nToH(uint16_t v) { return ntohs(v); }