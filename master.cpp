#include <iostream>
#include <map>
#include <thread>
#include <atomic>
#include <system_error>
#include <poll.h>
#include <netdb.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "err.h"

// START jk359785@students.mimuw.edu.pl ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes
// START localhost ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes
// rlwrap telnet localhost 37479

class PlayerSession;

class DelayedPlayerSession;

class TelnetSession;

void telnet_listen(int);

void player_launch(TelnetSession *, int, std::string, std::string);

void delayed_player_launch(TelnetSession *, std::shared_ptr<DelayedPlayerSession>, std::string, std::string,
                           std::string, int, std::string, std::string);

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

        if (boost::regex_match(port, boost::regex("\\d+"))) {
            port_int = boost::lexical_cast<uint16_t>(port);
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
        addr.sin_addr.s_addr = ((struct sockaddr_in *) (addr_result->ai_addr))->sin_addr.s_addr;
        addr.sin_port = htons(port_int);

        sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
        if (sock < 0) throw PlayerException("init_socket socket " + host);

        freeaddrinfo(addr_result);
    }

    virtual bool is_active() {
        return true;
    }

    class PlayerException : public std::exception {
    public:
        PlayerException(std::string s) : message(s) { }

        virtual const char *what() const throw() {
            return message.c_str();
        }

    private:
        std::string message;
    };
};

class DelayedPlayerSession : public PlayerSession {
public:
    DelayedPlayerSession(int id) : PlayerSession(id) {
        active = false;
    }

    bool is_active() {
        return active.load(std::memory_order_relaxed);
    }

    void activate() {
        active.store(true, std::memory_order_relaxed);
    }

private:
    std::atomic<bool> active;
};

class TelnetSession {
public:
    TelnetSession(int telnetsock) : sock(telnetsock) { }

    ~TelnetSession() {
        if (close(sock) < 0) perror("close");
    }

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
            }
            else if (rc == 0) {
                fprintf(stderr, "Telnet client on socket %d closed connection\n", sock);
                throw ConnectionClosed();
            }
            else if (len + 1 >= BUFFER_SIZE) {
                buffer[BUFFER_SIZE] = 0;
                len = 0;
                fprintf(stderr, "Telnet buffer exceeded, invalid command:\n%s\n", buffer);
                send_back("ERROR Buffer exceeded, invalid command");
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

    void finish(int id, std::string status) {
        auto it = PlayerSessions.find(id);
        if (it != PlayerSessions.end()) {
            if (close(it->second->sock) < 0) perror("close");
            PlayerSessions.erase(it);
            std::string s;
            if (status == "0" || status == "124")
                s = "Player " + std::to_string(id) + " finished with status " + status;
            else s = "ERROR " + std::to_string(id) + " Player finished with status " + status;
            send_back(s);
        }
        else {
            fprintf(stderr, "Player session id: %d, status: %s can not be finished\n", id, status.c_str());
        }
    }

    class ConnectionClosed : public std::exception {
    public:
        virtual const char *what() const throw() {
            return "Connection closed";
        }
    };

private:
    static const int BUFFER_SIZE = 2048;
    int sock;
    int next_id = 1;
    std::map<int, std::shared_ptr<PlayerSession>> PlayerSessions;

    int get_it() {
        if (next_id <= 0) next_id = 1;
        auto it = PlayerSessions.find(next_id);
        if (it == PlayerSessions.end()) {
            return next_id++;
        }
        while (it != PlayerSessions.end() && next_id > 0 && it->first == next_id) {
            ++next_id;
            ++it;
        }
        if (next_id <= 0) return get_it();
        return next_id++;
    }

    void parse_command(char *command) {
        std::string scommand(filter(command));
        // START komputer host path r-port file m-port md
        static const boost::regex start_regex(
                "START +(\\S+) +(\\S+ +\\S+ +\\d{1,5} +(\\S+) +(\\d{1,5}) +(?:yes|no))\\s*");
        // AT HH.MM M komputer host path r-port file m-port md
        static const boost::regex at_regex(
                "AT +(\\d{2})\\.(\\d{2}) +(\\d) +(\\S+) +(\\S+ +\\S+ +\\d{1,5} +(\\S+) +(\\d{1,5}) +(?:yes|no))\\s*");
        // PAUSE | PLAY | QUIT  ID
        static const boost::regex command_regex("(PAUSE|PLAY|QUIT) +(\\d+)\\s*");
        // TITLE ID
        static const boost::regex title_regex("TITLE +(\\d+)\\s*");
        boost::smatch match;
        if (boost::regex_match(scommand, match, command_regex)) {
            send_command(match[1], match[2]);
        }
        else if (boost::regex_match(scommand, match, title_regex)) {
            fetch_title(match[1]);
        }
        else if (boost::regex_match(scommand, match, start_regex)) {
            if (match[3] == "-") {
                fprintf(stderr, "Invalid command: %s\n", command);
                send_back("ERROR Invalid command");
            }
            else {
                start_ssh_session(match[1], match[4], match[2]);
            }
        }
        else if (boost::regex_match(scommand, match, at_regex)) {
            if (match[6] == "-") {
                fprintf(stderr, "Invalid command: %s\n", command);
                send_back("ERROR Invalid command");
            }
            else {
                start_delayed_ssh_session(match[1], match[2], match[3], match[4], match[7], match[5]);
            }
        }
        else {
            fprintf(stderr, "Invalid command: %s\n", scommand.c_str());
            send_back("ERROR Invalid command");
        }
    }

    std::string filter(char *command) {
        std::string scommand;
        int state = 0;
        for (size_t i = 0, n = strlen(command); i < n; ++i) {
            switch (state) {
                case 0:
                    if (command[i] == 255) state = 1;
                    else scommand.push_back(command[i]);
                    break;
                case 1:
                    if (command[i] == 255) {
                        scommand.push_back(command[i]);
                        state = 0;
                    }
                    else if (command[i] >= 251 && command[i] <= 254) state = 2;
                    else state = 0;
                    break;
                default:
                    // case 2
                    state = 0;
                    break;
            }
        }
        return scommand;
    }

    void start_ssh_session(std::string host, std::string port, std::string arguments) {
        int id = get_it();
        std::shared_ptr<PlayerSession> player(new PlayerSession(id));
        try {
            player->init_socket(host, port);
            PlayerSessions.insert(std::make_pair(id, player));
            std::thread(player_launch, this, id, host, arguments).detach();
        }
        catch (const PlayerSession::PlayerException &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR START " + host);
            return;
        }
        catch (const std::system_error &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR START " + host);
            return;
        }
        send_back("OK " + std::to_string(id));
    }

    void start_delayed_ssh_session(std::string hh, std::string mm, std::string interval, std::string host,
                                   std::string port, std::string arguments) {
        int id = get_it();
        std::shared_ptr<DelayedPlayerSession> player(new DelayedPlayerSession(id));
        try {
            player->init_socket(host, port);
            PlayerSessions.insert(std::make_pair(id, player));
            std::thread(delayed_player_launch, this, player, hh, mm, interval, id, host, arguments).detach();
        }
        catch (PlayerSession::PlayerException &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR START " + host);
            return;
        }
        catch (const std::system_error &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR START " + host);
            return;
        }
        send_back("OK " + std::to_string(id));
    }

    void send_command(std::string command, std::string id_str) {
        int id;
        try {
            id = boost::lexical_cast<int>(id_str);
        }
        catch (const boost::bad_lexical_cast &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR " + id_str + " Player parse title");
            return;
        }
        auto it = PlayerSessions.find(id);
        if (it == PlayerSessions.end()) {
            send_back("ERROR " + id_str + " Player not found");
        }
        else if (!it->second->is_active()) {
            send_back("ERROR " + id_str + " Player not active");
        }
        else {
            ssize_t len = sendto(it->second->sock, command.c_str(), command.size(), 0,
                                 (sockaddr *) &(it->second->addr),
                                 sizeof(struct sockaddr_in));
            if (len < 0) {
                send_back("ERROR " + id_str + " Player command not sent");
                perror("sendto: send command to player");
            }
            else {
                send_back("OK " + id_str);
            }
        }
    }

    void fetch_title(std::string id_str) {
        int id;
        try {
            id = boost::lexical_cast<int>(id_str);
        }
        catch (const boost::bad_lexical_cast &ex) {
            fprintf(stderr, "%s\n", ex.what());
            send_back("ERROR " + id_str + " Player parsing title");
            return;
        }
        auto it = PlayerSessions.find(id);
        if (it == PlayerSessions.end()) {
            send_back("ERROR " + id_str + " Player not found");
        }
        else if (!it->second->is_active()) {
            send_back("ERROR " + id_str + " Player not active");
        }
        else {
            ssize_t len = sendto(it->second->sock, "TITLE", 5, 0, (sockaddr *) &(it->second->addr),
                                 sizeof(struct sockaddr_in));
            if (len < 0) {
                send_back("ERROR " + id_str + " Player command not sent");
                perror("sendto: send command to player");
            }
            else {
                char buffer[8192];
                const static int WAITTIME = 3000;
                struct pollfd polls[1];
                polls[0].fd = it->second->sock;
                polls[0].events = POLLIN;
                polls[0].revents = 0;
                if (poll(polls, 1, WAITTIME) < 0) {
                    perror("poll");
                    send_back("ERROR " + id_str + " getting title from player");
                }
                else if (polls[0].revents == POLLIN) {
                    len = recv(it->second->sock, buffer, sizeof(buffer), 0);
                    if (len > 0) {
                        send_back("OK " + id_str + " " + std::string(buffer));
                        return;
                    }
                }
                send_back("ERROR " + id_str + " getting title from player");
            }
        }
    }

};

void telnet_listen(int telnet_sock) {
    TelnetSession telnet(telnet_sock);
    try {
        telnet.listen();
    }
    catch (TelnetSession::ConnectionClosed &ex) {
        fprintf(stderr, "%s\n", ex.what());
    }
}

void player_launch(TelnetSession *telnet_session, int id, std::string host, std::string arguments) {
    std::string command("ssh " + host + " \"bash -cl 'player " + arguments + "; echo $?'\"");
    FILE *fpipe = (FILE *) popen(command.c_str(), "r");
    if (fpipe == NULL) {
        perror("Problems with pipe");
    }
    else {
        char ret[256];
        if (fgets(ret, sizeof(ret), fpipe)) {
            boost::cmatch match;
            boost::regex_search(ret, match, boost::regex("(\\d+)"));
            std::string status(match[1]);
            telnet_session->finish(id, status);
        }
        else {
            perror("reading status from player");
        }
        pclose(fpipe);
    }
}

void delayed_player_launch(TelnetSession *telnet_session, std::shared_ptr<DelayedPlayerSession> player_session,
                           std::string hh, std::string mm, std::string interval, int id, std::string host,
                           std::string arguments) {
    const static int DAY_MINUTES = 1440;
    try {
        int HH = boost::lexical_cast<int>(hh);
        int MM = boost::lexical_cast<int>(mm);
        time_t t = time(0);
        struct tm *now = localtime(&t);
        int delay = (HH - now->tm_hour) * 60 + MM - now->tm_min;
        if (delay < 0) {
            delay = DAY_MINUTES - delay;
        }
        std::this_thread::sleep_for(std::chrono::minutes(delay));
        std::string command(
                "ssh " + host + " \"bash -cl 'timeout " + interval + "m player " + arguments + "; echo $?'\"");
        player_session->activate();
        FILE *fpipe = (FILE *) popen(command.c_str(), "r");
        if (fpipe == NULL) {
            perror("Problems with pipe");
        }
        else {
            char ret[256];
            if (fgets(ret, sizeof(ret), fpipe)) {
                boost::cmatch match;
                boost::regex_search(ret, match, boost::regex("(\\d+)"));
                std::string status(match[1]);
                telnet_session->finish(id, status);
            }
            else {
                perror("reading status from player");
            }
            pclose(fpipe);
        }
    }
    catch (...) {
        telnet_session->finish(id, "-1");
    }
}

int parse_port_number(char *port) {
    const static boost::regex port_regex("\\d+");
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
    else if (argc > 2) {
        fatal("Usage: %s [port]", argv[0]);
    }

    SocketListener socket(port);

    for (; ;) {
        socket.acceptConnection();
    }
}