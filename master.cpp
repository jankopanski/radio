#include <iostream>
#include <unordered_map>
#include <thread>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "SocketListener.h"
#include "err.h"

// START jk359785@students.mimuw.edu.pl ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes
// START localhost ant-waw-01.cdn.eurozet.pl / 8602 test5.mp3 50000 yes

using namespace std;

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