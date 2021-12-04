#include "RRecv.h"
#include <chrono>
#include "magic_enum.hpp"

using namespace std;
using namespace chrono;

RRecv::RRecv(const IPv4Addr& addr, uint16_t port)
	:myAddr(addr),myPort(port),buffer(Size::BYTE_32K),socket(),state(State::closed), mgrThrd()
{
	this->sPkgBuf = new char[maxPkgSize()];
	this->rPkgBuf = new char[maxPkgSize()];
	this->socket.bind(myAddr,myPort);
	this->listen();
	thread temp(&RRecv::manager, this);
	mgrThrd.swap(temp);
}

uint32_t RRecv::recv(char* buf, uint32_t size)
{
	uint32_t num = 0;
	while (num == 0 && state.load() != State::closed) {
		num= buffer.pop(buf, size);
	}
	printf("[LOG]: user recv %d bytes.\n",num);
	return num;
}

void RRecv::close()
{
	state.store(State::closed);
	socket.~UDPSock();
	mgrThrd.join();
}

void RRecv::manager()
{
	printf("[LOG]: manager thread start.\n");
	while (true) {
		IPv4Addr sourceIP;
		uint16_t sourcePort;
		State oldState = state.load();
		switch (oldState) {
		default:
			printf("[ERR]: unexpected state(%s)!\n", magic_enum::enum_name(oldState).data());
			exit(EXIT_FAILURE);
		case State::closed:
			break;
		case State::recv:
			auto timeout = milliseconds(10000);
			auto rnum = socket.recvfrom(rPkgBuf, maxPkgSize(), sourceIP, sourcePort, timeout);
			if (rnum == 0) {
				printf("[LOG]: recv timeout: %lld ms.\n", timeout.count());
				continue;
			}
			RPkg* rPkg = reinterpret_cast<RPkg*>(rPkgBuf);
			uint16_t checksum = rPkg->calCheckSum(targetAddr,myAddr,targetPort,myPort);
			if (checksum != 0&& rPkg->checkSum!=0) {
				printf("[LOG]: recv error pkg :checksum=%d\n", checksum);
				continue;
			}
			if (rPkg->flags & RPkg::F_FIN) 
			{
				state.store(State::closed);
				continue;
			}
			uint32_t seq = rPkg->getSEQ();
			printf("[LOG]: recv group=%d seq=%d", group, seq);
			if (seq != group) {
				printf(" group != seq.\n");
				continue;
			}
			else {
				uint32_t pushnum = buffer.freeSize();
				uint16_t bodysize = rPkg->getBodySize();
				if (pushnum < bodysize) {
					seq = seq ? 0 : 1;
					printf(" pushnum(%d) < pkgsize(%d) discard\n", pushnum, bodysize);
				}
				buffer.push(rPkg->data, bodysize);
				printf(" deliver=%d\n", bodysize);
				group = group ? 0 : 1;
			}
			RPkg* sPkg = new(sPkgBuf) RPkg();
			sPkg->makeACK(seq);
			sPkg->setCheckSum(sPkg->calCheckSum(myAddr, targetAddr, myPort, targetPort));
			socket.sendto(sPkgBuf, sPkg->size(), targetAddr, targetPort);
			printf("[LOG]: send ack=%d\n", seq);
			continue;
		}
		break;
	}
	printf("[LOG]: manager thread end.\n");
}

void RRecv::listen()
{
	state.store(State::listening);
	while (true) {
		auto timeout = milliseconds(10000);
		auto rnum = socket.recvfrom(rPkgBuf, maxPkgSize(), targetAddr, targetPort, timeout);
		if (rnum==0) {
			printf("[LOG]: listen timeout: %lld ms\n", timeout.count());
			continue;
		}
		RPkg* rPkg = reinterpret_cast<RPkg*>(rPkgBuf);
		if (rPkg->flags & RPkg::F_SYN) {
			RPkg* sPkg = new(sPkgBuf) RPkg();
			sPkg->makeACK(0);
			socket.sendto(sPkgBuf, sPkg->size(), targetAddr, targetPort);
			break;
		}
	}
	state.store(State::recv);
	printf("[LOG]: succeed to listen a connect from %s:%d.\n",targetAddr.to_string().c_str(),targetPort);
}
