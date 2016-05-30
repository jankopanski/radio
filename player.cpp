#include <iostream>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "err.h"

// ./player stream3.polskieradio.pl / 8900 - 10000 no
// ./player stream3.polskieradio.pl / 8900 - 10000 no | mplayer -
// ./player stream3.polskieradio.pl / 8900 - 10000 no | mplayer -cache 1024 -
// echo -n "hello" > /dev/udp/localhost/10000

using namespace std; // TODO usunąć

const int HEADER_MAX_LENGTH = 100000;
const int TIME = 5000;

void quit(int fd) {
    if (fd > 2) {
        if (close(fd) < 0) {
            syserr("quit close");
        }
    }
    exit(0);
}

class Radio {
public:
    Radio(int r_sock, int outfd, int metaint, bool metadata) : in(r_sock), out(outfd), audiolen(metaint),
                                                               metadata(metadata) {
        buffer_size = metadata ? min(max(audiolen, 4080) + 1, MAX_BUFFER_SIZE) : MAX_BUFFER_SIZE; // TODO statyczny buffer
        buffer = (char *) malloc((size_t) buffer_size);
        if (buffer == NULL) {
            syserr("Radio malloc");
        }
    }

    ~Radio() {
        free(buffer);
    }

    void process() {
        if (metadata) {
            switch (state) {
                case audio:
                    readlen = read(in, buffer, (size_t) min(audiolen - audioread, buffer_size));
                    if (readlen < 0) {
                        syserr("process read");
                    }
                    else if (readlen == 0) {
                        quit(out);
                    }
                    if (active) {
                        writelen = write(out, buffer, (size_t) readlen);
                        if (writelen < 0) {
                            syserr("process write");
                        }
                    }
                    audioread += readlen;
                    if (audioread == audiolen) {
                        audioread = 0;
                        if (metadata) state = byte;
                    }
                    break;
                case byte:
                    readlen = read(in, buffer, 1);
                    if (readlen < 0) {
                        syserr("process read");
                    }
                    else if (readlen == 0) {
                        quit(out);
                    }
                    metalen = static_cast<int>(buffer[0]) * 16;
                    if (metalen == 0) state = audio;
                    else state = meta;
                    break;
                case meta:
                    readlen = read(in, buffer + metaread, (size_t) (metalen - metaread)); // TODO metadane mogą się przepełnić
                    if (readlen < 0) {
                        syserr("process read");
                    }
                    else if (readlen == 0) {
                        quit(out);
                    }
                    metaread += readlen;
                    if (metaread == metalen) {
                        buffer[metalen] = 0;
                        boost::cmatch what;
                        boost::regex_search(buffer, what, title_regex); // TODO boost regex exceptions
                        title_ = what[1];
                        metaread = 0;
                        state = audio;
                    }
                    break;
                default:
                    fatal("process");
            }
        }
        else {
            readlen = read(in, buffer, (size_t) buffer_size);
            if (readlen < 0) {
                syserr("process read");
            }
            else if (readlen == 0) {
                quit(out);
            }
            if (active) {
                writelen = write(out, buffer, (size_t) readlen);
                if (writelen < 0) {
                    syserr("process write");
                }
            }
        }
    }

    void pause() {
        active = false;
    }

    void play() {
        active = true;
    }

    std::string title() {
        return title_;
    }

private:
    enum State {
        audio, byte, meta
    };

    State state = audio;
    bool active = true;
    const int in;
    const int out;
    const int audiolen;
    //const static int MAX_BUFFER_SIZE = 8192;
    const int MAX_BUFFER_SIZE = 8192;
    const bool metadata;
    int metalen = 0;
    int audioread = 0;
    int metaread = 0;
    int buffer_size;
    ssize_t readlen;
    ssize_t writelen;
    char *buffer;
    std::string title_ = "";
    const boost::regex title_regex{"StreamTitle='([^;]*)';"};
};

int initialize_radio_socket(const char *host, const char *r_port) {
    int r_sock, rc;
    struct addrinfo addr_hints, *addr_result;

    r_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (r_sock < 0) {
        syserr("initialize_radio_socket socket");
    }

    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    rc = getaddrinfo(host, r_port, &addr_hints, &addr_result);
    if (rc == EAI_SYSTEM) {
        syserr("initialize_radio_socket getaddrinfo: %s", gai_strerror(rc));
    }
    else if (rc != 0) {
        fatal("initialize_radio_socket getaddrinfo: %s", gai_strerror(rc));
    }

    rc = connect(r_sock, addr_result->ai_addr, addr_result->ai_addrlen);
    if (rc < 0) {
        syserr("initialize_radio_socket connect");
    }
    freeaddrinfo(addr_result);

    return r_sock;
}

int initialize_message_socket(const char *m_port) {
    int m_sock, m_port_int, rc;
    struct sockaddr_in controller;

    m_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock < 0) {
        syserr("initialize_message_socket socket");
    }

    if (boost::regex_match(m_port, boost::regex("\\d+"))) {
        m_port_int = atoi(m_port); // TODO lexical_cast
        if (m_port_int < 1024 || m_port_int > 65535) {
            fatal("invalid port number: %d", m_port_int);
        }
    }
    else {
        fatal("invalid port number: %s", m_port);
    }

    controller.sin_family = AF_INET;
    controller.sin_addr.s_addr = htonl(INADDR_ANY);
    controller.sin_port = htons((uint16_t) m_port_int);
    rc = bind(m_sock, (struct sockaddr *) &controller, (socklen_t) sizeof(controller));
    if (rc < 0) {
        syserr("initialize_message_socket bind");
    }

    return m_sock;
}

int initialize_output_file_descriptor(const char *file) {
    int fd = 1;
    if (strcmp(file, "-") != 0) {
        fd = open(file, O_WRONLY | O_CREAT, S_IRWXU);
        if (fd < 0) {
            syserr("initialize_output_file_descriptor open");
        }
    }
    return fd;
}

void send_get_request(const int sock, const char *host, const char *path, const bool metadata) {
    char buffer[HEADER_MAX_LENGTH];
    int buflen = snprintf(buffer, HEADER_MAX_LENGTH - 1,
                          "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\nIcy-MetaData:%d\r\n\r\n",
                          path, host, (int) metadata);
    if (buflen < 0) {
        syserr("send_get_request snprintf");
    }
    ssize_t rc = write(sock, buffer, (size_t) buflen);
    if (rc < 0) {
        syserr("send_get_request write");
    }
}

int receive_get_request(const int sock, const bool metadata) {
    int state = 0, len = 0;
    ssize_t rc;
    char buffer[HEADER_MAX_LENGTH];
    while (state < 4) {
        rc = read(sock, buffer + len, 1);
        if (rc < 0) {
            syserr("receive_get_request read");
        }
        if ((buffer[len] == '\r' && (state == 0 || state == 2)) ||
            (buffer[len] == '\n' && (state == 1 || state == 3)))
            ++state;
        else state = 0;
        ++len;
        if (len >= HEADER_MAX_LENGTH) {
            fatal("buffer overflow");
        }
    }
    boost::regex header;
    boost::cmatch match;
    if (metadata) {
        header = boost::regex("(?:ICY|HTTP/1.0|HTTP/1.1) (\\d{3}).*\r\n.*icy-metaint:(\\d+)\r\n.*");
    }
    else {
        header = boost::regex("(?:ICY|HTTP/1.0|HTTP/1.1) (\\d{3}).*\r\n.*");
    }
    if (!boost::regex_match(buffer, match, header)) {
        fatal("get response %s", buffer);
    }
    int status = boost::lexical_cast<int>(match[1]);
    if (!(status == 200 || status == 302 || status == 304)) { // TODO kody statusów
        fatal("get response status %d", status);
    }
    if (metadata) {
        return boost::lexical_cast<int>(match[2]);
    }
    else return 0;
}

void process_command(int sock, int out, Radio &radio) {
    char buffer[6];
    ssize_t recvlen;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    recvlen = recvfrom(sock, buffer, 6, 0, (struct sockaddr *) &addr, &addrlen);
    cerr<<buffer<<endl;
    //assert(strcmp(buffer, "QUIT") == 0);
    assert(buffer[0] == 'Q');
    assert(buffer[1] == 'U');
    assert(buffer[2] == 'I');
    assert(buffer[3] == 'T');
    assert(buffer[4] == '\0');
    if (recvlen < 0) {
        syserr("process_command recvfrom");
    }
    else if (recvlen == 4) {
        buffer[4] = 0;
        if (strcmp(buffer, "PLAY") == 0) {
            radio.play();
        }
        else if (strcmp(buffer, "QUIT") == 0) {
            quit(out);
        }
        else fprintf(stderr, "invalid command: %s\n", buffer);
    }
    else if (recvlen == 5) {
        buffer[5] = 0;
        if (strcmp(buffer, "PAUSE") == 0) {
            radio.pause();
        }
        else if (strcmp(buffer, "TITLE") == 0) {
            std::string title = radio.title();
            ssize_t sendlen = sendto(sock, title.c_str(), title.size(), 0, (struct sockaddr *) &addr, addrlen);
            if (sendlen < 0) {
                syserr("process_command sendto");
            }
        }
        else fprintf(stderr, "invalid command: %s\n", buffer);
    }
    else fprintf(stderr, "invalid command: %s\n", buffer);
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

    bool metadata = false;
    if (strcmp(md, "yes") == 0) metadata = true;
    else if (strcmp(md, "no") != 0) fatal("md argument");


    int r_sock, m_sock, outfd;

    r_sock = initialize_radio_socket(host, r_port);

    m_sock = initialize_message_socket(m_port);

    outfd = initialize_output_file_descriptor(file);

    send_get_request(r_sock, host, path, metadata);

    int metaint = receive_get_request(r_sock, metadata);

    Radio radio = Radio(r_sock, outfd, metaint, metadata);

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
            radio.process();
        }
        if (polls[1].revents == POLLIN) {
            process_command(m_sock, outfd, radio);
        }
    }
}
