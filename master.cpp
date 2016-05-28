#include <iostream>
#include <unordered_map>
#include <thread>
#include <netdb.h>
#include <sys/socket.h>
#include <libssh/libssh.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "err.h"

using namespace std;

class SshSession;
class TelnetSession;
void telnet_listen(int);
void ssh_exit(shared_ptr<SshSession>);

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

class SshSession {
public:
    SshSession(int id, TelnetSession *telnet_session) : id(id), master(telnet_session) { }

    ssh_channel getChannel() {
        return channel;
    }

    void start(std::string player, std::string arguments) {
        int rc;
        session = ssh_new();
        // TODO ssh_free(session)
        // ssh_disconnect
        if (session == NULL) throw SshException("start ssh_new " + player);
        ssh_options_set(session, SSH_OPTIONS_HOST, player.c_str());
        rc = ssh_connect(session);
        if (rc != SSH_OK) throw SshException("start ssh_connect " + player);
        rc = ssh_userauth_publickey_auto(session, NULL, NULL);
        if (rc != SSH_AUTH_SUCCESS) throw SshException("start ssh_userauth_publickey_auto " + player);
        channel = ssh_channel_new(session);
        if (channel == NULL) throw SshException("start ssh_channel_new " + player);
        rc = ssh_channel_open_session(channel);
        if (rc != SSH_OK) throw SshException("start ssh_channel_open_session " + player);
        std::string command = "./player " + arguments;
        rc = ssh_channel_request_exec(channel, command.c_str());
        if (rc != SSH_OK) throw SshException("start ssh_channel_request_exec " + player);
    }

    friend void ssh_exit(shared_ptr<SshSession>);

//    SshSession(int id, std::string player, std::string arguments, TelnetSession *telnetSession) : id(id), master(telnetSession) {
//
//    }
    class SshException: public std::exception {
    public:
        SshException(std::string s) : message(s) { }
        virtual const char* what() const throw() {
            return message.c_str();
        }
    private:
        std::string message;
    };

private:
    int id;
    ssh_session session;
    ssh_channel channel;
    shared_ptr<TelnetSession> master;
};

class DelayedSshSesion : public SshSession {
public:
    DelayedSshSesion(int id, TelnetSession *telnet_session) : SshSession(id, telnet_session) { }
};

class TelnetSession {
public:
    TelnetSession(int telnetsock) : sock(telnetsock) { }

    void send(std::string message) {
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
    void listen() {
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
//        char buffer[1024];
//        ssize_t len = read(sock, buffer, sizeof(buffer));
//        cerr<<len<<endl;
    }

    void finish(int id, int status) {
        auto it = SshSessions.find(id);
        if (it != SshSessions.end()) {
            SshSessions.erase(it);
            std::string s("Player " + std::to_string(id) + " finished with status " + std::to_string(status));
            send(s);
        }
        else {
            fprintf(stderr, "TelnetSession finish id: %d, status: %d\n", id, status);
        }
    }

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
    std::unordered_map<int, shared_ptr<SshSession>> SshSessions;

    void parse_command(char *command) {
        //cerr<<command<<endl;
        // START komputer host path r-port file m-port md
        static const boost::regex start_regex("START +(\\S+) +(\\S+ +\\S+ +\\d+ +\\S+ +\\d+ +(?:yes|no))\\s*");
        // AT HH.MM M komputer host path r-port file m-port md
        static const boost::regex at_regex("AT +(\\d{2}\\.\\d{2}) +(\\d) +(\\S+) +(\\S+ +\\S+ +\\d+ +\\S+ +\\d+ +(?:yes|no))\\s*");
        // PAUSE | PLAY | QUIT  ID
        static const boost::regex command_regex("(PAUSE|PLAY|QUIT) +(\\d+)\\s*");
        // TITLE ID
        static const boost::regex title_regex("TITLE +(\\d+)\\s*");

        boost::cmatch match; // TODO podział na matche
        if (boost::regex_match(command, match, command_regex)) {

        }
        else if (boost::regex_match(command, match, title_regex)) {

        }
        else if (boost::regex_match(command, match, start_regex)) {
            start_ssh_session(match[1], match[2]);
        }
        else if (boost::regex_match(command, match, at_regex)) {

        }
        else {
            fprintf(stderr, "Invalid command: %s\n", command);
            send("ERROR: Invalid command");
        }
    }

    void start_ssh_session(std::string player, std::string arguments) {
        std::shared_ptr<SshSession> ssh(new SshSession(next_id, this));
        try {
            ssh->start(player, arguments);
        }
        catch(SshSession::SshException &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send("ERROR: START " + player);
            return;
        }
        //SshSessions.emplace(std::make_pair(next_id, ssh));
        SshSessions.insert(std::make_pair(next_id, ssh));
        //std::thread(telnet_listen, telnet_sock).detach();
        std::thread(ssh_exit, ssh).detach();
        // TODO exception o thread
        send("OK " + std::to_string(next_id));
        ++next_id;
        //cerr<<player<< endl <<arguments<<endl;
        //SshSessions.emplace(std::make_pair(next_id, std::unique_ptr<SshSession>(new SshSession)));
        //SshSessions.insert({next_id, std::unique_ptr<SshSession>(new SshSession(next_id, player, arguments, this))});
        //SshSessions.emplace(std::make_pair(next_id, std::unique_ptr<SshSession>(new SshSession(next_id, player, arguments, this))));
    }
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

void ssh_exit(shared_ptr<SshSession> session) {
    int status = ssh_channel_get_exit_status(session->channel);
    session->master->finish(session->id, status);
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