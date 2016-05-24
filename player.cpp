#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <string>
#include "err.h"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 7) {
        fatal("Usage: %s host path r-port file m-port md", argv[0]);
    }

    char *host = argv[1];
    char *path = argv[2];
    char *r_port = argv[3];
    char *file = argv[4];
    char *m_port = argv[5];
    char *md = argv[6];

    int r_sock, m_sock;
    int rc;
    struct addrinfo addr_hints, *addr_result;

    r_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (r_sock < 0) {
        syserr("socket");
    }

    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    rc = getaddrinfo(host, r_port, &addr_hints, &addr_result);
    if (rc == EAI_SYSTEM) {
        syserr("getaddrinfo: %s", gai_strerror(rc));
    }
    else if (rc != 0) {
        fatal("getaddrinfo: %s", gai_strerror(rc));
    }

    rc = connect(r_sock, addr_result->ai_addr, addr_result->ai_addrlen);
    if (rc != 0) {
        syserr("connect");
    }

    // UDP

    freeaddrinfo(addr_result);

    string get = "";
    get += "GET / HTTP/1.0\r\n";
    //get += "Host: 66.220.31.135\r\n";
    get += "Host: stream3.polskieradio.pl\r\n";
    //get += "User-Agent: Orban/Coding Technologies AAC/aacPlus Plugin 1.0 (os=Windows NT 5.1 Service Pack 2)\r\n";
    get += "User-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\n";
    //get += "Accept: */*\r\n";
    //get += "Icy-MetaData:1\r\n";
    get += "Icy-MetaData:0\r\n";
    //get += "Connection: close\r\n";
    get += "\r\n";

    write(r_sock, get.c_str(), get.size());

    char buffer[100000];

    int len = 0;
    for (/*int num = 0; num < 1000; num += len*/;;) {
        len = read(r_sock, buffer, 100000);
        write(1, buffer, len);
    }
}
