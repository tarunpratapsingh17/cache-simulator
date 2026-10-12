#include "cache.hpp"
#include "trace.hpp"

#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace cachesim;

static void help() {
    std::cout << "Cache Simulator - single-level trace replay\n\n"
              << "Usage: cache_sim --trace FILE [options]\n"
              << "  --size BYTES             Cache capacity (default 32768)\n"
              << "  --line BYTES             Cache line size (default 64)\n"
              << "  --ways N                 Associativity (default 4)\n"
              << "  --policy lru|fifo        Replacement (default lru)\n"
              << "  --write-policy write-back|write-through  (default write-back)\n"
              << "  --write-allocate yes|no  Allocate on write miss (default yes)\n"
              << "  --csv FILE               Write one CSV row with configuration and statistics\n"
              << "  --help                   Show this message\n\n"
              << "Trace: R|W HEX_ADDRESS [DECIMAL_BYTES], with optional # comments.\n";
}

static void csv(std::ostream& out, const Cache& cache) {
    const auto& c = cache.config();
    const auto& s = cache.stats();
    out << "capacity_bytes,line_bytes,ways,sets,replacement,write_policy,write_allocate,"
           "records,accesses,reads,writes,hits,misses,hit_rate,evictions,writebacks,"
           "memory_read_bytes,memory_write_bytes,dirty_lines\n";
    out << c.capacity << ',' << c.line_size << ',' << c.ways << ',' << cache.sets() << ','
        << (c.replacement == Replacement::LRU ? "lru" : "fifo") << ','
        << (c.write_policy == WritePolicy::WriteBack ? "write-back" : "write-through") << ','
        << (c.write_allocate ? "yes" : "no") << ',' << s.records << ',' << s.accesses << ','
        << s.reads << ',' << s.writes << ',' << s.hits << ',' << s.misses << ','
        << std::setprecision(10) << (s.accesses ? static_cast<double>(s.hits) / s.accesses : 0.0)
        << ',' << s.evictions << ',' << s.writebacks << ',' << s.memory_read_bytes << ','
        << s.memory_write_bytes << ',' << cache.dirty_lines() << '\n';
}

int main(int argc, char** argv) {
    try {
        Config config;
        std::string trace_path, csv_path;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") { help(); return 0; }
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
            const std::string value = argv[++i];
            if (key == "--trace") trace_path = value;
            else if (key == "--csv") csv_path = value;
            else if (key == "--size") config.capacity = parse_unsigned(value, 10);
            else if (key == "--line") config.line_size = parse_unsigned(value, 10);
            else if (key == "--ways") config.ways = parse_unsigned(value, 10);
            else if (key == "--policy") {
                if (value == "lru") config.replacement = Replacement::LRU;
                else if (value == "fifo") config.replacement = Replacement::FIFO;
                else throw std::invalid_argument("policy must be lru or fifo");
            } else if (key == "--write-policy") {
                if (value == "write-back") config.write_policy = WritePolicy::WriteBack;
                else if (value == "write-through") config.write_policy = WritePolicy::WriteThrough;
                else throw std::invalid_argument("write policy must be write-back or write-through");
            } else if (key == "--write-allocate") {
                if (value != "yes" && value != "no") throw std::invalid_argument("write-allocate must be yes or no");
                config.write_allocate = value == "yes";
            } else throw std::invalid_argument("unknown option: " + key);
        }
        if (trace_path.empty()) throw std::invalid_argument("--trace is required; use --help for usage");
        if (!csv_path.empty() && csv_path == trace_path)
            throw std::invalid_argument("CSV output must differ from the trace path");
        Cache cache(config);
        std::ifstream trace(trace_path);
        if (!trace) throw std::runtime_error("cannot open trace: " + trace_path);
        if (!csv_path.empty()) {
            std::error_code error;
            if (std::filesystem::equivalent(trace_path, csv_path, error))
                throw std::invalid_argument("CSV output must not overwrite the input trace");
        }
        std::string line;
        std::uint64_t line_number = 0;
        while (std::getline(trace, line)) {
            ++line_number;
            try {
                Record record{};
                if (parse_record(line, record)) cache.access(record.operation, record.address, record.bytes);
            } catch (const std::exception& error) {
                throw std::runtime_error("trace line " + std::to_string(line_number) + ": " + error.what());
            }
        }
        if (trace.bad()) throw std::runtime_error("error reading trace");
        const auto& s = cache.stats();
        std::cout << "Configuration: " << config.capacity << " bytes, " << config.line_size
                  << "-byte lines, " << config.ways << " ways, " << cache.sets() << " sets\n"
                  << "Trace records: " << s.records << "\nBlock accesses: " << s.accesses
                  << "\nHits: " << s.hits << "\nMisses: " << s.misses << "\nHit rate: "
                  << std::fixed << std::setprecision(2)
                  << (s.accesses ? 100.0 * static_cast<double>(s.hits) / s.accesses : 0.0) << "%\n"
                  << "Evictions: " << s.evictions << "\nDirty writebacks: " << s.writebacks
                  << "\nMemory read bytes: " << s.memory_read_bytes
                  << "\nMemory write bytes: " << s.memory_write_bytes
                  << "\nDirty lines remaining: " << cache.dirty_lines() << '\n';
        if (!csv_path.empty()) {
            std::ofstream output(csv_path);
            if (!output) throw std::runtime_error("cannot open CSV output: " + csv_path);
            csv(output, cache);
            output.close();
            if (!output) throw std::runtime_error("error writing CSV output");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}
