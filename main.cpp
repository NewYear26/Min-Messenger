#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int id = 0;

struct User {
    int id;
    int socket;
};

std::vector<User> users;

void *clientcommunicate(void *data) {
    User user = *reinterpret_cast<User *>(data);
    while (true) {
        char buff[1024];
        int bits = recv(user.socket, buff, std::size(buff), 0);
        if (bits == -1) {
            std::cout << "Connection error :(";
            close(user.socket);
            // NADO DELETE IZ USERS
            break;
        }
        if (bits == 0) {
            std::cout << "Client disconnected!" << std::endl;
            close(user.socket);
            // NADO DELETE IZ USERS
            break;
        } else {
            buff[bits] = 0;
            for (int i = 0; i < users.size(); i++) {
                if (users[i].id != user.id) {
                    send(users[i].socket, buff, bits,0);
                }
            }

        };
    }
    return nullptr;
}

int main() {
    int serversocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serversocket < -1) {
        std::cout << "Can't create socket! ";
        return 404;
    }
    sockaddr_in serveradr = {0};
    serveradr.sin_family = AF_INET;
    serveradr.sin_port = htons(4551);
    inet_pton(AF_INET, "127.0.0.1", &serveradr.sin_addr);
    int binstatus = bind(serversocket, (sockaddr *) &serveradr, sizeof(serveradr));
    if (binstatus == -1) {
        std::cout << "Can't bind a socket! ";
        return 405;
    }
    int listenstatus = listen(serversocket, 10);
    if (listenstatus == -1) {
        std::cout << "Can't listen a socket! ";
        return 406;
    }
    sockaddr_in clientadr;
    socklen_t clientsize = sizeof(clientadr);

    while (true) {
        int sockmessage = accept(serversocket, (sockaddr *) &clientadr, &clientsize);
        if (sockmessage == -1) {
            std::cout << "Can't conect to client! ";
            continue;
        } else {
            std::cout << "Some one conneted!" << std::endl;
        }
        pthread_t idpotok;
        User newUser;
        newUser.id = id + 1;
        id++;
        newUser.socket = sockmessage;
        users.push_back(newUser);
        int er1 = pthread_create(&idpotok, 0, clientcommunicate, &newUser);
        if (er1 != 0) {
            std::cout << "POTOK ERROR, SOS";
            return -1;
        }
    }
    return 0;
}
