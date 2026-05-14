#include <catch2/catch_all.hpp>

#include <funx/config/Config.h>
#include <funx/test/scratch_dir.h>

#include <fstream>

TEST_CASE("Configuration integers must fit their target type", "[funxlib][config]")
{
    using funx::JsonConfig;

    REQUIRE(JsonConfig::get_int<uint8_t>(nlohmann::json(0), "/value") == 0);
    REQUIRE(JsonConfig::get_int<uint8_t>(nlohmann::json(255), "/value") == 255);
    REQUIRE_FALSE(JsonConfig::get_int<uint8_t>(nlohmann::json(-1), "/value"));
    REQUIRE_FALSE(JsonConfig::get_int<uint8_t>(nlohmann::json(256), "/value"));

    REQUIRE(JsonConfig::get_int<int8_t>(nlohmann::json(-128), "/value") == -128);
    REQUIRE(JsonConfig::get_int<int8_t>(nlohmann::json(127), "/value") == 127);
    REQUIRE_FALSE(JsonConfig::get_int<int8_t>(nlohmann::json(-129), "/value"));
    REQUIRE_FALSE(JsonConfig::get_int<int8_t>(nlohmann::json(128), "/value"));

    nlohmann::json unsigned_max = std::numeric_limits<uint64_t>::max();
    REQUIRE(JsonConfig::get_int<uint64_t>(unsigned_max, "/value") ==
            std::numeric_limits<uint64_t>::max());
    REQUIRE_FALSE(JsonConfig::get_int<int64_t>(unsigned_max, "/value"));

    funx::test::ScratchDir scratch_dir;
    auto config_path = scratch_dir.path() / "config.json";
    {
        std::ofstream config_file(config_path);
        config_file << R"({"test":{"values":[0,255,256]}})";
    }
    JsonConfig config(config_path);
    auto values = config.get_vector_int<uint8_t>("test", "/values");
    REQUIRE_FALSE(values);
    REQUIRE(values.error().contains("/values[2]"));
    scratch_dir.cleanup();
}
