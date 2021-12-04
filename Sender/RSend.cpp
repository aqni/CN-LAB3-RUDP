#include "RSend.h"
#include <iostream>
#include "magic_enum.hpp"
#include <random>

using namespace std;
using namespace chrono;

RSend::RSend(const IPv4Addr& addr, uint16_t port)
	:targetAddr(addr),targetPort(port),socket(),buffer(Size::BYTE_32K),state(State::closed)
{
	this->sPkgBuf = new char[maxPkgSize()];
	this->rPkgBuf = new char[maxPkgSize()];
	this->connect();
	printf("[LOG]: Succeed to connect to host:%s:%d\n", addr.to_string().c_str(),port);

	//start the manager thread
	thread temp(&RSend::manager, this);
	mgrThrd.swap(temp);
}

RSend::~RSend()
{
	delete[]sPkgBuf;
	delete[]rPkgBuf;
}

uint32_t RSend::send(const char* data, uint32_t size)
{
	uint32_t pushnum = 0;
	while (pushnum < size)
	{
		if (isClosing.load()) 
			break;
		pushnum += buffer.push(data+ pushnum, size-pushnum);
	}
	printf("[LOG]: user try to send %d bytes.\n", pushnum);
	return pushnum;
}

void RSend::close()
{
	printf("[LOG]: user try to close.\n");
	isClosing.store(true);
	mgrThrd.join();
	printf("[LOG]: close successfully.\n");
}

RPkg* RSend::recvPkg()
{
	IPv4Addr sourceIP;
	uint16_t sourcePort;
	RPkg* sPkg = reinterpret_cast<RPkg*>(sPkgBuf);
	while (true) {
		auto timeout = milliseconds(getRTO());
		auto rnum = socket.recvfrom(rPkgBuf, maxPkgSize(), sourceIP, sourcePort, timeout);
		if (rnum != 0) {
			nTimeout = 0;
			RPkg* rPkg= reinterpret_cast<RPkg*>(rPkgBuf);
			uint16_t checksum = rPkg->calCheckSum(myAddr, targetAddr, myPort, targetPort);
			if (checksum == 0 || rPkg->checkSum == 0) {
				return rPkg;
			}
			printf("[LOG]: recv error pkg :checksum=%d\n", checksum);
		}
		if (++nTimeout > maxNTimeout) {
			printf("[ERR]: recv timeout more than %d times.\n", maxNTimeout);
			exit(EXIT_FAILURE);
		}
		printf("[LOG]: recv timeout: %lld ms. resend", timeout.count());
		socket.sendto(sPkgBuf, sPkg->size(), targetAddr, targetPort);
		printf(" group=%d seq=%d.\n", group, sPkg->getSEQ());
	}
}

void RSend::doSend()
{
	RPkg* sPkg = new(sPkgBuf) RPkg();
	uint32_t popnum = buffer.pop(sPkg->data, MSS);
	if (popnum == 0) {
		return;
	}
	sPkg->makeSEQ(group, popnum);
	sPkg->setCheckSum(sPkg->calCheckSum(myAddr, targetAddr, myPort, targetPort));
	socket.sendto(sPkgBuf, sPkg->size(), targetAddr, targetPort);
	printf("[LOG]: send group=%d seq=%d size=%d.\n", group, sPkg->getSEQ(), sPkg->getBodySize());
	setState(State::wait);
}

void RSend::doWait()
{
	RPkg* rPkg = recvPkg();
	uint32_t ack = rPkg->getACK();
	printf("[LOG]: recv group=%d ack=%d.", group, ack);
	if (!(rPkg->flags & RPkg::F_ACK)) {
		printf(" not ack, discard.\n");
		return;
	}
	if (ack != group)
	{
		printf(" group!=ack do nothing\n");
		return;
	}
	group = group ? 0 : 1;
	printf(" success send.\n");
	setState(State::send);
}

bool RSend::timeToClose()
{
	return isClosing.load()&&buffer.empty();
}


void RSend::closeConnection()
{
	RPkg* sPkg = new(sPkgBuf) RPkg();
	sPkg->makeFIN(group);
	sPkg->setCheckSum(sPkg->calCheckSum(myAddr,targetAddr,myPort,targetPort));
	socket.sendto(sPkgBuf,sPkg->size(),targetAddr,targetPort);
	printf("[LOG]: send FIN.\n");
	RPkg* rPkg = recvPkg();
	printf("[LOG]: recv ack.\n");
	setState(State::closed);
}

std::chrono::milliseconds RSend::getRTO()
{
	uint16_t out = 10 << nTimeout;
	out = max(10, out);
	return std::chrono::milliseconds(out);
}

void RSend::setState(State s)
{
	State oldState = state;
	state = s;
	printf("[LOG]: ===== state(%s) ---> state(%s) ===== \n",
		magic_enum::enum_name(oldState).data(),
		magic_enum::enum_name(s).data());
}

void RSend::manager()
{
	printf("[LOG]: manager thread start.\n");
	while (true)
	{
		if (timeToClose()) {
			closeConnection();
			break;
		}
		switch (state) {
		default: 
			printf("[ERR]: unexpected state(%s)!\n",magic_enum::enum_name(state).data());
			exit(EXIT_FAILURE);
		case State::closed:
			break;
		case State::send:
			doSend();
			continue;
		case State::wait:
			doWait();
			continue;
		}
		break;
	}
	printf("[LOG]: manager thread end.\n");
}

void RSend::connect()
{
	cout << "try connect to " << targetAddr.to_string() << ":" << targetPort << endl;
	RPkg* sPkg = new(sPkgBuf) RPkg();
	sPkg->makeSYN();
	const char* ptr = reinterpret_cast<const char*>(sPkg);
	socket.sendto(ptr,sPkg->size(),targetAddr,targetPort);
	socket.getMyaddrPort(myAddr, myPort);
	printf("[LOG]: sender bind in %s:%d.\n",myAddr.to_string().c_str(), myPort);
	while (true) {
		auto timeout = milliseconds(1000);
		IPv4Addr sourceIP;
		uint16_t sourcePort;
		size_t rnum = socket.recvfrom(rPkgBuf, maxPkgSize(), sourceIP, sourcePort, timeout);
		if (rnum == 0) {
			nTimeout++;
			if (nTimeout > maxNTimeout) {
				printf("[ERR]: connect timeout more than %d times.\n", maxNTimeout);
				exit(EXIT_FAILURE);
			}
			printf("[LOG]: connect timeout :%lld ms.\n", timeout.count());
			continue;
		}
		nTimeout = 0;
		RPkg* rPkg = reinterpret_cast<RPkg*>(rPkgBuf);
		break;
	}
	printf("[LOG]: succeed to connect.\n");
	setState(State::send);
}

//void RSend::setTimer(milliseconds ms,uint8_t nTimeout, uint32_t dataSeq, uint16_t dataLen)
//{
//	timevts.emplace(dataSeq,dataLen, nTimeout,ms);
//}


//void RSend::timerProcess()
//{
//	while (timevts.top().tp > system_clock::now())
//	{
//		State oldState = state.exchange(State::connecting);
//		const auto& evt = timevts.top();
//		if (evt.nTimeout < 1) {
//			timeout(evt);
//		}
//	}
//}
