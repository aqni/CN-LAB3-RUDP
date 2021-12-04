// Recipient.cpp
//
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include "RRecv.h"
#include "cmdline.h"

using namespace std;
//C:\Users\A\source\repos\RUDP\x64\Debug

const string outpath = "C:/Users/A/Desktop/net/recvfile/";
char filebuffer[102400] = {0};

int main(int argc, char* argv[])
{
    //parse command line argument
    cmdline::parser argp;
    argp.add<string>("addr", 'i', "the ip address to connect", false, "127.0.0.1");
    argp.add<int>("port", 'p', "port number", false, 10101, cmdline::range(1, 65535));
    argp.parse_check(argc, argv);

    string addr = argp.get<string>("addr");
    uint16_t port = argp.get<int>("port");

    //start to recv file
    RRecv reci(addr, port);
    auto recvnum = reci.recv(filebuffer, sizeof(filebuffer) - 1);
    int n = strlen(filebuffer)+1;
    string filename(filebuffer);
    //open the output file.
    ofstream outfile = ofstream(outpath+filename, ios::binary);
    if (!outfile.is_open()) {
        cerr << "Failed to open thr file:" << outpath+ filename << endl;
        exit(EXIT_FAILURE);
    }
    outfile.write(filebuffer+n, recvnum-n);
    while (true) {
        auto recvnum=reci.recv(filebuffer,sizeof(filebuffer)-1);
        if (recvnum == 0)break;
        outfile.write(filebuffer, recvnum);
    }

    reci.close();
    printf("succeed to close connect.\n");
    outfile.close();
    printf("recv file successfully, file:%s\n", (outpath + filename).c_str());
}

