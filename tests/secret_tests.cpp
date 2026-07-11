#include "../secret.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Test = std::function<bool()>;
constexpr auto timeout = std::chrono::milliseconds(1000);
int failures = 0;

struct SocketPair {
    std::array<int, 2> fd{-1, -1};

    SocketPair() {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd.data()) != 0) {
            std::perror("socketpair");
            std::exit(2);
        }
    }

    ~SocketPair() {
        for (int socket : fd) {
            if (socket >= 0) close(socket);
        }
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    void close_end(std::size_t index) {
        if (fd[index] >= 0) {
            close(fd[index]);
            fd[index] = -1;
        }
    }
};

bool send_all(int socket, const uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t count = send(socket, data + offset, size - offset, 0);
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

std::vector<uint8_t> receive_exact(int socket, std::size_t size) {
    std::vector<uint8_t> result(size);
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t count = recv(socket, result.data() + offset, size - offset, 0);
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    result.resize(offset);
    return result;
}

std::vector<uint8_t> make_frame(uint8_t code,
                                const std::vector<uint8_t>& payload = {}) {
    const std::size_t full_size = payload.size() + 3;
    if (full_size > std::numeric_limits<uint16_t>::max()) {
        return {};
    }

    std::vector<uint8_t> result(full_size);
    const uint16_t network_size = htons(static_cast<uint16_t>(full_size));
    std::memcpy(result.data(), &network_size, sizeof(network_size));
    result[2] = code;
    std::copy(payload.begin(), payload.end(), result.begin() + 3);
    return result;
}

bool send_secret_writes_valid_binary_frame() {
    SocketPair sockets;
    const std::array<uint8_t, 3> payload{0x41, 0x00, 0xff};

    const bool sent = send_Secret(7, payload.data(), payload.size(), sockets.fd[0]);
    const auto actual = receive_exact(sockets.fd[1], payload.size() + 3);
    const auto expected = make_frame(
        7, std::vector<uint8_t>(payload.begin(), payload.end()));
    return sent && actual == expected;
}

bool send_secret_supports_empty_payload() {
    SocketPair sockets;
    const bool sent = send_Secret(4, nullptr, 0, sockets.fd[0]);
    return sent && receive_exact(sockets.fd[1], 3) == make_frame(4);
}

bool send_secret_rejects_null_payload() {
    SocketPair sockets;
    return !send_Secret(1, nullptr, 1, sockets.fd[0]);
}

bool send_secret_rejects_size_overflow() {
    SocketPair sockets;
    std::vector<uint8_t> payload(
        static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()) - 2,
        0x5a);
    return !send_Secret(1, payload.data(), payload.size(), sockets.fd[0]);
}

bool send_secret_reports_closed_peer() {
    SocketPair sockets;
    sockets.close_end(1);
    const uint8_t payload = 1;
    return !send_Secret(1, &payload, 1, sockets.fd[0]);
}

bool send_message_preserves_embedded_zero() {
    SocketPair sockets;
    const std::string message{"a\0b", 3};
    const bool sent = send_Message(2, message, sockets.fd[0]);
    return sent && receive_exact(sockets.fd[1], 6) ==
                       make_frame(2, {'a', 0, 'b'});
}

bool read_secret_reads_valid_frame() {
    SocketPair sockets;
    const auto frame = make_frame(9, {'o', 'k'});
    if (!send_all(sockets.fd[0], frame.data(), frame.size())) return false;

    std::array<uint8_t, 8> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    const bool read = read_Secret(data.data(), data.size(), sockets.fd[1],
                                  code, frame_size);
    return read && code == 9 && frame_size == 5 &&
           data[0] == 'o' && data[1] == 'k';
}

bool read_secret_accepts_exact_capacity() {
    SocketPair sockets;
    const std::vector<uint8_t> payload{1, 2, 3, 4};
    const auto frame = make_frame(8, payload);
    if (!send_all(sockets.fd[0], frame.data(), frame.size())) return false;

    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    const bool read = read_Secret(data.data(), data.size(), sockets.fd[1],
                                  code, frame_size);
    return read && code == 8 && frame_size == 7 &&
           std::equal(data.begin(), data.end(), payload.begin());
}

bool read_secret_accepts_empty_payload() {
    SocketPair sockets;
    const auto frame = make_frame(6);
    if (!send_all(sockets.fd[0], frame.data(), frame.size())) return false;

    std::array<uint8_t, 1> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    return read_Secret(data.data(), data.size(), sockets.fd[1], code, frame_size) &&
           code == 6 && frame_size == 3;
}

bool read_secret_handles_fragmented_header() {
    SocketPair sockets;
    const auto frame = make_frame(2, {'o', 'k'});
    std::thread writer([&] {
        send_all(sockets.fd[0], frame.data(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        send_all(sockets.fd[0], frame.data() + 1, frame.size() - 1);
    });

    std::array<uint8_t, 2> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    const bool read = read_Secret(data.data(), data.size(), sockets.fd[1],
                                  code, frame_size);
    writer.join();
    return read && code == 2 && frame_size == 5 &&
           data[0] == 'o' && data[1] == 'k';
}

bool read_secret_handles_fragmented_payload() {
    SocketPair sockets;
    const auto frame = make_frame(5, {1, 2, 3, 4});
    std::thread writer([&] {
        send_all(sockets.fd[0], frame.data(), 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        send_all(sockets.fd[0], frame.data() + 4, frame.size() - 4);
    });

    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    const bool read = read_Secret(data.data(), data.size(), sockets.fd[1],
                                  code, frame_size);
    writer.join();
    return read && code == 5 && frame_size == 7 &&
           data == std::array<uint8_t, 4>{1, 2, 3, 4};
}

bool read_secret_rejects_closed_peer() {
    SocketPair sockets;
    sockets.close_end(0);
    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    return !read_Secret(data.data(), data.size(), sockets.fd[1], code, frame_size);
}

bool read_secret_rejects_missing_code() {
    SocketPair sockets;
    const uint16_t network_size = htons(3);
    send_all(sockets.fd[0], reinterpret_cast<const uint8_t*>(&network_size), 2);
    shutdown(sockets.fd[0], SHUT_WR);

    std::array<uint8_t, 1> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    return !read_Secret(data.data(), data.size(), sockets.fd[1], code, frame_size);
}

bool read_secret_rejects_truncated_payload() {
    SocketPair sockets;
    const auto frame = make_frame(3, {1, 2, 3, 4});
    send_all(sockets.fd[0], frame.data(), frame.size() - 1);
    shutdown(sockets.fd[0], SHUT_WR);

    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    return !read_Secret(data.data(), data.size(), sockets.fd[1], code, frame_size);
}

bool read_secret_rejects_short_frame() {
    SocketPair sockets;
    const uint16_t network_size = htons(2);
    std::array<uint8_t, 3> frame{};
    std::memcpy(frame.data(), &network_size, sizeof(network_size));
    frame[2] = 1;
    send_all(sockets.fd[0], frame.data(), frame.size());
    shutdown(sockets.fd[0], SHUT_WR);

    std::array<uint8_t, 1> data{};
    uint8_t code = 0;
    uint16_t frame_size = 0;
    return !read_Secret(data.data(), data.size(), sockets.fd[1], code, frame_size);
}

bool read_secret_rejects_oversized_payload() {
    SocketPair sockets;
    const auto frame = make_frame(1, {1, 2, 3, 4, 5, 6});
    send_all(sockets.fd[0], frame.data(), frame.size());

    std::array<uint8_t, 10> backing{};
    backing.fill(0xa5);
    uint8_t code = 0;
    uint16_t frame_size = 0;
    const bool read = read_Secret(backing.data() + 2, 2, sockets.fd[1],
                                  code, frame_size);
    return !read &&
           std::all_of(backing.begin() + 4, backing.end(),
                       [](uint8_t byte) { return byte == 0xa5; });
}

void run(const char* name, const Test& test) {
    const pid_t child = fork();
    if (child < 0) {
        std::perror("fork");
        ++failures;
        return;
    }
    if (child == 0) {
        std::signal(SIGPIPE, SIG_IGN);
        _exit(test() ? 0 : 1);
    }

    int status = 0;
    bool completed = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
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

    const bool passed = completed && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!completed) {
        std::cout << " (timeout)";
    } else if (WIFSIGNALED(status)) {
        std::cout << " (signal " << WTERMSIG(status) << ')';
    }
    std::cout << '\n';
    failures += !passed;
}

} // namespace

int main() {
    run("send_Secret writes a valid binary frame",
        send_secret_writes_valid_binary_frame);
    run("send_Secret supports an empty payload",
        send_secret_supports_empty_payload);
    run("send_Secret rejects null non-empty payload",
        send_secret_rejects_null_payload);
    run("send_Secret rejects uint16 size overflow",
        send_secret_rejects_size_overflow);
    run("send_Secret reports a closed peer",
        send_secret_reports_closed_peer);
    run("send_Message preserves an embedded zero byte",
        send_message_preserves_embedded_zero);
    run("read_Secret reads a valid frame",
        read_secret_reads_valid_frame);
    run("read_Secret accepts exact buffer capacity",
        read_secret_accepts_exact_capacity);
    run("read_Secret accepts an empty payload",
        read_secret_accepts_empty_payload);
    run("read_Secret handles a fragmented header",
        read_secret_handles_fragmented_header);
    run("read_Secret handles a fragmented payload",
        read_secret_handles_fragmented_payload);
    run("read_Secret rejects a closed peer",
        read_secret_rejects_closed_peer);
    run("read_Secret rejects a missing code",
        read_secret_rejects_missing_code);
    run("read_Secret rejects a truncated payload",
        read_secret_rejects_truncated_payload);
    run("read_Secret rejects a frame shorter than its header",
        read_secret_rejects_short_frame);
    run("read_Secret enforces max_numbits",
        read_secret_rejects_oversized_payload);

    std::cout << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
