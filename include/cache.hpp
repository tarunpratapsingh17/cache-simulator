#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cachesim {

enum class Replacement { LRU, FIFO };
enum class WritePolicy { WriteBack, WriteThrough };

struct Config {
    std::uint64_t capacity = 32768;
    std::uint64_t line_size = 64;
    std::uint64_t ways = 4;
    Replacement replacement = Replacement::LRU;
    WritePolicy write_policy = WritePolicy::WriteBack;
    bool write_allocate = true;

    void validate() const {
        const auto power_of_two = [](std::uint64_t x) { return x && !(x & (x - 1)); };
        if (!power_of_two(capacity) || !power_of_two(line_size) || !power_of_two(ways))
            throw std::invalid_argument("capacity, line size, and ways must be positive powers of two");
        if (line_size > capacity || ways > capacity / line_size)
            throw std::invalid_argument("cache must contain at least one complete set");
        if (capacity / line_size > 1048576)
            throw std::invalid_argument("at most 1,048,576 simulated cache lines are supported");
    }
};

struct Stats {
    std::uint64_t records = 0;
    std::uint64_t accesses = 0;  // Block accesses: one trace record may cross lines.
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t writebacks = 0;
    std::uint64_t memory_read_bytes = 0;
    std::uint64_t memory_write_bytes = 0;
};

class Cache {
    struct Line {
        bool valid = false;
        bool dirty = false;
        std::uint64_t tag = 0;
        std::uint64_t inserted = 0;
        std::uint64_t touched = 0;
    };

    Config config_;
    Stats stats_;
    std::uint64_t sets_ = 0;
    std::uint64_t clock_ = 0;
    std::vector<Line> lines_;

    void access_block(char operation, std::uint64_t address, std::uint64_t bytes) {
        const auto block = address / config_.line_size;
        const auto set = block % sets_;
        const auto tag = block / sets_;
        const auto first = static_cast<std::size_t>(set * config_.ways);
        const auto last = first + static_cast<std::size_t>(config_.ways);
        const bool write = operation == 'W';
        ++clock_;
        ++stats_.accesses;
        if (write) ++stats_.writes;
        else ++stats_.reads;

        for (auto i = first; i < last; ++i) {
            auto& line = lines_[i];
            if (line.valid && line.tag == tag) {
                ++stats_.hits;
                line.touched = clock_;
                if (write) {
                    if (config_.write_policy == WritePolicy::WriteBack) line.dirty = true;
                    else stats_.memory_write_bytes += bytes;
                }
                return;
            }
        }

        ++stats_.misses;
        if (write && !config_.write_allocate) {
            stats_.memory_write_bytes += bytes;
            return;
        }

        auto victim = first;
        for (auto i = first; i < last; ++i) {
            if (!lines_[i].valid) { victim = i; break; }
            const auto age = config_.replacement == Replacement::LRU
                ? lines_[i].touched : lines_[i].inserted;
            const auto oldest = config_.replacement == Replacement::LRU
                ? lines_[victim].touched : lines_[victim].inserted;
            if (age < oldest) victim = i;
        }

        auto& line = lines_[victim];
        if (line.valid) {
            ++stats_.evictions;
            if (line.dirty) {
                ++stats_.writebacks;
                stats_.memory_write_bytes += config_.line_size;
            }
        }
        stats_.memory_read_bytes += config_.line_size;
        line = {true, write && config_.write_policy == WritePolicy::WriteBack,
                tag, clock_, clock_};
        if (write && config_.write_policy == WritePolicy::WriteThrough)
            stats_.memory_write_bytes += bytes;
    }

public:
    explicit Cache(Config config = {}) : config_(config) {
        config_.validate();
        sets_ = config_.capacity / config_.line_size / config_.ways;
        lines_.resize(static_cast<std::size_t>(config_.capacity / config_.line_size));
    }

    void access(char operation, std::uint64_t address, std::uint64_t bytes = 1) {
        if (operation != 'R' && operation != 'W')
            throw std::invalid_argument("operation must be R or W");
        if (!bytes || address > std::numeric_limits<std::uint64_t>::max() - (bytes - 1))
            throw std::invalid_argument("access size is zero or address range overflows 64 bits");
        ++stats_.records;
        while (bytes) {
            const auto chunk = std::min(bytes, config_.line_size - address % config_.line_size);
            access_block(operation, address, chunk);
            bytes -= chunk;
            if (bytes) address += chunk;  // Avoid wrapping after the final byte at UINT64_MAX.
        }
    }

    const Stats& stats() const { return stats_; }
    const Config& config() const { return config_; }
    std::uint64_t sets() const { return sets_; }
    std::uint64_t dirty_lines() const {
        return static_cast<std::uint64_t>(std::count_if(lines_.begin(), lines_.end(),
            [](const Line& line) { return line.valid && line.dirty; }));
    }
};

}  // namespace cachesim
