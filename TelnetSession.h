//
// Created by jan on 29.05.16.
//

#ifndef RADIO_TELNETSESSION_H
#define RADIO_TELNETSESSION_H

#include <unordered_map>
#include "PlayerSession.h"

class TelnetSession {
public:
    TelnetSession(int telnetsock);

    void send(std::string message);

    void listen();

    void finish(int id, int status);

    class ConnectionClosed: public std::exception {
    public:
        virtual const char* what() const throw()
        {
            return "Connection closed";
        }
    };

private:
    static const int BUFFER_SIZE = 1024;
    static const ConnectionClosed ex;
    int sock;
    int next_id = 1;
    std::unordered_map<int, std::shared_ptr<PlayerSession>> PlayerSessions;

    void parse_command(char *command);

    void send_command_to_player(std::string command, std::string id);

    void start_ssh_session(std::string host, std::string port, std::string arguments);
};

void telnet_listen(int telnet_sock) {
    //cerr << telnet_sock << endl;
    TelnetSession telnet(telnet_sock);
    try {
        telnet.listen();
    }
    catch(TelnetSession::ConnectionClosed &ex) {
        fprintf(stderr, "%s\n", ex.what());
    }
}

#endif //RADIO_TELNETSESSION_H
