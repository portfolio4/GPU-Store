#include <catch2/catch_all.hpp>

#include <future>

#include <funx/clock/Clock.h>
#include <funx/net/TcpServerSocket.h>

TEST_CASE("Test TcpSocket", "[funxlib]")
{
    const funx::TcpPort SERVER_PORT{9999};
    funx::TcpServerSocket server_socket(funx::TcpServerSocket::Config{
        .server_port = SERVER_PORT,
    });
    const std::string CLIENT_TO_SERVER_MESSAGE = "hello world";
    const std::string SERVER_TO_CLIENT_MESSAGE = "received!";


    SECTION("Fail to connect") {
        REQUIRE_FALSE(funx::TcpSocket::create(funx::TcpSocket::Config{
            .ip4="127.0.0.2",
            .port=funx::TcpPort{1234},
        }));
    }

    SECTION("Server accepts many clients") {
        bool server_done = false;
        constexpr int NUM_CLIENTS = 5;

        std::thread server_thread([&]() {
            auto stop = funx::Clock::now() + std::chrono::seconds(5);
            int num_clients_connected = 0;
            while (funx::Clock::now() < stop && num_clients_connected < NUM_CLIENTS) {
                auto s = server_socket.maybeAccept();
                if (s) {
                    while (funx::Clock::now() < stop) {
                        auto read_result = s->tryRead();
                        if (read_result) {
                            REQUIRE(funx::byte_to_str_view(*read_result) ==
                                    CLIENT_TO_SERVER_MESSAGE);
                            ++num_clients_connected;
                            break;
                        }
                    }
                    break;
                }
            }
            server_done = true;
        });

        std::vector<std::thread> client_threads;
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            client_threads.push_back(std::thread([&]() {
                auto client = funx::TcpSocket::createOrThrow(funx::TcpSocket::Config{
                    .ip4="127.0.0.1",
                    .port=SERVER_PORT
                });
                client.send(funx::str_to_byte_view(CLIENT_TO_SERVER_MESSAGE));
                auto stop = funx::Clock::now() + std::chrono::seconds(5);

                // keep connection alive
                while (funx::Clock::now() < stop && !server_done) {}
            }));
        }

        server_thread.join();
        for (auto& t : client_threads) {
            t.join();
        }
    }

    SECTION("Client connects, sends message and waits for response") {
        std::thread client_thread([&]() {
            auto client = funx::TcpSocket::createOrThrow(funx::TcpSocket::Config{
                .ip4 = "127.0.0.1",
                .port = SERVER_PORT,
            });

            client.send(funx::str_to_byte_view(CLIENT_TO_SERVER_MESSAGE));

            auto stop = funx::Clock::now() + std::chrono::seconds(5);
            std::optional<funx::byte_view> server_response;
            while (funx::Clock::now() < stop) {
                auto read_result = client.tryRead();
                if (read_result) {
                    server_response = read_result;
                    break;
                }
            }
            REQUIRE(server_response);
            REQUIRE(funx::byte_to_str_view(*server_response) ==
                    SERVER_TO_CLIENT_MESSAGE);
        });

        std::thread server_thread([&]() {
            auto stop = funx::Clock::now() + std::chrono::seconds(5);

            // try to accept a connection from client
            std::optional<funx::TcpSocket> server;
            while (funx::Clock::now() < stop) {
                auto accept_result = server_socket.maybeAccept();
                if (accept_result) {
                    server = std::move(accept_result);
                    break;
                }
            }
            REQUIRE(server);

            // wait for a message from the client
            std::optional<funx::byte_view> client_message;
            while (funx::Clock::now() < stop) {
                auto read_result = server->tryRead();
                if (read_result) {
                    client_message = read_result;
                    break;
                }
            }

            REQUIRE(client_message);
            REQUIRE(funx::byte_to_str_view(*client_message) ==
                    CLIENT_TO_SERVER_MESSAGE);

            server->send(funx::str_to_byte_view(SERVER_TO_CLIENT_MESSAGE));
        });

        client_thread.join();
        server_thread.join();
    }
}

