#include <iostream>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include "err.h"

using namespace std;

class SocketListener {
public:
    SocketListener(int port) {

    }

private:
    int sock;
};

class TelnetSession {

};

class SshSession {

};

class DelayedSshSesion : public SshSession {

};

int parse_port_number(std::string port) {
    static boost::regex port_regex("\\d+");
    boost::smatch match;
    if (!boost::regex_match(port, match, port_regex)) {
        fatal("invalid port number %s", port);
    }
    int port_num = boost::lexical_cast<int>(port);
    if (port_num < 1024 || port_num > 65535) {
        fatal("invalid port number %d", port_num);
    }
    return port_num;
}

//int get_free_port() {
//
//}

int main(int argc, char *argv[]) {
    int port = 0;

//    if (argc == 1) {
//        port = 0get_free_port();
//    }
    if (argc == 2) {
        port = parse_port_number(argv[1]);
    }
    else if (argc > 2){
        fatal("Usage: %s [port]", argv[0]);
    }


    SocketListener socket(port);
}