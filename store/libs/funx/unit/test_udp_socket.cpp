#include <catch2/catch_all.hpp>

#include <funx/net/UdpSocket.h>

TEST_CASE("Test UdpSocket", "[funxlib][udp]")
{
    SECTION("Unicast") {
        auto s1 = funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = "127.0.0.1",
            .local_port = funx::UdpPort{8000},
            .remote_ip4 = "127.0.0.1",
            .remote_port = funx::UdpPort{9000},
        });
        auto s2 = funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = "127.0.0.1",
            .local_port = funx::UdpPort{9000},
            .remote_ip4 = "127.0.0.1",
            .remote_port = funx::UdpPort{8000},
        });

        const char* msg1 = "Hello world";
        REQUIRE(s1.trySend(funx::str_to_byte_view(msg1)));
        auto maybe_read2 = s2.tryRead();
        REQUIRE(maybe_read2);
        REQUIRE(funx::byte_to_str_view(*maybe_read2) == msg1);
        REQUIRE(!s2.tryRead());

        const char* msg2 = "Goodbye";
        for (int i = 0; i < 5; ++i) {
            REQUIRE(s2.trySend(funx::str_to_byte_view(msg2)));
        }
        for (int i = 0; i < 5; ++i) {
            auto maybe_read1 = s1.tryRead();
            REQUIRE(maybe_read1);
            REQUIRE(funx::byte_to_str_view(*maybe_read1) == msg2);
        }
        REQUIRE(!s1.tryRead());
    }

    SECTION("Multicast") {
        auto sender = funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = std::nullopt,
            .local_port = funx::UdpPort{8000},
            .remote_ip4 = "226.1.1.1",
            .remote_port = funx::UdpPort{9000},
        });
        auto receiver1 = funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = "226.1.1.1",
            .local_port = funx::UdpPort{9000},
            .remote_ip4 = std::nullopt,
            .remote_port = std::nullopt,
        });
        auto receiver2 = funx::UdpSocket::createOrThrow(funx::UdpSocket::Config{
            .local_ip4 = "226.1.1.1",
            .local_port = funx::UdpPort{9000},
            .remote_ip4 = std::nullopt,
            .remote_port = std::nullopt,
        });


        const char* msg = "This is a multicast msg";
        for (int i = 0; i < 5; ++i) {
            REQUIRE(sender.trySend(funx::str_to_byte_view(msg)));
        }

        for (int i = 0; i < 5; ++i) {
            auto maybe_read1 = receiver1.tryRead();
            REQUIRE(maybe_read1);
            REQUIRE(funx::byte_to_str_view(*maybe_read1) == msg);

            auto maybe_read2 = receiver2.tryRead();
            REQUIRE(maybe_read2);
            REQUIRE(funx::byte_to_str_view(*maybe_read2) == msg);
        }
        REQUIRE(!receiver1.tryRead());
        REQUIRE(!receiver2.tryRead());
    }
}

