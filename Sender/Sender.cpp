// Sender.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <fstream>
#include <string>
#include "RSend.h"
#include <chrono>
#include "cmdline.h"

using namespace std;
using namespace chrono;
//C:\Users\A\source\repos\RUDP\x64\Debug

const string filepath = "C:/Users/A/Desktop/net/sendfile/";
char filebuffer[102400];


//TODO 伪首部
//TODO MSS设为10240不能保证可靠性
//TODO 丢包时速度问题
int main(int argc, char*argv[])
{
    //parse command line argument
    cmdline::parser argp;
    argp.add<string>("file", 'f', "the file to send", false, "1.jpg");
    argp.add<string>("addr", 'i', "the ip address to connect", false, "127.0.0.1");
    argp.add<int>("port", 'p', "port number", false, 12300, cmdline::range(1, 65535));
    argp.parse_check(argc, argv);
    string filename = argp.get<string>("file");
    string addr = argp.get<string>("addr");
    uint16_t port = argp.get<int>("port");

    //Open file
    ifstream infile = ifstream(filepath+filename, ios::binary);
    if (!infile.is_open()) {
        cerr << "[ERR]:Failed to open thr file:" << filepath+filename << endl;
        exit(EXIT_FAILURE);
    }
    cout << "[LOG]: succeed to open thr file:" << filepath+filename << endl;
    //Open connect
    RSend sender(addr,port);
    sender.send(filename.c_str(), filename.size() + 1);
    int total = filename.size() + 1;
    //start to send
    auto start = system_clock::now();

    while (!infile.eof()) {
        infile.read(filebuffer, sizeof(filebuffer));
        size_t nget = infile.gcount();
        sender.send(filebuffer,nget);
        total += nget;
    }
    sender.close();
    //send end
    auto end = system_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    double spendSec = double(duration.count()) * microseconds::period::num / microseconds::period::den;
    printf("send file spent %f s.\n", spendSec);
    printf("%d bytes in total. speed = %d bytes ps\n", total, int(total /spendSec));
    infile.close();
    printf("success to send the file: %s.\n", filename.c_str());
}