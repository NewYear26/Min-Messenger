#include "../secret.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += !condition;
}

std::array<int, 2> sockets() {
    std::array<int, 2> result{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, result.data()) != 0) {
        std::perror("socketpair");
        std::exit(2);
    }
    return result;
}

std::vector<uint8_t> receive_exact(int socket, std::size_t size) {
    std::vector<uint8_t> result(size);
    std::size_t offset = 0;
    while (offset != size) {
        const auto count = recv(socket, result.data() + offset, size - offset, 0);
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    result.resize(offset);
    return result;
}

void test_send_secret_frame() {
    auto pair = sockets();
    const uint8_t payload[]{0x41, 0x00, 0xff};
    check(send_Secret(7, payload, sizeof(payload), pair[0]),
          "send_Secret reports success");
    const auto bytes = receive_exact(pair[1], 6);
    uint16_t length = 0;
    if (bytes.size() >= 2) std::memcpy(&length, bytes.data(), sizeof(length));
    check(bytes.size() == 6 && length == 6 && bytes[2] == 7 &&
              std::equal(bytes.begin() + 3, bytes.end(), payload),
          "send_Secret serializes header, code, and binary payload");
    close(pair[0]);
    close(pair[1]);
}

void test_send_message() {
    auto pair = sockets();
    const std::string message{"a\0b", 3};
    check(send_Message(2, message, pair[0]), "send_Message reports success");
    const auto bytes = receive_exact(pair[1], 6);
    check(bytes.size() == 6 && bytes[2] == 2 && bytes[3] == 'a' &&
              bytes[4] == 0 && bytes[5] == 'b',
          "send_Message preserves embedded NUL bytes");
    close(pair[0]);
    close(pair[1]);
}

void test_read_secret() {
    auto pair = sockets();
    const uint8_t payload[]{'o', 'k'};
    check(send_Secret(9, payload, sizeof(payload), pair[0]), "fixture frame sent");
    std::array<uint8_t, 8> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    check(result && code == 9 && size == 5 && data[0] == 'o' && data[1] == 'k',
          "read_Secret returns code, frame size, and payload");
    close(pair[0]);
    close(pair[1]);
}

void test_read_rejects_short_frame() {
    auto pair = sockets();
    const uint16_t invalid_length = 2;
    send(pair[0], &invalid_length, sizeof(invalid_length), 0);
    const uint8_t code_byte = 1;
    send(pair[0], &code_byte, sizeof(code_byte), 0);
    shutdown(pair[0], SHUT_WR);

    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const pid_t child = fork();
    if (child == 0) {
        const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
        _exit(result ? 1 : 0);
    }
    int status = 0;
    bool completed = false;
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (waitpid(child, &status, WNOHANG) == child) {
            completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!completed) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    check(completed && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "read_Secret rejects length smaller than header");
    close(pair[0]);
    close(pair[1]);
}
} // namespace

int main() {
    test_send_secret_frame();
    test_send_message();
    test_read_secret();
    test_read_rejects_short_frame();
    std::cout << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
