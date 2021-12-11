#include "RSend.h"
#include <iostream>
#include "magic_enum.hpp"
#include <random>

using namespace std;
using namespace chrono;
using namespace magic_enum;

RSend::RSend(const IPv4Addr& addr, uint16_t port)
	:targetAddr(addr),targetPort(port),socket(),buffer(Size::BYTE_32K),state(State::closed)
{
	this->rPkgBuf = new char[maxPkgSize()];
	this->connect();

	//start the manager thread
	thread temp(&RSend::manager, this);
	mgrThrd.swap(temp);
}

RSend::~RSend()
{
	delete[]rPkgBuf;
}

uint32_t RSend::send(const char* data, uint32_t size)
{
	uint32_t pushnum = 0;
	while (pushnum < size)
	{
		if (state!=State::send) 
			break;
		int partnum= buffer.push(data + pushnum, size - pushnum);
		pushnum += partnum;
		if (partnum != 0) {
			printf("[LOG]: <USER> PUSH %d bytes into buffer.\n", partnum);
		}
	}
	return pushnum;
}

void RSend::close()
{
	printf("[LOG]: <USER> try to close.\n");
	setState(State::closing);
	mgrThrd.join();
	printf("[LOG]: <USER> close successfully.\n");
}

void RSend::connect()
{
	cout << "[LOG]: <USER> try connect to " << targetAddr.to_string() << ":" << targetPort << endl;
	setState(State::connecting);
	while (true) {
		sendSyn(nextSeq);
		setTimer(nextSeq,0,0,RPkg::F_SYN);
		RPkg* rPkg = nullptr;
		while (rPkg == nullptr) rPkg = recvACK();
		if (rPkg->getACK()==nextSeq) {
			window = rPkg->getWindow();
			break;
		}
		else {
			printf("[ERR]: connect recv ack err. recv ack=%d, but nextSeq=%d.\n",rPkg->getACK(),nextSeq);
			exit(EXIT_FAILURE);
		}
	}
	socket.getMyaddrPort(myAddr, myPort);
	printf("[LOG]: <USER> BING in %s:%d.\n", myAddr.to_string().c_str(), myPort);
	printf("[LOG]: <USER> CONNECT to host:%s:%d\n", targetAddr.to_string().c_str(), targetPort);
	setState(State::send);
}

void RSend::closeConnection()
{
	sendFin(nextSeq);
	setTimer(nextSeq,0,0,RPkg::F_FIN);
	RPkg* rPkg = nullptr;
	while(rPkg==nullptr) rPkg = recvACK();
	if (rPkg->getACK() != nextSeq) {
		printf("[ERR]: connect recv ack err. recv ack=%d, but nextSeq=%d.\n", rPkg->getACK(), nextSeq);
		exit(EXIT_FAILURE);
	}
	setState(State::closed);
}

void RSend::trySendPkg()
{
	while (true) {
		int sendSize = min(
			min(buffer.begin() + window - nextSeq/*窗口限制*/,buffer.end() - nextSeq/*缓冲区数量*/),
			MSS
		);
		if (!(sendSize>0)) break;
		sendSize=sendSeq(nextSeq, sendSize);
		setTimer(nextSeq, sendSize,0,RPkg::F_SEQ);
		nextSeq += sendSize;
		printf("[LOG]: STATE nextSeq=%d,sendBase=%d,window=%d,toSend=%d, buffer:[%d,%d).\n",
			nextSeq, buffer.begin(),window,buffer.end()-nextSeq,buffer.begin(), buffer.end());
	}
}

void RSend::wait()
{
	RPkg* rPkg = recvACK();
	if (rPkg == nullptr)return;
	uint32_t ack = rPkg->getACK();
	window = rPkg->getWindow();
	printf("[LOG]: RECV ACK ack=%d, nextSeq=%d, window=%d, buffer[%d,%d)\n", 
		ack,nextSeq,window,buffer.begin(),buffer.end());

	if (ack > buffer.begin()) { //if ack > sendbase
		uint32_t popnum=buffer.pop(ack- buffer.begin());
		printf("[LOG]: POP %d bytes from buffer. buffer:[%d,%d).\n", popnum,buffer.begin(),buffer.end());
	}
	else {
		if (lastAck == ack) {
			if (++repeatACK>=3) {
				uint32_t sendnum=sendSeq(lastAck,MSS);
				printf("[LOG]: REPEAT 3 ack=%d. send [%d,%d).\n", lastAck, lastAck, lastAck+ sendnum);
				repeatACK = 0;
			}
		} else {
			repeatACK = 0;
			lastAck = ack;
		}

	}
}

RPkg* RSend::recvACK()
{
	IPv4Addr sourceIP;
	uint16_t sourcePort;
	milliseconds timeout= handleTimer();
	auto rnum = socket.recvfrom(rPkgBuf, maxPkgSize(), sourceIP, sourcePort, timeout);
	if (rnum == 0) return nullptr;
	RPkg* rPkg = reinterpret_cast<RPkg*>(rPkgBuf);
	if (uint16_t checksum = rPkg->calCheckSum(); checksum != 0 && rPkg->checkSum != 0) {
		printf("[LOG]: RECV err pkg checksum=%d.\n", checksum);
		return nullptr;
	}
	if (rPkg->flags != RPkg::F_ACK) {
		printf("[LOG]: RECV pkg is not ack, flags=%d.\n", rPkg->flags);
		return nullptr;
	}
	return rPkg;
}

uint32_t RSend::sendSeq(uint32_t seq, uint16_t size)
{
	char* sPkgBuf = new char[maxPkgSize()];
	RPkg* sPkg = new(sPkgBuf) RPkg();

	uint32_t getnum = buffer.get(seq, sPkg->data, size);
	printf("[LOG]: GET %d bytes from buffer .\n", getnum);
	sPkg->makeSEQ(seq, getnum);
	sPkg->setCheckSum(sPkg->calCheckSum());
	socket.sendto(sPkgBuf, sPkg->size(), targetAddr, targetPort);
	printf("[LOG]: SEND SEQ [%d,%d).\n", sPkg->getSEQ(), sPkg->getSEQ()+sPkg->getBodySize());
	delete[] sPkgBuf;
	return getnum;
}

void RSend::sendSyn(uint32_t seq)
{
	RPkg sPkg;
	sPkg.makeSYN(seq);
	sPkg.setCheckSum(sPkg.calCheckSum());
	socket.sendto((char*)&sPkg, sPkg.size(), targetAddr, targetPort);
	printf("[LOG]: SEND SYN [%d,%d).\n", seq, seq);
}

void RSend::sendFin(uint32_t seq)
{
	RPkg sPkg;
	sPkg.makeFIN(seq);
	sPkg.setCheckSum(sPkg.calCheckSum());
	socket.sendto((char*)&sPkg, sPkg.size(), targetAddr, targetPort);
	printf("[LOG]: SEND FIN [%d,%d).\n", seq, seq);
}

milliseconds RSend::getRTO()
{
	return milliseconds(10);
}

inline void RSend::setTimer(uint32_t seq, uint16_t size,uint8_t nTimeout,uint8_t flags)
{
	auto timeout = milliseconds(getRTO().count()<< nTimeout);
	timevts.emplace(seq, size, timeout, nTimeout, flags);
	printf("[LOG]: TIMER [%d,%d) in %lld ms, %d times,flag=%d.\n",seq,seq+size,timeout.count(), nTimeout,flags);
}

milliseconds RSend::handleTimer()
{
	if (timevts.empty()) return milliseconds(10);
	auto now = system_clock::now();
	while (now > timevts.top().tp) {
		processTimeout(timevts.top());
		timevts.pop();
		if (timevts.empty()) return milliseconds(10);
	}
	return duration_cast<milliseconds>(timevts.top().tp - now);
}

void RSend::processTimeout(const Timevt& evt)
{
	if ((evt.dataSeq + evt.dataLen > buffer.begin()) || (evt.dataSeq == nextSeq && evt.dataLen == 0)) { //如果需要重传
		if (evt.nTimeout > maxNTimeout) {
			printf("[ERR]: timeout more than %d times.\n", maxNTimeout);
			exit(EXIT_FAILURE);
		}
		printf("[LOG]: TIMEOUT [%d,%d), flag=%d, start resend.\n", evt.dataSeq, evt.dataSeq + evt.dataLen,evt.flag);
		if (evt.flag & RPkg::F_SEQ)
			sendSeq(evt.dataSeq, evt.dataLen);
		if (evt.flag & RPkg::F_SYN)
			sendSyn(evt.dataSeq);
		if (evt.flag & RPkg::F_FIN)
			sendFin(evt.dataSeq);
		setTimer(evt.dataSeq, evt.dataLen, evt.nTimeout + 1, evt.flag);
	}//else 否则已经被确认
}

void RSend::setState(State s)
{
	State old = state;
	state = s;
	printf("[LOG]: %s-->%s | window=%d, nextSeq=%d, buffer=[%d,%d).\n",
		enum_name(old).data(), enum_name(s).data(),
		window, nextSeq, buffer.begin(), buffer.end());
}

void RSend::manager()
{
	printf("[LOG]: manager thread start.\n");
	while (true)
	{
		switch (state) {
		default: 
			printf("[ERR]: unexpected state(%s)!\n",magic_enum::enum_name(state).data());
			exit(EXIT_FAILURE);
		case State::closing:
			if (buffer.empty()) {
				closeConnection();
				continue;
			}
		case State::send:
			trySendPkg();
			wait();
			continue;
		case State::closed:
			break;
		}
		break;
	}
	printf("[LOG]: manager thread end.\n");
}