#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <string>
#include <boost/regex.hpp>
#include <fcntl.h>
#include "err.h"

// ./player stream3.polskieradio.pl / 8900 - 10000 no
// echo -n "hello" > /dev/udp/localhost/10000

using namespace std;

const int QUEUE_LENGTH = 5;
const int HEADER_MAN_LENGTH = 100000;

int initialize_radio_socket(const char *host, const char *r_port) {
    int r_sock, rc;
    struct addrinfo addr_hints, *addr_result;

    r_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (r_sock < 0) {
        syserr("radio socket");
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
        syserr("radio connect");
    }
    freeaddrinfo(addr_result);

    return r_sock;
}

int initialize_message_socket(const char *m_port) {
    int m_sock, m_port_int, rc;
    struct sockaddr_in controller;

    m_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock < 0) {
        syserr("message socket");
    }

    if (boost::regex_match(m_port, boost::regex("\\d+"))) {
        m_port_int = atoi(m_port);
        if (m_port_int < 1024 || m_port_int > 65535) {
            fatal("Invalid port number: %d", m_port_int);
        }
    }
    else {
        fatal("Invalid port number: %s", m_port);
    }

    controller.sin_family = AF_INET;
    controller.sin_addr.s_addr = htonl(INADDR_ANY);
    controller.sin_port = htons((uint16_t) m_port_int);
    rc = bind(m_sock, (struct sockaddr *) &controller, (socklen_t) sizeof(controller));
    if (rc < 0) {
        syserr("bind");
    }

    return m_sock;
}

int initialize_output_file_descriptor(const char *file) {
    int fd = 1;
    if (file == "-") {
        fd = open(file, O_WRONLY | O_CREAT);
        if (fd < 0) {
            syserr("open");
        }
    }
    return fd;
}

void send_get_request(const int sock, const char *host, const char *path, const char *md) {
    int mdi;
    if (md == "no") mdi = 0;
    else if (md == "yes") mdi = 1;
    else fatal("md argument");
    char buffer[HEADER_MAN_LENGTH];
    int buflen = snprintf(buffer, HEADER_MAN_LENGTH - 1, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\nIcy-MetaData:%d\r\n\r\n", path, host, mdi);
    if (buflen < 0) {
        syserr("snprintf");
    }
    ssize_t rc = write(sock, buffer, (size_t) buflen);
    if (rc < 0) {
        syserr("get write");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 7) {
        fatal("Usage: %s host path r-port file m-port md", argv[0]);
    }

    const char *host = argv[1];
    const char *path = argv[2];
    const char *r_port = argv[3];
    const char *file = argv[4];
    const char *m_port = argv[5];
    const char *md = argv[6];

    int r_sock, m_sock, outfd;

    r_sock = initialize_radio_socket(host, r_port);
    m_sock = initialize_message_socket(m_port);
    outfd = initialize_output_file_descriptor(file);
    send_get_request(r_sock, host, path, md);
    receive_get_request(); // TODO string reszta, metaint


//    std::string get = "";
//    get += "GET / HTTP/1.0\r\n";
//    //get += "Host: 66.220.31.135\r\n";
//    get += "Host: stream3.polskieradio.pl\r\n";
//    //get += "User-Agent: Orban/Coding Technologies AAC/aacPlus Plugin 1.0 (os=Windows NT 5.1 Service Pack 2)\r\n";
//    get += "User-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\n";
//    //get += "Accept: */*\r\n";
//    //get += "Icy-MetaData:1\r\n";
//    get += "Icy-MetaData:0\r\n";
//    //get += "Connection: close\r\n";
//    get += "\r\n";
//
//    write(r_sock, get.c_str(), get.size());
//
    char buffer[100000];
//
//    int len = 0;
//    for (/*int num = 0; num < 1000; num += len*/;;) {
//        len = read(r_sock, buffer, 100000);
//        write(1, buffer, len);
//    }


//    for(;;) {
//        //int readlen = recv(m_sock, buffer, sizeof(buffer), 0);
//        int readlen = read(m_sock, buffer ,sizeof(buffer));
//        write(1, buffer, readlen);
//    }
}
