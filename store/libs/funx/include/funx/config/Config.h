#ifndef FUNX_CONFIG_H
#define FUNX_CONFIG_H

#include <funx/util/expected.h>

#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace funx {

// Application configs are structured like
//   {
//      "ServiceName_0": {
//         "property1": <value1>,
//         "property2": [<value2.1>, <value2.2>, ...],
//         "property3": {
//             "property3.1": <value3.1>,
//             "property3.2": <value3.1>,
//             ...
//         }
//      },
//      "ServiceName_1" {
//         ...
//      }
//   }
//
// Most apps just need calls of the form
//     `get_<type>("ServiceName_0", "/property1")`
// to get the value <value1>. Or calls of the form
//     `get_vector_<type>("ServiceName_0", "/property2")`
// to get the [<value2.1>, <value2.2>] array.
//
// But if you need to operate on a nested object, e.g. property3 in the above
// example, then you use
//     `get("ServiceName_0", "/property3")`
// to get the nlohmann:json object representing property3. Then operate on
// the json data with
//     `JsongConfig::get(json_data, "/property3.1");`
class JsonConfig
{
public:
    using Data = nlohmann::json;

    JsonConfig(std::string const& filename);

    const Data& data() const;

    std::expected<nlohmann::json, std::string> get_instance(
            std::string const& instance) const;

    static std::expected<nlohmann::json, std::string> get(
            nlohmann::json const& json,
            std::string const& path);
    std::expected<nlohmann::json, std::string> get(
            std::string const& instance,
            std::string const& path) const;

    std::expected<std::string, std::string> get_str(
            std::string const& instance,
            std::string const& path) const;

    template <std::integral T>
    static std::expected<T, std::string> get_int(
            nlohmann::json const& data,
            std::string const& path)
    {
        if (data.type() != nlohmann::json::value_t::number_integer &&
            data.type() != nlohmann::json::value_t::number_unsigned) {
            return std::unexpected(std::format(
                "Property '{}' is not an integer", path));
        }
        auto out_of_range = [&]() -> std::expected<T, std::string> {
            return std::unexpected(std::format(
                "Property '{}' value {} is outside the range [{}, {}]",
                path, data.dump(), std::numeric_limits<T>::min(),
                std::numeric_limits<T>::max()));
        };
        if (data.type() == nlohmann::json::value_t::number_integer) {
            auto value = data.get<int64_t>();
            if (!std::in_range<T>(value)) {
                return out_of_range();
            }
            return static_cast<T>(value);
        } else {
            auto value = data.get<uint64_t>();
            if (!std::in_range<T>(value)) {
                return out_of_range();
            }
            return static_cast<T>(value);
        }
    }

    template <std::integral T>
    std::expected<T, std::string> get_int(
            std::string const& instance,
            std::string const& path) const
    {
        auto data = FUNX_TRY(get(instance, path));
        return JsonConfig::get_int<T>(data, path).transform_error([&](std::string err) {
            return std::format("Config error for instance '{}': {}", instance, err);
        });
    }

    template <std::integral T>
    std::expected<std::vector<T>, std::string>
    get_vector_int(std::string const& instance, std::string const& path) const
    {
        auto data = FUNX_TRY(get(instance, path));
        if (!data.is_array()) {
            return std::unexpected(std::format(
                "Instance '{}' property '{}' is not an array", instance, path));
        }
        std::vector<T> rtn;
        for (auto it = data.begin(); it != data.end(); ++it) {
            size_t index = it - data.begin();
            auto value = JsonConfig::get_int<T>(*it, std::format("{}[{}]", path, index));
            if (!value) {
                return std::unexpected(std::format(
                    "Config error for instance '{}': {}", instance, value.error()));
            }
            rtn.push_back(*value);
        }
        return rtn;
    }

    std::expected<std::vector<std::string>, std::string>
    get_vector_str(std::string const& instance, std::string const& path) const;

private:
    Data data_;
};

} // namespace funx

#endif

