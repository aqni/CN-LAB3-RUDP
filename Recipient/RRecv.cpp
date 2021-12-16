#include "RRecv.h"
#include <chrono>
#include "magic_enum.hpp"


using namespace std;
using namespace chrono;
using namespace magic_enum;

RRecv::RRecv(const IPv4Addr& addr, uint16_t port)
	:myAddr(addr),myPort(port),buffer(Size::BYTE_32K),socket(),state(State::closed), mgrThrd()
{
	this->rPkgBuf = new char[maxPkgSize()];
	this->socket.bind(myAddr,myPort);
	setState(State::listening);
	thread temp(&RRecv::manager, this);
	mgrThrd.swap(temp);
}

RRecv::~RRecv()
{
	delete[] rPkgBuf;
}

uint32_t RRecv::recv(char* buf, uint32_t size)
{
	uint32_t num = 0;
	while (num == 0 && state.load() != State::closed) {
		num= buffer.pop(buf, size);
	}
	printf("[LOG]: <USER> recv %d bytes.\n",num);
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
	printf("[LOG]: START manager thread start.\n");
	while (true) {
		State oldState = state.load();
		RPkg* rPkg = new(rPkgBuf) RPkg();
		switch (oldState) {
		default:
			printf("[ERR]: unexpected state(%s)!\n", magic_enum::enum_name(oldState).data());
			exit(EXIT_FAILURE);
		case State::closed:
			break;
		case State::listening:
		case State::recv:
			rPkg = recvPkg();
			printf("[LOG]: RECV [%d,%d), flags=%d.\n",rPkg->getSEQ(), rPkg->getSEQ()+rPkg->getBodySize(),rPkg->flags);
			if (rPkg->flags & RPkg::F_SYN) {
				nextACK = rPkg->getSEQ();
				sendAck(nextACK, getWindow(), rPkg->getSEQ());
				setState(State::recv);
			}
			if (rPkg->flags & RPkg::F_SEQ) {
				deliverData(rPkg->getSEQ(),rPkg->getBodySize(),rPkg->data);
				sendAck(nextACK, getWindow(), rPkg->getSEQ());
				setState(State::recv);
			}
			if (rPkg->flags & RPkg::F_FIN) {
				nextACK = rPkg->getSEQ() + rPkg->getBodySize();
				sendAck(nextACK, getWindow(), rPkg->getSEQ());
				setState(State::closed);
			}
			continue;
		}
		break;
	}
	printf("[LOG]: END manager thread end.\n");
}

void RRecv::setState(State s)
{
	State old=state.exchange(s);
	printf("[LOG]: %s-->%s | rwnd=%d, nextACK=%d, buffer=[%d,%d).\n",
		enum_name(old).data(),enum_name(s).data(),
		getWindow(),nextACK,buffer.begin(),buffer.end());
}

RPkg* RRecv::recvPkg()
{
	IPv4Addr sourceIP;
	uint16_t sourcePort;
	RPkg* rPkg = reinterpret_cast<RPkg*>(rPkgBuf);
	auto timeout = milliseconds(10000);
	while (true) {
		auto rnum = socket.recvfrom(rPkgBuf, maxPkgSize(), sourceIP, sourcePort, timeout);
		if (rnum == 0) {
			printf("[LOG]: TIMEOUT recv timeout: %lld ms.\n", timeout.count());
			continue;
		}
		if (rnum != rPkg->size()) {
			printf("[ERR]: recv %d bytes, but pkssize=%d.\n", rnum, rPkg->size());
			exit(EXIT_FAILURE);
		}
		if (targetPort == 0) { //如果是第一次连接，则设置对方地址端口
			targetAddr.addr = sourceIP.addr;
			targetPort = sourcePort;
			printf("[LOG]: set target: %s:%d.\n",targetAddr.to_string().c_str(), targetPort);
		}
		if (sourceIP.addr != targetAddr.addr || sourcePort != targetPort) { //必须是对方的地址端口发送的UDP数据
			printf("[LOG]: recv from %s:%d, but target is %s:%d.\n",
				sourceIP.to_string().c_str(), sourcePort, targetAddr.to_string().c_str(), targetPort);
			continue;
		}
		if (uint16_t checksum= rPkg->calCheckSum(); checksum != 0 && rPkg->checkSum != 0) { //计算校验和
			printf("[LOG]: recv error pkg :checksum=%d\n", checksum);
			continue;
		}
		if (size_t size = rPkg->size(); rnum != size) {
			printf("[LOG]: recv %zu bytes, but pkg size=%zu.\n", rnum, size);
			continue;
		}
		break;
	}
	return rPkg;
}

void RRecv::sendAck(uint32_t ack, uint16_t window,uint32_t seq)
{
	RPkg sPkg;
	sPkg.makeACK(nextACK, getWindow(), seq);
	sPkg.setCheckSum(sPkg.calCheckSum());
	socket.sendto((char*)&sPkg, sPkg.size(), targetAddr, targetPort);
	printf("[LOG]: SEND ACK ,ack=%d, rwnd=%d.\n", sPkg.getACK(), sPkg.getWindow());
}

//不支持超过4GB
void RRecv::deliverData(uint32_t seq, uint16_t size, const char* data)
{
	if (seq <= nextACK) { //push data into buffer
		uint32_t end = seq + size;
		if (end <= nextACK) return;
		uint32_t dataOffset = nextACK - seq;
		uint32_t wSize = end - nextACK;
		if (!cacheRanges.empty()) {
			uint32_t nextCachedBegin = cacheRanges.front().first;
			wSize = min(wSize, nextCachedBegin - nextACK);
		}
		uint32_t pushnum=buffer.push(data+dataOffset,wSize);
		nextACK += pushnum;
		printf("[LOG]: PUSH %d bytes into buffer, buffer:[%d,%d).\n",
			pushnum,buffer.begin(), buffer.end());
		uint32_t oldACK = nextACK;
		while (!cacheRanges.empty()) {  //合并缓存
			uint32_t nextCachedBegin = cacheRanges.front().first;
			uint32_t nextCachedEnd = cacheRanges.front().second;
			if (nextACK < nextCachedBegin) break;
			if (nextACK < nextCachedEnd) {
				nextACK = nextCachedEnd;
			}
			cacheRanges.pop_front();
			printf("[LOG]: MERGE cache[%d,%d), nextack:%d.\n", nextCachedBegin, nextCachedEnd, nextACK);
		}
		pushnum = buffer.push(nextACK - oldACK);
		assert(pushnum == nextACK - oldACK);
		printf("[LOG]: PUSH %d bytes into buffer, buffer:[%d,%d).\n",
			pushnum, buffer.begin(), buffer.end());

	}
	else { //cache data in buffer
		auto iter = cacheRanges.begin();
		while (iter->first < seq) iter++;
		auto range = make_pair(seq, min(seq + size, iter->first));
		uint32_t setnum = buffer.set(range.first, data, range.second - range.first);
		printf("[LOG]: CACHE %d bytes in rwnd:[%d,%d).\n", setnum, range.first, range.second);
		range.second = range.first + setnum;
		cacheRanges.insert(iter, range);
	}
}

