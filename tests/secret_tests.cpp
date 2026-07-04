#include "../secret.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using Test = std::function<bool()>;
constexpr auto test_timeout = std::chrono::milliseconds(750);
int failures = 0;

std::array<int, 2> make_sockets() {
    std::array<int, 2> result{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, result.data()) != 0) {
        std::perror("socketpair");
        std::exit(2);
    }
    return result;
}

bool write_all(int fd, const uint8_t* data, std::size_t size) {
    while (size != 0) {
        const auto count = send(fd, data, size, 0);
        if (count <= 0) return false;
        data += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}

std::vector<uint8_t> read_exact(int fd, std::size_t size) {
    std::vector<uint8_t> result(size);
    std::size_t offset = 0;
    while (offset != size) {
        const auto count = recv(fd, result.data() + offset, size - offset, 0);
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    result.resize(offset);
    return result;
}

std::vector<uint8_t> frame(uint16_t size, uint8_t code,
                           const std::vector<uint8_t>& payload = {}) {
    std::vector<uint8_t> result(3 + payload.size());
    std::memcpy(result.data(), &size, sizeof(size));
    result[2] = code;
    std::copy(payload.begin(), payload.end(), result.begin() + 3);
    return result;
}

bool send_secret_serializes_binary_frame() {
    auto pair = make_sockets();
    const uint8_t payload[]{0x41, 0x00, 0xff};
    const bool sent = send_Secret(7, payload, sizeof(payload), pair[0]);
    const auto bytes = read_exact(pair[1], 6);
    uint16_t length = 0;
    if (bytes.size() >= 2) std::memcpy(&length, bytes.data(), sizeof(length));
    close(pair[0]);
    close(pair[1]);
    return sent && bytes.size() == 6 && length == 6 && bytes[2] == 7 &&
           std::equal(bytes.begin() + 3, bytes.end(), payload);
}

bool send_secret_accepts_empty_payload() {
    auto pair = make_sockets();
    const bool sent = send_Secret(4, nullptr, 0, pair[0]);
    const auto bytes = read_exact(pair[1], 3);
    close(pair[0]);
    close(pair[1]);
    return sent && bytes.size() == 3 && bytes[2] == 4;
}

bool send_secret_rejects_null_nonempty_payload() {
    auto pair = make_sockets();
    const bool result = send_Secret(1, nullptr, 1, pair[0]);
    close(pair[0]);
    close(pair[1]);
    return !result;
}

bool send_secret_fails_on_closed_peer() {
    auto pair = make_sockets();
    close(pair[1]);
    const uint8_t byte = 1;
    const bool result = send_Secret(1, &byte, 1, pair[0]);
    close(pair[0]);
    return !result;
}

bool send_secret_rejects_length_overflow() {
    auto pair = make_sockets();
    std::vector<uint8_t> payload(UINT16_MAX - 2, 0x5a);
    std::thread drain([&] { read_exact(pair[1], payload.size() + 3); });
    const bool result = send_Secret(1, payload.data(), payload.size(), pair[0]);
    shutdown(pair[0], SHUT_WR);
    drain.join();
    close(pair[0]);
    close(pair[1]);
    return !result;
}

bool send_secret_uses_network_byte_order() {
    auto pair = make_sockets();
    const uint8_t payload[]{1, 2};
    const bool sent = send_Secret(3, payload, sizeof(payload), pair[0]);
    const auto bytes = read_exact(pair[1], 5);
    close(pair[0]);
    close(pair[1]);
    return sent && bytes.size() == 5 && bytes[0] == 0 && bytes[1] == 5;
}

bool send_message_preserves_embedded_nul() {
    auto pair = make_sockets();
    const std::string message{"a\0b", 3};
    const bool sent = send_Message(2, message, pair[0]);
    const auto bytes = read_exact(pair[1], 6);
    close(pair[0]);
    close(pair[1]);
    return sent && bytes.size() == 6 && bytes[2] == 2 &&
           bytes[3] == 'a' && bytes[4] == 0 && bytes[5] == 'b';
}

bool read_secret_reads_valid_frame() {
    auto pair = make_sockets();
    const auto bytes = frame(5, 9, {'o', 'k'});
    write_all(pair[0], bytes.data(), bytes.size());
    std::array<uint8_t, 8> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return result && code == 9 && size == 5 &&
           data[0] == 'o' && data[1] == 'k';
}

bool read_secret_reads_exact_capacity() {
    auto pair = make_sockets();
    const auto bytes = frame(7, 8, {1, 2, 3, 4});
    write_all(pair[0], bytes.data(), bytes.size());
    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return result && code == 8 && size == 7 &&
           std::equal(data.begin(), data.end(), bytes.begin() + 3);
}

bool read_secret_reads_empty_payload() {
    auto pair = make_sockets();
    const auto bytes = frame(3, 6);
    write_all(pair[0], bytes.data(), bytes.size());
    std::array<uint8_t, 1> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return result && code == 6 && size == 3;
}

bool read_secret_handles_fragmented_header() {
    auto pair = make_sockets();
    const auto bytes = frame(5, 2, {'o', 'k'});
    std::thread writer([&] {
        write_all(pair[0], bytes.data(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        write_all(pair[0], bytes.data() + 1, bytes.size() - 1);
    });
    std::array<uint8_t, 2> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    writer.join();
    close(pair[0]);
    close(pair[1]);
    return result && code == 2 && size == 5 && data[0] == 'o' && data[1] == 'k';
}

bool read_secret_handles_fragmented_body() {
    auto pair = make_sockets();
    const auto bytes = frame(7, 5, {1, 2, 3, 4});
    std::thread writer([&] {
        write_all(pair[0], bytes.data(), 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        write_all(pair[0], bytes.data() + 4, bytes.size() - 4);
    });
    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    writer.join();
    close(pair[0]);
    close(pair[1]);
    return result && code == 5 && size == 7 &&
           std::equal(data.begin(), data.end(), bytes.begin() + 3);
}

bool read_secret_rejects_closed_peer() {
    auto pair = make_sockets();
    close(pair[0]);
    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[1]);
    return !result;
}

bool read_secret_rejects_truncated_code() {
    auto pair = make_sockets();
    const uint16_t size_on_wire = 3;
    write_all(pair[0], reinterpret_cast<const uint8_t*>(&size_on_wire), 2);
    shutdown(pair[0], SHUT_WR);
    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return !result;
}

bool read_secret_rejects_truncated_payload() {
    auto pair = make_sockets();
    const auto bytes = frame(8, 1, {'x'});
    write_all(pair[0], bytes.data(), bytes.size());
    shutdown(pair[0], SHUT_WR);
    std::array<uint8_t, 8> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return !result;
}

bool read_secret_rejects_short_frame() {
    auto pair = make_sockets();
    const auto bytes = frame(2, 1);
    write_all(pair[0], bytes.data(), bytes.size());
    shutdown(pair[0], SHUT_WR);
    std::array<uint8_t, 4> data{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(data.data(), data.size(), pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return !result;
}

bool read_secret_rejects_oversized_payload() {
    auto pair = make_sockets();
    const auto bytes = frame(9, 1, {1, 2, 3, 4, 5, 6});
    write_all(pair[0], bytes.data(), bytes.size());
    std::array<uint8_t, 16> backing{};
    uint8_t code = 0;
    uint16_t size = 0;
    const bool result = read_Secret(backing.data(), 2, pair[1], code, size);
    close(pair[0]);
    close(pair[1]);
    return !result;
}

void run(const char* name, const Test& test) {
    const pid_t child = fork();
    if (child == 0) {
        std::signal(SIGPIPE, SIG_IGN);
        _exit(test() ? 0 : 1);
    }

    int status = 0;
    bool completed = false;
    const auto deadline = std::chrono::steady_clock::now() + test_timeout;
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
    if (!completed) std::cout << " (timeout)";
    else if (WIFSIGNALED(status)) std::cout << " (signal " << WTERMSIG(status) << ')';
    std::cout << '\n';
    failures += !passed;
}
} // namespace

int main() {
    run("send_Secret serializes binary frame", send_secret_serializes_binary_frame);
    run("send_Secret supports empty payload", send_secret_accepts_empty_payload);
    run("send_Secret rejects null nonempty payload",
        send_secret_rejects_null_nonempty_payload);
    run("send_Secret returns false for closed peer", send_secret_fails_on_closed_peer);
    run("send_Secret rejects uint16 length overflow",
        send_secret_rejects_length_overflow);
    run("send_Secret uses network byte order", send_secret_uses_network_byte_order);
    run("send_Message preserves embedded NUL", send_message_preserves_embedded_nul);
    run("read_Secret reads valid frame", read_secret_reads_valid_frame);
    run("read_Secret accepts exact buffer capacity", read_secret_reads_exact_capacity);
    run("read_Secret accepts empty payload", read_secret_reads_empty_payload);
    run("read_Secret handles fragmented TCP header",
        read_secret_handles_fragmented_header);
    run("read_Secret handles fragmented TCP body", read_secret_handles_fragmented_body);
    run("read_Secret rejects closed peer", read_secret_rejects_closed_peer);
    run("read_Secret rejects missing code", read_secret_rejects_truncated_code);
    run("read_Secret rejects truncated payload", read_secret_rejects_truncated_payload);
    run("read_Secret rejects frame shorter than header", read_secret_rejects_short_frame);
    run("read_Secret enforces max_numbits", read_secret_rejects_oversized_payload);
    std::cout << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
