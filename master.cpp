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
// rlwrap telnet localhost 37479

using namespace std;

class PlayerSession;
class TelnetSession;
void telnet_listen(int);
void player_launch(TelnetSession*, int, std::string, std::string);

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
    }

private:
    static const int BACKLOG = 10;
    int sock;
};

class PlayerSession {
public:
    int id;
    int sock;
    struct sockaddr_in addr;

    PlayerSession(int id) : id(id) { }

    void init_socket(std::string host, std::string port) {
        int rc;
        uint16_t port_int;

        //sock = socket(AF_INET, SOCK_DGRAM, 0);
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

        sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);

        freeaddrinfo(addr_result);
    }

    class PlayerException: public std::exception {
    public:
        PlayerException(std::string s) : message(s) { }
        virtual const char* what() const throw() {
            return message.c_str();
        }
    private:
        std::string message;
    };
};

class DelayedPlayerSession : public PlayerSession {
public:
    DelayedPlayerSession(int id) : PlayerSession(id) { }
};

class TelnetSession {
public:
    TelnetSession(int telnetsock) : sock(telnetsock) { }

    void send_back(std::string message) {
        ssize_t rc;
        message.append("\r\n");
        rc = write(sock, message.c_str(), message.size());
        if (rc < 0) {
            perror("TelnetSession send write");
            throw ConnectionClosed();
        }
    }

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
                send_back("ERROR: Buffer exceeded, invalid command");
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

    void finish(int id, std::string status, std::string error_message) {
        auto it = PlayerSessions.find(id);
        if (it != PlayerSessions.end()) {
            PlayerSessions.erase(it);
            std::string s;
            if (status == "0") s = "Player " + std::to_string(id) + " finished with status " + status;
            else s = "ERROR: Player " + std::to_string(id) + " finished with status " + status + " " + error_message;
            send_back(s);
        }
        else {
            fprintf(stderr, "Player session id: %d, status: %s can not be finished\n", id, status.c_str());
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
    int sock;
    int next_id = 1;
    std::unordered_map<int, shared_ptr<PlayerSession>> PlayerSessions;

    void parse_command(char *command) {
        // TODO filtracja sekwencji znakowych
        // START komputer host path r-port file m-port md
        static const boost::regex start_regex("START +(\\S+) +(\\S+ +\\S+ +\\d+ +(\\S+) +(\\d+) +(?:yes|no))\\s*");
        // AT HH.MM M komputer host path r-port file m-port md
        static const boost::regex at_regex("AT +(\\d{2}\\.\\d{2}) +(\\d) +(\\S+) +(\\S+ +\\S+ +\\d+ +(\\S+) +(\\d+) +(?:yes|no))\\s*");
        // PAUSE | PLAY | QUIT  ID
        static const boost::regex command_regex("(PAUSE|PLAY|QUIT) +(\\d+)\\s*");
        // TITLE ID
        static const boost::regex title_regex("TITLE +(\\d+)\\s*");

        boost::cmatch match; // TODO podział na matche
        if (boost::regex_match(command, match, command_regex)) {
            cerr<<"parse_command "<<match[1]<<' '<<match[2]<<endl;
            send_command(match[1], match[2]);
        }
        else if (boost::regex_match(command, match, title_regex)) {

        }
        else if (boost::regex_match(command, match, start_regex)) {
            if (match[3] == "-") {
                fprintf(stderr, "Invalid command: %s\n", command);
                send_back("ERROR: Invalid command");
            }
            else {
                start_ssh_session(match[1], match[4], match[2]);
            }
        }
        else if (boost::regex_match(command, match, at_regex)) {
            // TODO file '-'
        }
        else {
            fprintf(stderr, "Invalid command: %s\n", command);
            send_back("ERROR: Invalid command");
        }
    }

    void start_ssh_session(std::string host, std::string port, std::string arguments) {
        std::shared_ptr<PlayerSession> player(new PlayerSession(next_id));
        try {
            player->init_socket(host, port);
        }
        catch(PlayerSession::PlayerException &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR: START " + host);
            return;
        }
        PlayerSessions.insert(std::make_pair(next_id, player));
        std::thread(player_launch, this, next_id, host, arguments).detach();
        // TODO exception do thread
        send_back("OK " + std::to_string(next_id));
        ++next_id;
    }

    void send_command(std::string command, std::string id_str) {
        int id = boost::lexical_cast<int>(id_str);
        auto it = PlayerSessions.find(id);
        if (it == PlayerSessions.end()) {
            send_back("ERROR: Player " + id_str + " not found");
        }
        else {
            ssize_t len = sendto(it->second->sock, command.c_str(), command.size(), 0, (sockaddr *) &(it->second->addr), sizeof(struct sockaddr_in));
            if (len < 0) {
                send_back("ERROR: Player " + id_str + " command not sent");
                perror("sendto: send command to player");
            }
            else {
                send_back("OK " + id_str);
            }
        }
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

void player_launch(TelnetSession *telnet_session, int id, std::string host, std::string arguments) {
    // TODO zmienić na player
    // ' EXIT STATUS $? '
    std::string command("ssh " + host + " './ClionProjects/radio/player " + arguments + "; echo $?'");
    FILE *fpipe = (FILE *) popen(command.c_str(), "r");
    if (fpipe == NULL) {
        perror("Problems with pipe");
    }
    else {
        char ret[256]; // TODO zwiększyć limt bufora i go sprawdzać
        if (fgets(ret, sizeof(ret), fpipe)) {
            // TODO lepsza obróbka statusu
            fprintf(stderr, "%s\n", ret);
            std::string error_message = "42"; // TODO zaślepka
            std::string status = std::string(ret);
            telnet_session->finish(id, status, error_message);
        }
        else {
            perror("reading status from player");
        }
        pclose(fpipe);
    }
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