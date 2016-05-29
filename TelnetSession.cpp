//
// Created by jan on 29.05.16.
//
#include <iostream>
#include <thread>
#include <unistd.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "TelnetSession.h"

TelnetSession::TelnetSession(int telnetsock) : sock(telnetsock) { }

void TelnetSession::send(std::string message) {
    ssize_t rc;
    message.append("\r\n");
    rc = write(sock, message.c_str(), message.size());
    if (rc < 0) {
        perror("TelnetSession send write");
        throw ConnectionClosed();
    }
}

// TODO read, parse, process
// sensowne wczytywanie danych
// to chyba jest ok
// czyszczenie bufora
void TelnetSession::listen() {
    bool cr = false;
    ssize_t rc;
    size_t len = 0;
    char buffer[BUFFER_SIZE + 1];
    for (; ;) {
        rc = read(sock, buffer + len, 1);
        if (rc < 0) {
            perror("TelnetSession process read");
            throw ConnectionClosed();
            //return;
        }
        else if (rc == 0) {
            fprintf(stderr, "Telnet client on socket %d closed connection\n", sock);
            throw ConnectionClosed();
            //return;
        }
        else if (len + 1 >= BUFFER_SIZE) {
            buffer[BUFFER_SIZE] = 0;
            len = 0;
            fprintf(stderr, "Telnet buffer exceeded, invalid command:\n%s\n", buffer);
            send("ERROR: Buffer exceeded, invalid command");
            // TODO wypisywanie błędów do sesji telnet
        }
        else if (buffer[len] == '\r') {
            cr = true;
        }
        else if (buffer[len] == '\n' || (buffer[len] == '\0' && cr)) {
            buffer[len + 1] = 0;
            len = 0;
            cr = false;
            parse_command(buffer);
        }
        else {
            cr = false;
            ++len;
        }
    }
}

void TelnetSession::finish(int id, int status) {
    auto it = PlayerSessions.find(id);
    if (it != PlayerSessions.end()) {
        PlayerSessions.erase(it);
        std::string s("Player " + std::to_string(id) + " finished with status " + std::to_string(status));
        send(s);
    }
    else {
        fprintf(stderr, "TelnetSession finish id: %d, status: %d\n", id, status);
    }
}

void TelnetSession::parse_command(char *command) {
    //cerr<<command<<endl;
    // START komputer host path r-port file m-port md
    static const boost::regex start_regex("START +(\\S+) +(\\S+ +\\S+ +\\d+ +(\\S+) +(\\d+) +(?:yes|no))\\s*");
    // AT HH.MM M komputer host path r-port file m-port md
    static const boost::regex at_regex(
            "AT +(\\d{2}\\.\\d{2}) +(\\d) +(\\S+) +(\\S+ +\\S+ +\\d+ +(\\S+) +(\\d+) +(?:yes|no))\\s*");
    // PAUSE | PLAY | QUIT  ID
    static const boost::regex command_regex("(PAUSE|PLAY|QUIT) +(\\d+)\\s*");
    // TITLE ID
    static const boost::regex title_regex("TITLE +(\\d+)\\s*");

    boost::cmatch match; // TODO podział na matche
    if (boost::regex_match(command, match, command_regex)) {
        send_command_to_player(match[1], match[2]);
    }
    else if (boost::regex_match(command, match, title_regex)) {

    }
    else if (boost::regex_match(command, match, start_regex)) {
        //cerr<<match[1]<<endl<<match[2]<<endl<<match[3]<<endl;
        if (match[3] == "-") {
            fprintf(stderr, "Invalid command: %s\n", command);
            send("ERROR: Invalid command");
        }
        else {
            start_ssh_session(match[1], match[4], match[2]);
        }
    }
    else if (boost::regex_match(command, match, at_regex)) {

    }
    else {
        fprintf(stderr, "Invalid command: %s\n", command);
        send("ERROR: Invalid command");
    }
}

void TelnetSession::send_command_to_player(std::string command, std::string id) {
    int id_int = boost::lexical_cast<int>(id);
    auto it = PlayerSessions.find(id_int);
    if (it == PlayerSessions.end()) {
        send("ERROR :" + id + "player not found");
    }
    else {
        it->second->send_command();
    }
}

void TelnetSession::start_ssh_session(std::string host, std::string port, std::string arguments) {
    std::shared_ptr<PlayerSession> player(new PlayerSession(next_id, this));
    try {
        player->init_socket(host, port);
    }
    catch (PlayerSession::PlayerException &ex) {
        fprintf(stderr, "%s\n", ex.what());
        send("ERROR: START " + host);
        return;
    }
    //PlayerSessions.emplace(std::make_pair(next_id, ssh));
    PlayerSessions.insert(std::make_pair(next_id, player));
    //std::thread(telnet_listen, telnet_sock).detach();
    std::thread(player_launch, player, host, arguments).detach();
    // TODO exception do thread
    send("OK " + std::to_string(next_id));
    ++next_id;
    //cerr<<player<< endl <<arguments<<endl;
    //PlayerSessions.emplace(std::make_pair(next_id, std::unique_ptr<PlayerSession>(new PlayerSession)));
    //PlayerSessions.insert({next_id, std::unique_ptr<PlayerSession>(new PlayerSession(next_id, player, arguments, this))});
    //PlayerSessions.emplace(std::make_pair(next_id, std::unique_ptr<PlayerSession>(new PlayerSession(next_id, player, arguments, this))));
}