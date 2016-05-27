#include <iostream>
#include <thread>
#include <netdb.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "err.h"

using namespace std;

void telnet_listen(int telnet_sock) {
    cerr << telnet_sock << endl;
}

int parse_port_number(char *port) {
    static boost::regex port_regex("\\d+");
    if (!boost::regex_match(port, port_regex)) {
        fatal("Invalid port number %s", port);
    }
    int port_num = boost::lexical_cast<int>(port);
    if (port_num < 1024 || port_num > 65535) {
        fatal("Invalid port number %d", port_num);
    }
    return port_num;
}

// TODO sensowna obsługa wyjątków
class SocketListener {
public:
    SocketListener(int port) {
        int rc;
        struct sockaddr_in master;

        sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            syserr("SocketListener socket");
        }

        master.sin_family = AF_INET;
        master.sin_addr.s_addr = htonl(INADDR_ANY);
        master.sin_port = htons((uint16_t) port);
        rc = bind(sock, (struct sockaddr *) &master, sizeof(master));
        if (rc < 0) {
            syserr("SocketListener bind");
        }

        if (port == 0) {
            socklen_t len = (socklen_t) sizeof(master);
            rc = getsockname(sock, (struct sockaddr *) &master, &len);
            if (rc < 0) {
                syserr("SocketListener getsockname");
            }
            printf("Listening at port %d\n", (int) ntohs(master.sin_port));
        }

        rc = listen(sock, BACKLOG);
        if (rc < 0) {
            syserr("SocketListener listen");
        }
    }

    void acceptConnection() {
        int telnet_sock = accept(sock, (struct sockaddr *) NULL, (socklen_t *) NULL);
        if (telnet_sock < 0) {
            syserr("SocketListener accept accept");
        }
        std::thread(telnet_listen, telnet_sock).detach();
        //TelnetSessions.emplace(TelnetSession(telnetsock));
    }

private:
    static const int BACKLOG = 10;
    int sock;
    //set<std::shared_ptr<TelnetSession>> TelnetSessions;
};

class TelnetSession {
public:
    TelnetSession(int telnetsock) : sock(telnetsock) {

    }

private:
    int sock;
};

class SshSession {

};

class DelayedSshSesion : public SshSession {

};

int main(int argc, char *argv[]) {
    int port = 0;

    if (argc == 2) {
        port = parse_port_number(argv[1]);
    }
    else if (argc > 2){
        fatal("Usage: %s [port]", argv[0]);
    }

    SocketListener socket(port);

    for (; ;) {
        socket.acceptConnection();
    }
}