#include "RSend.h"
#include <iostream>
#include "magic_enum.hpp"
#include <random>
#include <algorithm>

using namespace std;
using namespace chrono;
using namespace magic_enum;

RSend::RSend(const IPv4Addr& addr, uint16_t port)
	:targetAddr(addr),targetPort(port),socket(),buffer(Size::BYTE_32K),state(State::closed)
{
	this->rPkgBuf = new char[maxPkgSize()];
	this->connect();

	printf("[LOG]: <Congestion> INTI cwnd=%d ssthresh=%d dupACK=%d.\n",cwnd,ssthresh,dupACK);

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
			rwnd = rPkg->getWindow();
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
		uint32_t remainRwnd = buffer.begin() + rwnd - nextSeq/*窗口限制*/;
		uint32_t remainCached = buffer.end() - nextSeq/*缓冲区数量*/;
		uint32_t reaminCwnd = buffer.begin() + cwnd - nextSeq;
		auto minArr = to_array({ (uint32_t)MSS,remainRwnd,remainCached,reaminCwnd });
		uint16_t sendSize = *min_element(minArr.begin(), minArr.end());
		if (!(sendSize>0)) break;
		sendSize=sendSeq(nextSeq, sendSize);
		setTimer(nextSeq, sendSize,0,RPkg::F_SEQ);
		nextSeq += sendSize;
		printf("[LOG]: STATE nextSeq=%d,sendBase=%d,rwnd=%d,toSend=%d, buffer:[%d,%d).\n",
			nextSeq, buffer.begin(),rwnd,buffer.end()-nextSeq,buffer.begin(), buffer.end());
	}
}

void RSend::wait()
{
	RPkg* rPkg = recvACK();
	if (rPkg == nullptr)return;
	uint32_t ack = rPkg->getACK();
	rwnd = rPkg->getWindow();
	printf("[LOG]: RECV ACK ack=%d, nextSeq=%d, rwnd=%d, buffer[%d,%d)\n", 
		ack,nextSeq,rwnd,buffer.begin(),buffer.end());
	bool updated = updateTimer(rPkg->getSEQ(), ack);
	//CON 拥塞控制
	if (ack > buffer.begin()) { //if new ACK
		uint32_t popnum=buffer.pop(ack- buffer.begin());
		printf("[LOG]: POP %d bytes from buffer. buffer:[%d,%d).\n", popnum,buffer.begin(),buffer.end());
		updateCongestionWindow();
	} else {
		if (lastAck == ack) { //if duplicate ACK
			dupACK++;
			printf("[LOG]: <Congestion> DUP_ACK ack=%d, dupNum=%d.\n", lastAck, dupACK);
			if (cState == CongestionState::fastRecovery) {
				cwnd = cwnd + MSS;
				printf("[LOG]: <Congestion> DUP_ACK when fastRecovery cwnd+MSS->%d.\n", cwnd);
			}else if (dupACK>=3) {
				//enter fastRecovery
				uint32_t sendnum = sendSeq(lastAck, MSS);
				printf("[LOG]: <Congestion> DUP_3_ACK ack=%d. send [%d,%d).\n", lastAck, lastAck, lastAck + sendnum);
				ssthresh = cwnd / 2;
				cwnd = ssthresh + 3 * MSS;
				setCongestionState(CongestionState::fastRecovery);
			}
		} else {
			dupACK = 0;
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
	return milliseconds(max(rto, 1));
}

inline void RSend::setTimer(uint32_t seq, uint16_t size,uint8_t nTimeout,uint8_t flags)
{
	auto timeout = milliseconds(getRTO().count()<< nTimeout);
	timevts.push_back(Timevt(seq, size, timeout, nTimeout, flags));
	push_heap(timevts.begin(), timevts.end(), greater<Timevt>());
	printf("[LOG]: TIMER [%d,%d) in %lld ms, %d times,flag=%d.\n",seq,seq+size,timeout.count(), nTimeout,flags);
}

milliseconds RSend::handleTimer()
{
	if (timevts.empty()) return milliseconds(10);
	auto now = system_clock::now();
	while (now > timevts.front().tp) {
		processTimeout(timevts.front());
		pop_heap(timevts.begin(), timevts.end(), greater<Timevt>());
		timevts.pop_back();
		if (timevts.empty()) return milliseconds(10);
	}
	return duration_cast<milliseconds>(timevts.front().tp - now);
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

		//CON 拥塞控制
		ssthresh = cwnd / 2;
		cwnd = MSS;
		dupACK = 0;
		setCongestionState(CongestionState::start);

		setTimer(evt.dataSeq, evt.dataLen, evt.nTimeout + 1, evt.flag);
	}//else 否则已经被确认
}

void RSend::setState(State s)
{
	State old = state;
	state = s;
	printf("[LOG]: %s-->%s | rwnd=%d, nextSeq=%d, buffer=[%d,%d).\n",
		enum_name(old).data(), enum_name(s).data(),
		rwnd, nextSeq, buffer.begin(), buffer.end());
}

bool RSend::updateTimer(uint32_t seq, uint32_t ack)
{
	bool updated = false;
	for (auto& evt : timevts) {
		if (evt.acked) continue;
		if (evt.flag != RPkg::F_SEQ) continue;
		if (evt.dataSeq == seq && evt.dataSeq + evt.dataLen <= ack) {
			evt.acked = true;
			auto now = system_clock::now();
			updateRTT(evt.sendTimestamp, now);
			updated = true;
		}
	}
	return updated;
}

void RSend::updateRTT(time_point<system_clock> send, time_point<system_clock> back)
{
	auto newRTT = duration_cast<milliseconds>(back - send);
	double newRTTms = static_cast<double>(newRTT.count());
	double oldRTTms = static_cast<double>(rtt);
	double a = 0.125;
	double RTT = (1 - a) * oldRTTms + a * newRTTms;
	rtt = static_cast<uint32_t>(RTT);
	rto = 2 * rtt;
	printf("[LOG]: RTT rtt=%d,rto=%d.\n", rtt, rto);
}

void RSend::setCongestionState(CongestionState cs)
{
	//CON 拥塞控制
	CongestionState old = cState;
	cState = cs;
	printf("[LOG]: <Congestion> %s-->%s | rwnd=%d, cwnd=%d, ssthresh=%d.\n",
		enum_name(old).data(), enum_name(cs).data(),rwnd, cwnd, ssthresh);
}

//called when a new ack is received.
void RSend::updateCongestionWindow()
{
	//CON 拥塞控制
	printf("[LOG]: <Congestion> NEW_ACK when %s cwnd=%d ssthresh=%d, ", enum_name(cState).data(),cwnd, ssthresh);
	switch (cState) {
	case CongestionState::start:
		cwnd += MSS;
		printf("update cwnd=%d.\n", cwnd);
		if (cwnd > ssthresh) {
			printf("[LOG]: <Congestion> CWND_OVER cwnd=%d > ssthresh=%d.\n", cwnd, ssthresh);
			setCongestionState(CongestionState::avoid);
		}
		break;
	case CongestionState::avoid:
		cwnd += MSS * (MSS / cwnd);
		printf("update cwnd=%d.\n",cwnd);
		break;
	case CongestionState::fastRecovery:
		cwnd = ssthresh;
		dupACK = 0;
		printf("update cwnd=%d, dupACK=%d.\n", cwnd, dupACK);
		setCongestionState(CongestionState::avoid);
	}
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