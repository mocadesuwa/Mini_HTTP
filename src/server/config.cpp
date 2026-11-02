#include "mini_http/server/config.h"

#include <charconv>
#include <string_view>
#include <system_error>
#include <limits>

namespace mini_http {
namespace {

bool parse_positive_integer(
    std::string_view text,
    std::size_t max_value,
    std::size_t& value) {
    if(text.empty()) {
        return false;
    }

    std::size_t parse_value = 0;
    const char* first = text.data();
    const char* last = first + text.size();

    const std::from_chars_result result =
        std::from_chars(first, last, parse_value);

    if(result.ec != std::errc{} ||
        result.ptr != last ||
        parse_value == 0 ||
        parse_value > max_value) {

        return false;
    }

    value = parse_value;

    return true;
}

} // inner namespace

ConfigParseResult parse_arguments(
    int argc,
    const char *const argv[],
    ServerConfig &config,
    std::string &error) {

    error.clear();
    ServerConfig parsed_config{};

    for(int index = 1; index < argc; index++) {
        const std::string_view option = argv[index];

        if(option == "--help") {
            return ConfigParseResult::Help;
        }

        if(option != "--port" &&
            option != "--root" &&
            option != "--threads" &&
            option != "--idle-timeout") {

            error = std::string("unknown option: ") + std::string(option);

            return ConfigParseResult::Error;
        }

        if(index + 1 >= argc) {
            error = std::string("missing value for ") + std::string(option);

            return ConfigParseResult::Error;
        }

        const std::string_view argument_value = argv[++index];

        if(option == "--port") {
            std::size_t number = 0;

            if(!parse_positive_integer(argument_value, 65535, number)) {
                error = "invalid value for --port";
                return ConfigParseResult::Error;
            }

            parsed_config.port = static_cast<std::uint16_t>(number);
        } else if(option == "--threads") {
            std::size_t number = 0;

            if(!parse_positive_integer(
                argument_value,
                std::numeric_limits<std::size_t>::max(),
                number)) {

                error = "invalid value for --threads";
                return ConfigParseResult::Error;
            }

            parsed_config.thread_count = number;
        } else if(option == "--root") {
            if(argument_value.empty()) {
                error = "empty value for --root";
                return ConfigParseResult::Error;
            }

            parsed_config.document_root =
                std::filesystem::path(argument_value);
        } else if(option == "--idle-timeout") {
            using SecondsRep = std::chrono::seconds::rep;

            const std::size_t max_seconds =
                static_cast<std::size_t>(
                    std::numeric_limits<SecondsRep>::max());

            std::size_t number = 0;

            if(!parse_positive_integer(
                argument_value,
                max_seconds,
                number)) {

                error = "invalid value for --idle-timeout";
                return ConfigParseResult::Error;
            }
            parsed_config.idle_timeout =
                std::chrono::seconds{static_cast<SecondsRep>(number)};
        }
    }

    config = parsed_config;
    return ConfigParseResult::Success;
}



} // namespace