//
// Created by jan on 29.05.16.
//

#ifndef RADIO_SOCKETLISTENER_H
#define RADIO_SOCKETLISTENER_H


#include "TelnetSession.h"

class SocketListener {
public:
    SocketListener(int port);

    void acceptConnection();

private:
    static const int BACKLOG = 10;
    int sock;
};


#endif //RADIO_SOCKETLISTENER_H
