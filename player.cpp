#include <iostream>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <boost/regex.hpp>
#include "err.h"

// ./player stream3.polskieradio.pl / 8900 - 10000 no
// echo -n "hello" > /dev/udp/localhost/10000

using namespace std;

const int QUEUE_LENGTH = 5;
const int HEADER_MAN_LENGTH = 100000;
const int TIME = 5000;

class Radio {
public:
    Radio(int r_sock, int outfd, int metaint, bool metadata) : in(r_sock), out(outfd), audiolen(metaint), metadata(metadata) {
        buffer = (char *) malloc(max((size_t) audiolen, 4080));
        if (audiobuffer == NULL) {
            syserr("malloc");
        }
    }

    ~Radio() {
        free(audiobuffer);
    }

    void process() {
        switch (state) {
            case audio:
                break;
            case byte:
                break;
            case meta:
                break;
            default:
                fatal("radio process");
        }
    }

    void pause() {
        active = false;
    }

    void play() {
        active = true;
    }

    std::string title() {
        // TODO zera wewnątrz tytułu, dodać długość tytułu
        return std::string(title_);
    }

private:
    enum State {audio, byte, meta};

    State state = audio;
    bool active = true;
    const bool metadata;
    const int in;
    const int out;
    const int audiolen;
    int metalen;
    int audioread;
    int metaread;
    char metalenbyte;
    char *audiobuffer;
    char metabuffer[4080];
    char title_[1000];

};

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

void send_get_request(const int sock, const char *host, const char *path, const bool metadata) {
    char buffer[HEADER_MAN_LENGTH];
    int buflen = snprintf(buffer, HEADER_MAN_LENGTH - 1,
                          "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\nIcy-MetaData:%d\r\n\r\n",
                          path, host, (int) metadata);
    if (buflen < 0) {
        syserr("snprintf");
    }
    ssize_t rc = write(sock, buffer, (size_t) buflen);
    if (rc < 0) {
        syserr("get write");
    }
}

int receive_get_request(const int sock) {
    int rc, state = 0, len = 0;
    char buffer[HEADER_MAN_LENGTH];
    while (state < 4) {
        rc = read(sock, buffer + len, 1);
        if (rc < 0) {
            syserr("read");
        }
        if ((buffer[len] == '\r' && (state == 0 || state == 2)) ||
            (buffer[len] == '\n' && (state == 1 || state == 3)))
            ++state;
        else state = 0;
        ++len;
    }
    boost::regex header("ICY.*(\\d{3}).*\r\n.*icy-metaint:(\\d+)\r\n.*");
    boost::smatch token;
    if (!boost::regex_match(buffer, token, header)) {
        fatal("Invalid get response %s", header);
    }
    int status = atoi(token[1]);
    if (!(status == 200 || status == 302 || status == 304)) {
        fatal("get response status %d", status);
    }
    return atoi(token[2]);
}

//void receive_get_request(const int sock, int &metaint, string &rest) {
//    const int CHUNK_SIZE = 1024;
//    int bufread = 0;
//    char buffer[HEADER_MAN_LENGTH];
//    memset(buffer, 0, HEADER_MAN_LENGTH);
//    boost::regex end(".*\r\n\r\n.*"); // TODO boost wyjątki
//    do {
//        if (bufread + CHUNK_SIZE > HEADER_MAN_LENGTH) {
//            fatal("HEADER_MAN_LENGTH exceeded");
//        }
//        ssize_t len = read(sock, buffer + bufread, CHUNK_SIZE);
//        if (len < 0) {
//            syserr("read");
//        }
//        // TODO len == 0
//        bufread += len;
//    } while (boost::regex_match(buffer, end));
//    boost::regex header("ICY.*(\\d{3}).*\r\n.*icy-metaint:(\\d+)\r\n.*\r\n\r\n(.*)");
//    boost::smatch token;
//    if (!boost::regex_match(buffer, token, header)) {
//        fatal("Invalid get response %s", header);
//    }
//    int status = atoi(token[1]);
//    if (!(status == 200 || status == 302 || status == 304)) {
//        fatal("status %d", status);
//    }
//    metaint = atoi(token[2]);
//    rest = string(token[3]);
//}

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

    bool metadata;
    if (md == "no") metadata = false;
    else if (md == "yes") metadata = true;
    else fatal("md argument");


    int r_sock, m_sock, outfd;

    r_sock = initialize_radio_socket(host, r_port);

    m_sock = initialize_message_socket(m_port);

    outfd = initialize_output_file_descriptor(file);

    send_get_request(r_sock, host, path, metadata);

    int metaint = receive_get_request(r_sock);

    Radio radio = Radio(r_sock, metaint, metadata);

    struct pollfd polls[2];
    polls[0].fd = r_sock;
    polls[1].fd = m_sock;
    polls[0].events = polls[1].events = POLLIN;

    for (; ;) {
        polls[0].revents = polls[1].revents = 0;
        if (poll(polls, 2, TIME) < 0) {
            syserr("poll");
        }
        if (polls[0].revents == POLLIN) {

        }
        if (polls[1].revents == POLLIN) {

        }
    }


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
