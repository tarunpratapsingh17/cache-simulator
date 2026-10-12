#include "cache.hpp"
#include "trace.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace cachesim;

static void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static void rejects(const std::function<void()>& action) {
    bool threw = false;
    try { action(); } catch (const std::exception&) { threw = true; }
    check(threw, "expected rejection");
}

int main() {
    int passed = 0;
    auto test = [&](const std::string& name, const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; throw; }
    };
    try {
        test("direct-mapped conflict and line offsets", [] {
            Cache c({16, 4, 1});
            c.access('R', 0); c.access('R', 3); c.access('R', 16); c.access('R', 0);
            check(c.stats().hits == 1 && c.stats().misses == 3 && c.stats().evictions == 2, "conflict counts");
        });
        test("LRU differs from FIFO after a hit", [] {
            Cache lru({8, 4, 2, Replacement::LRU});
            Cache fifo({8, 4, 2, Replacement::FIFO});
            for (auto address : {0ULL, 4ULL, 0ULL, 8ULL, 0ULL}) {
                lru.access('R', address); fifo.access('R', address);
            }
            check(lru.stats().hits == 2 && fifo.stats().hits == 1, "replacement order");
        });
        test("dirty eviction writes a complete line", [] {
            Cache c({8, 4, 1});
            c.access('W', 0, 2); c.access('R', 8);
            check(c.stats().writebacks == 1 && c.stats().memory_write_bytes == 4 && c.stats().memory_read_bytes == 8, "writeback traffic");
        });
        test("write-through writes only requested bytes", [] {
            Config config{8, 4, 1}; config.write_policy = WritePolicy::WriteThrough;
            Cache c(config); c.access('W', 0, 2); c.access('W', 1, 1);
            check(c.stats().memory_write_bytes == 3 && c.stats().memory_read_bytes == 4 && c.dirty_lines() == 0, "write-through traffic");
        });
        test("no-write-allocate bypasses without displacing", [] {
            Config config{4, 4, 1}; config.write_allocate = false;
            Cache c(config); c.access('R', 0); c.access('W', 4, 3); c.access('R', 0);
            check(c.stats().hits == 1 && c.stats().evictions == 0 && c.stats().memory_write_bytes == 3, "bypass behavior");
        });
        test("read allocation still occurs with no-write-allocate", [] {
            Config config{4, 4, 1}; config.write_allocate = false;
            Cache c(config); c.access('W', 0); c.access('R', 0); c.access('R', 0);
            check(c.stats().misses == 2 && c.stats().hits == 1 && c.stats().memory_read_bytes == 4, "read fill");
        });
        test("accesses spanning lines are split", [] {
            Cache c({16, 4, 1}); c.access('R', 3, 6); c.access('R', 3, 6);
            check(c.stats().records == 2 && c.stats().accesses == 6 && c.stats().hits == 3 && c.stats().misses == 3, "split counts");
        });
        test("sets do not evict each other", [] {
            Cache c({16, 4, 2});
            for (auto address : {0ULL, 8ULL, 4ULL, 12ULL, 0ULL, 8ULL, 4ULL, 12ULL}) c.access('R', address);
            check(c.stats().hits == 4 && c.stats().evictions == 0, "independent sets");
        });
        test("highest 64-bit address is valid", [] {
            Cache c({8, 4, 1}); const auto max = std::numeric_limits<std::uint64_t>::max();
            c.access('R', max); c.access('R', max - 1, 2);
            check(c.stats().hits == 1, "high address");
            rejects([&] { c.access('R', max, 2); });
        });
        test("invalid configuration is rejected", [] {
            rejects([] { Cache c({0, 4, 1}); }); rejects([] { Cache c({12, 4, 1}); });
            rejects([] { Cache c({8, 16, 1}); }); rejects([] { Cache c({8, 4, 4}); });
            rejects([] { Cache c({2097152, 1, 1}); });
        });
        test("invalid operations do not change statistics", [] {
            Cache c; rejects([&] { c.access('X', 0); }); rejects([&] { c.access('R', 0, 0); });
            check(c.stats().records == 0, "invalid access mutated statistics");
        });
        test("trace parser handles comments and optional size", [] {
            Record r{};
            check(!parse_record(" # comment", r), "comment line");
            check(parse_record("W 0x1f 4 # store", r) && r.operation == 'W' && r.address == 31 && r.bytes == 4, "full record");
            check(parse_record("R ff", r) && r.address == 255 && r.bytes == 1, "default size");
        });
        test("malformed traces are rejected", [] {
            Record r{};
            for (const auto& text : {"R", "X 0", "R -1", "W gg", "R 1 0", "R 1 2 extra", "R 1 1048577", "R 0x10000000000000000"})
                rejects([&] { parse_record(text, r); });
        });
        test("resident dirty lines are not implicitly flushed", [] {
            Cache c({8, 4, 1}); c.access('W', 0);
            check(c.dirty_lines() == 1 && c.stats().memory_write_bytes == 0, "end-of-trace semantics");
        });
    } catch (...) { return 1; }
    std::cout << passed << " test groups passed\n";
}
