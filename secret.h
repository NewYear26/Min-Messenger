#ifndef UNTITLED_SECRET_H
#define UNTITLED_SECRET_H
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mutex>
bool send_Secret(uint8_t code,const uint8_t *data, size_t numbits, int socket);
bool read_Secret(uint8_t *data, size_t max_numbits, int socket, uint8_t &code, uint16_t &realmes);
bool send_Message(uint8_t code, std::string message, int socket);
const int MAX_SEND_ATTEMPTS = 100;
const int MAX_MESSAGE_SIZE = 4096;
#endif //UNTITLED_SECRET_H
