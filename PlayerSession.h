//
// Created by jan on 29.05.16.
//

#ifndef RADIO_PLAYERSESSION_H
#define RADIO_PLAYERSESSION_H

#include <memory>
#include "TelnetSession.h"

class PlayerSession {
public:
    PlayerSession(int id, TelnetSession *telnet_session);

    void init_socket(std::string host, std::string port);

    void send_command(std::string command);

    friend void player_launch(std::shared_ptr<PlayerSession>, std::string, std::string);

    class PlayerException: public std::exception {
    public:
        PlayerException(std::string s) : message(s) { }
        virtual const char* what() const throw() {
            return message.c_str();
        }
    private:
        std::string message;
    };

private:
    int id;
    int sock;
    struct sockaddr_in addr;
    std::shared_ptr<TelnetSession> telnet;
};

class DelayedPlayerSession : public PlayerSession {
public:
    DelayedPlayerSession(int id, TelnetSession *telnet_session) : PlayerSession(id, telnet_session) { }
};

void player_launch(std::shared_ptr<PlayerSession> session, std::string host, std::string arguments) {
    // TODO zmienić na player
    // ' EXIT STATUS $? '
    std::string command("ssh " + host + " './ClionProjects/radio/player " + arguments + "; echo $?'");
    FILE *fpipe = (FILE *) popen(command.c_str(), "r");
    if (fpipe == NULL) {
        perror("Problems with pipe");
    }
    else {
        char ret[256];
        fgets(ret, sizeof(ret), fpipe);
        //fprintf(stderr, "%s\n", ret);
        //cerr << "exit status" << ret << endl;
        // TODO parsowanie błedu, ERROR ID, ERROR message
        // boost regex ? lexical_cast ?
        /////// session->telnet->send("EXIT STATUS: " + std::string(ret));
        //session->telnet->finish(session->id, std::string(ret));
        pclose(fpipe);
    }
}


#endif //RADIO_PLAYERSESSION_H
