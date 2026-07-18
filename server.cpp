#include "secret.h"
int id = 0;


struct User {
    int id;
    int socket;
    std::vector<int> room;
};

std::vector<User> users;
std::vector<int> free_ids;

std::mutex users_mutex;
std::mutex freeids_mutex;

void ClientCom(User user) {
    bool oror = send_Message(0, "ok", user.socket);
    if (oror == false) {
        close(user.socket);
        std::cout << "Send_Message is false";
        return;
    }
    char buff[MAX_MESSAGE_SIZE];
    uint16_t realmsize;
    uint8_t code;
    oror = read_Secret(reinterpret_cast<uint8_t *>(buff), std::size(buff), user.socket, code, realmsize);
    if (oror == false) {
        std::cout << "nedostup read_Secret";
        close(user.socket);
        return;
    }
    if (code != 1) {
        std::cout << "Everything is bad, code ne 1";
        close(user.socket);
        return;
    }
    while (true) {
        bool status = read_Secret(reinterpret_cast<uint8_t *>(buff), MAX_MESSAGE_SIZE,user.socket, code, realmsize);
        if (status == false) {
            std::cout << "read_Secret ERROR";
            close(user.socket);
            return;
        }
        if (code == 2) {
            for (int i = 0; i < users.size(); i++) {
                send_Secret(2, reinterpret_cast<uint8_t*>(buff), realmsize-3, users[i].socket);
            }
        }
    }
}


int main() {
    int serversocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serversocket < -1) {
        std::cout << "Can't create socket! ";
        return 404;
    }
    sockaddr_in serveradr = {0};
    serveradr.sin_family = AF_INET;
    serveradr.sin_port = htons(4552);
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
        User user;
        {
            std::lock_guard<std::mutex> lock(freeids_mutex);
            if (std::size(free_ids) != 0) {
                user.id = free_ids[std::size(free_ids) - 1];
                free_ids.pop_back();
            } else {
                user.id = id + 1;
                id++;
            }
            user.socket = sockmessage;
            users.push_back(user);
        }
        std::thread t(ClientCom, user);
        t.detach();
    }
    return 0;
}
