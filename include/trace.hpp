#pragma once

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cachesim {

inline std::uint64_t parse_unsigned(const std::string& text, int base) {
    if (text.empty() || text.front() == '-' || text.front() == '+')
        throw std::invalid_argument("expected an unsigned number: " + text);
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, base);
    if (consumed != text.size()) throw std::invalid_argument("invalid number: " + text);
    return value;
}

struct Record { char operation; std::uint64_t address; std::uint64_t bytes; };

// Addresses are hexadecimal; sizes are decimal. Empty/comment lines are skipped.
inline bool parse_record(std::string text, Record& record) {
    text = text.substr(0, text.find('#'));
    std::istringstream input(text);
    std::string operation, address, bytes, extra;
    if (!(input >> operation)) return false;
    if (operation != "R" && operation != "W")
        throw std::invalid_argument("operation must be R or W");
    if (!(input >> address)) throw std::invalid_argument("missing hexadecimal address");
    record = {operation.front(), parse_unsigned(address, 16), 1};
    if (input >> bytes) record.bytes = parse_unsigned(bytes, 10);
    if (input >> extra) throw std::invalid_argument("unexpected field: " + extra);
    if (!record.bytes || record.bytes > 1048576)
        throw std::invalid_argument("record size must be between 1 and 1,048,576 bytes");
    return true;
}

}  // namespace cachesim
