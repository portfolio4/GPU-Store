#ifndef FUNX_MATCH_TEST_CATCH_PRINTERS_H
#define FUNX_MATCH_TEST_CATCH_PRINTERS_H
#include <funx/match/proto.h>

#include <ostream>
#include <print>

std::ostream& operator<<(std::ostream& os, funx::match::Order const& o) {
    os << std::format("{}", o);
    return os;
}

std::ostream& operator<<(std::ostream& os, funx::match::Event const& e) {
    os << std::format("{}", e);
    return os;
}

#endif  // FUNX_MATCH_TEST_CATCH_PRINTERS_H

