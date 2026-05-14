#include <catch2/catch_all.hpp>

#include <flip/flip.h>
#include <flip/util.h>
#include <flop/flop.h>
#include <flop/util.h>
#include <funx/net/util.h>

#include <cstring>

TEST_CASE("FLOP and FLIP reject inconsistent message lengths", "[protocol]")
{
    SECTION("FLOP") {
        flop::MessageT<flop::Heartbeat> msg{};
        bool handled = false;
        auto handler = [&](auto const&) { handled = true; };

        msg.header.msg_size = 1;
        REQUIRE_FALSE(flop::handleMsg(funx::util::to_bytes(msg), handler));
        REQUIRE_FALSE(handled);

        msg.header.msg_size = sizeof(msg) - sizeof(msg.header.msg_size);
        std::array<std::byte, sizeof(msg) + 1> oversized{};
        std::memcpy(oversized.data(), &msg, sizeof(msg));
        REQUIRE_FALSE(flop::handleMsg(oversized, handler));
        REQUIRE_FALSE(handled);
    }

    SECTION("FLIP") {
        flip::MessageT<flip::Heartbeat> msg{};
        bool handled = false;
        auto handler = [&](auto const&) { handled = true; };

        msg.header.msg_size = 1;
        REQUIRE_FALSE(flip::handleMsg(funx::util::to_bytes(msg), handler));
        REQUIRE_FALSE(handled);

        msg.header.msg_size = sizeof(msg) - sizeof(msg.header.msg_size);
        std::array<std::byte, sizeof(msg) + 1> oversized{};
        std::memcpy(oversized.data(), &msg, sizeof(msg));
        REQUIRE_FALSE(flip::handleMsg(oversized, handler));
        REQUIRE_FALSE(handled);
    }
}
