//
// Created by jan on 29.05.16.
//

#include <netdb.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "PlayerSession.h"

PlayerSession::PlayerSession(int id, TelnetSession *telnet_session) : id(id), telnet(telnet_session) { }

void PlayerSession::init_socket(std::string host, std::string port) {
    // TODO usunąć
    //host = "localhost";

    int rc;
    uint16_t port_int;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) throw PlayerException("init_socket socket " + host);

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

    freeaddrinfo(addr_result);
}

void PlayerSession::send_command(std::string command) {
    ssize_t len = sendto(sock, command.c_str(), command.size(), 0, (sockaddr *) &addr, sizeof(struct sockaddr_in));
    if (len < 0) {
        perror("sendto: send command to player");
        telnet->send("ERROR " + std::to_string(id) + " " + command);
    }
}