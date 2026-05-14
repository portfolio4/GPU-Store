#include <funx/config/Config.h>

#include <funx/util/expected.h>

#include <expected>
#include <format>
#include <fstream>

namespace funx {

JsonConfig::JsonConfig(std::string const& filename)
{
    std::ifstream f(filename);
    data_ = nlohmann::json::parse(f);
}

const JsonConfig::Data& JsonConfig::data() const {
    return data_;
}

std::expected<nlohmann::json, std::string>
JsonConfig::get_instance(std::string const& instance) const {
    std::string instance_path = std::format("/{}", instance);
    auto res = data().value(nlohmann::json::json_pointer(instance_path), nlohmann::json{});
    if (res.is_null()) {
        return std::unexpected(std::format(
                "Missing instance '{}' in config", instance));
    }
    return res;
}

std::expected<nlohmann::json, std::string>
JsonConfig::get(nlohmann::json const& data, std::string const& path) {
    auto res = data.value(nlohmann::json::json_pointer(path), nlohmann::json{});
    if (res.is_null()) {
        return std::unexpected(std::format(
                "Missing property '{}'", path));
    }
    return res;
}

std::expected<nlohmann::json, std::string>
JsonConfig::get(std::string const& instance, std::string const& path) const {
    auto data = FUNX_TRY(get_instance(instance));
    return JsonConfig::get(data, path).transform_error([&](std::string const& err) {
        return std::format("Config error for instance '{}': {}", instance, err);
    });
}

std::expected<std::string, std::string> JsonConfig::get_str(
        std::string const& instance, std::string const& path) const
{
    auto data = FUNX_TRY(get(instance, path));
    if (data.type() != nlohmann::json::value_t::string) {
        return std::unexpected(std::format(
                    "Instance '{}' property '{}' is not a string", instance, path));
    }
    return data;
}

std::expected<std::vector<std::string>, std::string>
JsonConfig::get_vector_str(std::string const& instance, std::string const& path) const
{
    auto data = FUNX_TRY(get(instance, path));
    if (!data.is_array()) {
        return std::unexpected(std::format(
            "Instance '{}' property '{}' is not an array", instance, path));
    }
    std::vector<std::string> rtn;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it->is_string()) {
            rtn.push_back(*it);
        } else {
            return std::unexpected(std::format(
                "Instance '{}' property '{}' element {} is not a string",
                instance, path, (it - data.begin())));
        }
    }
    return rtn;
}


} // namepsace funx

