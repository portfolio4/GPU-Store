#include <catch2/catch_all.hpp>

#include <future>
#include <cstring>
#include <funx/clock/Clock.h>
#include <funx/net/TcpServerSocket.h>
#include <funx/net/TcpSession.h>
#include <funx/net/TcpSocket.h>
#include <funx/net/types.h>

TEST_CASE("Test TcpSession", "[funxlib]")
{
    const funx::TcpPort SERVER_PORT{9999};
    funx::TcpServerSocket server_socket(funx::TcpServerSocket::Config{
        .server_port = SERVER_PORT,
    });

    std::future<funx::TcpSession> server_fut = std::async(std::launch::async, [&]() {
        auto stop = funx::Clock::now() + std::chrono::seconds(1);
        while (funx::Clock::now() < stop) {
            auto socket = server_socket.maybeAccept();
            if (socket) {
                return funx::TcpSession(funx::TcpSession::Config{
                    .socket = std::move(*socket)
                });
            }
        }
        FAIL("Did not receive a client");
        throw std::runtime_error("Did not receive a client");
    });

    funx::TcpSession client{funx::TcpSession::Config{
        .socket = funx::TcpSocket::createOrThrow(funx::TcpSocket::Config{
            .ip4 = "127.0.0.1",
            .port = SERVER_PORT,
        })
    }};

    auto status = server_fut.wait_for(std::chrono::seconds(1));
    REQUIRE(status == std::future_status::ready);

    funx::TcpSession server = server_fut.get();

    SECTION("Send and receive a message") {
        std::string_view msg = "this is a basic test";
        client.send(funx::str_to_byte_view(msg));
        auto resp = server.peek(msg.size());
        REQUIRE(resp);
        REQUIRE(funx::byte_to_str_view(*resp) == msg);
    }

    SECTION("Buffers messages properly") {
        // If we send a message of size 500k, it will overflow the buffers
        // for sure if TcpSession attempts to read the whole thing. This
        // confirms that it does not try to read the whole thing
        std::vector<char> msg;
        for (int i = 0; i < 500'000; ++i) {
            msg.push_back(static_cast<char>(i));
        }
        client.send(funx::byte_view(reinterpret_cast<const std::byte*>(msg.data()), msg.size()));

        // peeks larger than MTU should be rejected
        REQUIRE(!server.peek(funx::TcpSession::MTU + 1));
        REQUIRE(server.peek(funx::TcpSession::MTU));

        for (size_t i = 0; i < msg.size(); ++i) {
            auto resp = server.peek(1);
            REQUIRE(resp);
            REQUIRE((*resp)[0] == static_cast<std::byte>(msg[i]));
            server.consume(1);
        }
    }

    SECTION("Never fully consume the buffer") {
        std::string_view msg = "0123456789";
        std::vector<char> expected = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
        // server sends 10 bytes 0..9
        // client reads 9 bytes
        // Do this 1000 times and we should have an extra 1000 elements left
        for (int i = 0; i < 1000; ++i) {
            server.send(funx::str_to_byte_view(msg));
            std::optional<funx::byte_view> peek = client.peek(9);
            REQUIRE(peek);
            for (int i = 0; i < 9; ++i) {
                REQUIRE((*peek)[i] == static_cast<std::byte>(expected[i]));
            }
            client.consume(9);
            std::rotate(expected.begin(), expected.begin() + 9, expected.end());
        }

        REQUIRE(!client.peek(1001));
        REQUIRE(client.peek(1000));
        for (int i = 0; i < 100; ++i) {
            std::optional<funx::byte_view> peek = client.peek(10);
            REQUIRE(peek);
            for (int j = 0; j < 10; ++j) {
                REQUIRE((*peek)[j] == static_cast<std::byte>(expected[j]));
            }
            client.consume(10);
        }

        // should be no more data
        REQUIRE(!client.peek(1));
        client.consume(20);
        REQUIRE(!client.peek(1));
    }

    SECTION("Typed peeks copy from an unaligned buffer offset") {
        constexpr uint32_t expected = 0x12345678U;
        std::array<std::byte, sizeof(uint8_t) + sizeof(expected)> msg{};
        msg[0] = std::byte{0xFF};
        std::memcpy(msg.data() + 1, &expected, sizeof(expected));
        server.send(msg);

        REQUIRE(client.peek(msg.size()));
        client.consume(1);
        auto actual = client.peekCopy<uint32_t>();
        REQUIRE(actual);
        REQUIRE(*actual == expected);
    }

    SECTION("Compacts unread data without corruption") {
        std::vector<std::byte> msg;
        msg.reserve(20'000);

        // Use non-repeating data so stale bytes from before compaction cannot
        // accidentally compare equal to the unread fragment.
        uint32_t value = 1;
        for (int i = 0; i < 20'000; ++i) {
            value = value * 1664525U + 1013904223U;
            msg.push_back(static_cast<std::byte>(value >> 24));
        }
        server.send(msg);

        size_t offset = 0;
        while (offset < msg.size()) {
            size_t bytes_to_read = std::min<size_t>(7, msg.size() - offset);
            auto stop = funx::Clock::now() + std::chrono::seconds(1);
            std::optional<funx::byte_view> received;
            while (!received && funx::Clock::now() < stop) {
                received = client.peek(bytes_to_read);
            }

            REQUIRE(received);
            REQUIRE(std::ranges::equal(*received,
                funx::byte_view(msg.data() + offset, bytes_to_read)));
            client.consume(bytes_to_read);
            offset += bytes_to_read;
        }
    }
}

