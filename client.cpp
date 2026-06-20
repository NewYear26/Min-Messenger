#include <iostream>
#include <pthread.h>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "secret.h"

int servsk;

void comserver() {
    char buff[MAX_MESSAGE_SIZE];
    uint16_t realmsize;
    uint8_t code;
   bool oror = read_Secret(reinterpret_cast<uint8_t *>(buff), MAX_MESSAGE_SIZE, servsk, code, realmsize);
    if (oror == false) {
        close(servsk);
        std::cout << "ERROR CON";
        return;
    }
    if (code != 0) {
        close(servsk);
        std::cout << "PROTO_ERROR" << std::endl<< "SERVER MUST SEND 0";
        return;
    }
    oror = send_Message(1,"ok", servsk);
    if (oror == false) {
        close(servsk);
        std::cout << "Send messange net otveta";
        return;
    }
    while (true) {
        bool readokrono = read_Secret(reinterpret_cast<uint8_t *>(buff), MAX_MESSAGE_SIZE, servsk, code, realmsize);
        if (readokrono == false) {
            std::cout << "ERROR";
            return;
        }
        else {
            if (code == 2) {
                std::cout << "IMPORTANT!!!";
                buff[realmsize-3] = 0;
                std::cout << buff;
                std::cout << std::flush;
            }
            else {
                std::cout << code;
                std::cout << std::flush;
            }
        }
    }

}

int main() {
    servsk = socket(AF_INET, SOCK_STREAM, 0);
    if (servsk < -1) {
        std::cout << "Can't create socket! ";
        return 404;
    }
    sockaddr_in serveradr = {0};
    serveradr.sin_family = AF_INET;
    serveradr.sin_port = htons(4552);
    inet_pton(AF_INET, "127.0.0.1", &serveradr.sin_addr);
    int statusconnect = connect(servsk, (sockaddr *) &serveradr, sizeof(serveradr));
    if (statusconnect == -1) {
        std::cout << "Can't connect!";
        return 405;
    }
    std::thread d(comserver);
    d.detach();
    std::string msg;
    while (true) {
        std::cin >> msg;
       bool sendokorno = send_Message(2, msg, servsk);
        if (sendokorno == false) {
            std::cout << "ERROR";
            return 1;
        }
        else {

        }
    }
    return 0;
}
