#include <iostream>
#include <unordered_map>
#include <thread>
#include <netdb.h>
#include <sys/socket.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "err.h"

// START jk359785@students.mimuw.edu.pl ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes
// START localhost ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes

using namespace std;

class PlayerSession;
class TelnetSession;
void telnet_listen(int);
void player_launch(shared_ptr<PlayerSession>, std::string, std::string);

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

class PlayerSession {
public:
    PlayerSession(int id, TelnetSession *telnet_session) : id(id), telnet(telnet_session) { }

    void init_socket(std::string host, std::string port) {
        // TODO usunąć
        //host = "localhost";

        int rc;
        uint16_t port_int;

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) throw PlayerException("init_socket socket " + host);

        if (boost::regex_match(port, boost::regex("\\d+"))) {
            port_int = boost::lexical_cast<uint16_t >(port);
            if (port_int < 1024 || port_int > 65535)
                throw PlayerException("invalid port number: " + port + " for " + host);
        }
        else {
            throw PlayerException("invalid port number: " + port + " for " + host);
        }

        struct addrinfo addr_hints;
        struct addrinfo *addr_result;
        memset(&addr_hints, 0, sizeof(struct addrinfo));
        addr_hints.ai_family = AF_INET;
        addr_hints.ai_socktype = SOCK_DGRAM;
        addr_hints.ai_protocol = IPPROTO_UDP;
        rc = getaddrinfo(host.c_str(), NULL, &addr_hints, &addr_result);
        if (rc == EAI_SYSTEM)
            throw PlayerException("getaddrinfo: " + std::string(gai_strerror(rc)));
        else if (rc != 0)
            throw PlayerException("getaddrinfo: " + std::string(gai_strerror(rc)));

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ((struct sockaddr_in*) (addr_result->ai_addr))->sin_addr.s_addr;
        addr.sin_port = htons(port_int);

        freeaddrinfo(addr_result);
    }

    friend void player_launch(shared_ptr<PlayerSession>, std::string, std::string);

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
    shared_ptr<TelnetSession> telnet;
};

class DelayedPlayerSession : public PlayerSession {
public:
    DelayedPlayerSession(int id, TelnetSession *telnet_session) : PlayerSession(id, telnet_session) { }
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
    }

    void finish(int id, int status) {
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
    std::unordered_map<int, shared_ptr<PlayerSession>> PlayerSessions;

    void parse_command(char *command) {
        //cerr<<command<<endl;
        // START komputer host path r-port file m-port md
        static const boost::regex start_regex("START +(\\S+) +(\\S+ +\\S+ +\\d+ +\\S+ +(\\d+) +(?:yes|no))\\s*");
        // AT HH.MM M komputer host path r-port file m-port md
        static const boost::regex at_regex("AT +(\\d{2}\\.\\d{2}) +(\\d) +(\\S+) +(\\S+ +\\S+ +\\d+ +\\S+ +(\\d+) +(?:yes|no))\\s*");
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
            //cerr<<match[1]<<endl<<match[2]<<endl<<match[3]<<endl;
            // TODO filtrowanie '-' jako pliku
            start_ssh_session(match[1], match[3], match[2]);
        }
        else if (boost::regex_match(command, match, at_regex)) {

        }
        else {
            fprintf(stderr, "Invalid command: %s\n", command);
            send("ERROR: Invalid command");
        }
    }

    void start_ssh_session(std::string host, std::string port, std::string arguments) {
        std::shared_ptr<PlayerSession> player(new PlayerSession(next_id, this));
        try {
            player->init_socket(host, port);
        }
        catch(PlayerSession::PlayerException &ex) {
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

void player_launch(shared_ptr<PlayerSession> session, std::string host, std::string arguments) {
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
        session->telnet->send("EXIT STATUS: " + std::string(ret));
        pclose(fpipe);
    }
}

//void ssh_exit(shared_ptr<PlayerSession> session) {
////    int status = ssh_channel_get_exit_status(session->channel);
////    session->master->finish(session->id, status);
//}

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