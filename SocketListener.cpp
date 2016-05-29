//
// Created by jan on 29.05.16.
//

#include <thread>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include "SocketListener.h"
#include "err.h"

SocketListener::SocketListener(int port) {
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

void SocketListener::acceptConnection() {
    int telnet_sock = accept(sock, (struct sockaddr *) NULL, (socklen_t *) NULL);
    if (telnet_sock < 0) {
        syserr("SocketListener accept accept");
    }
    std::thread(telnet_listen, telnet_sock).detach();
    //TelnetSessions.emplace(TelnetSession(telnetsock));
}