#include "secret.h"

bool send_Secret(uint8_t code, const uint8_t *data, size_t numbits, int socket) {
    uint8_t *dataforcend = new uint8_t[3 + numbits];
    for (int i = 0; i < numbits; i++) {
        *(dataforcend + 3 + i) = *reinterpret_cast<const uint8_t *>(data + i);
    }
    *reinterpret_cast<uint16_t *>(dataforcend) = 3 + numbits;
    *reinterpret_cast<uint8_t *>(dataforcend + 2) = code;
    ssize_t total = 0;
    int attemp = 0;
    while (true) {
        ssize_t kolvo = send(socket, dataforcend + total, numbits + 3 - total, 0);
        if (kolvo <= 0) {
            attemp++;
            if (attemp == MAX_SEND_ATTEMPTS) {
                delete [] dataforcend;
                return false;
            }
        }
        if (kolvo > 0) {
            total += kolvo;
            if (total == numbits + 3) {
                delete [] dataforcend;
                return true;
            }
        }
    }
}

bool read_Secret(uint8_t *data, size_t max_numbits, int socket, uint8_t &code, uint16_t &realmes) {
    uint16_t msize;
    ssize_t colv = recv(socket, &msize, 2, 0);
    if (colv < 2) {
        std::cout << "Can't read the message";
        return false;
    }
    colv = recv(socket, &code,1, 0);
    if (colv < 1) {
        std::cout << "Can't read the message";
    }
    realmes = msize;
    int total = 3;
    while (true) {
        ssize_t kolvo = recv(socket, data + total - 3, msize - total, 0);
        if (kolvo == -1) {
            std::cout << "Can't read the message";
            return false;
        }
        if (kolvo > 0) {
            total += kolvo;
            if (total == msize) {
                return true;
            }
        }
    }
}

bool send_Message(uint8_t code, std::string message, int socket) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(message.c_str());
    bool result = send_Secret(code, data, message.size(), socket);
    return result;
}
