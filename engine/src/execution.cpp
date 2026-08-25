#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "laplace/execution.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace laplace::execution {

namespace {

constexpr std::uint32_t known_processor_flags =
    LAPLACE_EXECUTION_PROCESSOR_ONLINE | LAPLACE_EXECUTION_PROCESSOR_ALLOWED;
constexpr std::uint32_t known_topology_flags =
    LAPLACE_EXECUTION_TOPOLOGY_AFFINITY_CONSTRAINED |
    LAPLACE_EXECUTION_TOPOLOGY_MEMORY_CONSTRAINED |
    LAPLACE_EXECUTION_TOPOLOGY_HYBRID_CORES;
constexpr std::uint64_t known_isa_flags =
    LAPLACE_EXECUTION_ISA_X86_SSE2 | LAPLACE_EXECUTION_ISA_X86_AVX |
    LAPLACE_EXECUTION_ISA_X86_AVX2 | LAPLACE_EXECUTION_ISA_X86_AVX512F |
    LAPLACE_EXECUTION_ISA_X86_FMA | LAPLACE_EXECUTION_ISA_X86_BMI2 |
    LAPLACE_EXECUTION_ISA_ARM_NEON;

struct Observation final {
    std::vector<laplace_execution_processor> processors;
    std::vector<laplace_execution_cache> caches;
    std::vector<std::uint32_t> cache_processor_ids;
    std::vector<laplace_execution_memory_domain> memory_domains;
    std::uint64_t total_memory_bytes{};
    std::uint64_t usable_memory_bytes{};
    std::uint64_t page_bytes{};
    std::uint64_t isa_flags{};
    std::uint32_t flags{};
};

class TopologyObservationProvider {
public:
    virtual ~TopologyObservationProvider() = default;
    [[nodiscard]] virtual laplace_execution_status observe(Observation& output) const = 0;
};

template <typename Value>
struct MeasuredValue final {
    Value value{};
    bool present{};

    MeasuredValue() = default;
    MeasuredValue(Value observed) : value(std::move(observed)), present(true) {}

    [[nodiscard]] bool has_value() const noexcept { return present; }
    [[nodiscard]] const Value& operator*() const noexcept { return value; }
    [[nodiscard]] const Value* operator->() const noexcept { return &value; }
    [[nodiscard]] Value value_or(Value replacement) const {
        return present ? value : std::move(replacement);
    }
};

[[nodiscard]] bool add_overflows(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return true;
    }
    result = left + right;
    return false;
}

[[nodiscard]] bool multiply_overflows(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return true;
    }
    result = left * right;
    return false;
}

[[nodiscard]] std::uint64_t ceil_divide(
    const std::uint64_t numerator,
    const std::uint64_t denominator) noexcept {
    return (numerator / denominator) + ((numerator % denominator) == 0U ? 0U : 1U);
}

[[nodiscard]] MeasuredValue<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        return {};
    }
    std::string text = buffer.str();
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
            text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

template <typename Integer>
[[nodiscard]] MeasuredValue<Integer> parse_integer(const std::string_view text) {
    Integer value{};
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        return {};
    }
    return value;
}

template <typename Integer>
[[nodiscard]] MeasuredValue<Integer> read_integer(const std::filesystem::path& path) {
    const auto text = read_text(path);
    if (!text.has_value()) {
        return {};
    }
    return parse_integer<Integer>(*text);
}

[[nodiscard]] bool is_numbered_name(
    const std::string_view name,
    const std::string_view prefix,
    std::uint32_t& number) {
    if (!name.starts_with(prefix) || name.size() == prefix.size()) {
        return false;
    }
    const auto value = parse_integer<std::uint32_t>(name.substr(prefix.size()));
    if (!value.has_value()) {
        return false;
    }
    number = *value;
    return true;
}

[[nodiscard]] std::vector<std::uint32_t> parse_id_list(const std::string_view text) {
    std::vector<std::uint32_t> result;
    std::size_t begin = 0U;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view part = text.substr(begin, end - begin);
        const std::size_t dash = part.find('-');
        const auto first = parse_integer<std::uint32_t>(part.substr(0U, dash));
        if (!first.has_value()) {
            return {};
        }
        std::uint32_t last = *first;
        if (dash != std::string_view::npos) {
            const auto parsed_last = parse_integer<std::uint32_t>(part.substr(dash + 1U));
            if (!parsed_last.has_value() || *parsed_last < *first) {
                return {};
            }
            last = *parsed_last;
        }
        for (std::uint32_t id = *first;; ++id) {
            result.push_back(id);
            if (id == last) {
                break;
            }
            if (id == std::numeric_limits<std::uint32_t>::max()) {
                return {};
            }
        }
        begin = end + (comma == std::string_view::npos ? 0U : 1U);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

[[nodiscard]] MeasuredValue<std::uint64_t> parse_scaled_bytes(
    const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    std::size_t digits = 0U;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        ++digits;
    }
    const auto number = parse_integer<std::uint64_t>(text.substr(0U, digits));
    if (!number.has_value()) {
        return {};
    }
    std::uint64_t scale = 1U;
    if (digits < text.size()) {
        switch (text[digits]) {
            case 'K':
            case 'k':
                scale = UINT64_C(1024);
                break;
            case 'M':
            case 'm':
                scale = UINT64_C(1024) * UINT64_C(1024);
                break;
            case 'G':
            case 'g':
                scale = UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024);
                break;
            default:
                return {};
        }
    }
    std::uint64_t bytes{};
    if (multiply_overflows(*number, scale, bytes)) {
        return {};
    }
    return bytes;
}

[[nodiscard]] MeasuredValue<std::uint64_t> read_proc_memory_value(
    const std::string_view key) {
    std::ifstream stream("/proc/meminfo");
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.starts_with(key)) {
            continue;
        }
        std::istringstream parser(line.substr(key.size()));
        std::uint64_t kibibytes{};
        parser >> kibibytes;
        std::uint64_t bytes{};
        if (!parser || multiply_overflows(kibibytes, UINT64_C(1024), bytes)) {
            return {};
        }
        return bytes;
    }
    return {};
}

[[nodiscard]] MeasuredValue<std::filesystem::path> current_cgroup_v2_path() {
    std::ifstream stream("/proc/self/cgroup");
    std::string line;
    while (std::getline(stream, line)) {
        constexpr std::string_view marker = "0::";
        if (!line.starts_with(marker)) {
            continue;
        }
        std::filesystem::path root("/sys/fs/cgroup");
        const std::string relative = line.substr(marker.size());
        if (!relative.empty() && relative != "/") {
            root /= relative.front() == '/' ? relative.substr(1U) : relative;
        }
        return root;
    }
    return {};
}

[[nodiscard]] MeasuredValue<std::uint64_t> cgroup_memory_available(
    bool& constrained) {
    const auto root = current_cgroup_v2_path();
    if (!root.has_value()) {
        return {};
    }
    const auto maximum_text = read_text(*root / "memory.max");
    const auto current = read_integer<std::uint64_t>(*root / "memory.current");
    if (!maximum_text.has_value() || !current.has_value() || *maximum_text == "max") {
        return {};
    }
    const auto maximum = parse_integer<std::uint64_t>(*maximum_text);
    if (!maximum.has_value()) {
        return {};
    }
    constrained = true;
    return *current >= *maximum ? 0U : *maximum - *current;
}

[[nodiscard]] std::uint32_t cache_kind_from_text(const std::string_view text) {
    if (text == "Data") {
        return LAPLACE_EXECUTION_CACHE_DATA;
    }
    if (text == "Instruction") {
        return LAPLACE_EXECUTION_CACHE_INSTRUCTION;
    }
    if (text == "Unified") {
        return LAPLACE_EXECUTION_CACHE_UNIFIED;
    }
    return 0U;
}

[[nodiscard]] std::uint32_t processor_core_kind(const std::filesystem::path& cpu_path) {
    const auto core_type = read_integer<std::uint32_t>(cpu_path / "topology/core_type");
    if (!core_type.has_value()) {
        return LAPLACE_EXECUTION_CORE_UNKNOWN;
    }
    if (*core_type == 1U) {
        return LAPLACE_EXECUTION_CORE_EFFICIENCY;
    }
    if (*core_type == 2U) {
        return LAPLACE_EXECUTION_CORE_PERFORMANCE;
    }
    return LAPLACE_EXECUTION_CORE_UNKNOWN;
}

[[nodiscard]] std::uint64_t observe_isa_flags() noexcept {
    std::uint64_t result{};
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    result |= __builtin_cpu_supports("sse2") ? LAPLACE_EXECUTION_ISA_X86_SSE2 : 0U;
    result |= __builtin_cpu_supports("avx") ? LAPLACE_EXECUTION_ISA_X86_AVX : 0U;
    result |= __builtin_cpu_supports("avx2") ? LAPLACE_EXECUTION_ISA_X86_AVX2 : 0U;
    result |= __builtin_cpu_supports("avx512f") ? LAPLACE_EXECUTION_ISA_X86_AVX512F : 0U;
    result |= __builtin_cpu_supports("fma") ? LAPLACE_EXECUTION_ISA_X86_FMA : 0U;
    result |= __builtin_cpu_supports("bmi2") ? LAPLACE_EXECUTION_ISA_X86_BMI2 : 0U;
#elif defined(__aarch64__) || defined(__ARM_NEON)
    result |= LAPLACE_EXECUTION_ISA_ARM_NEON;
#endif
    return result;
}

[[nodiscard]] std::uint32_t processor_memory_domain(const std::filesystem::path& cpu_path) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(cpu_path, error)) {
        if (error) {
            break;
        }
        std::uint32_t id{};
        if (is_numbered_name(entry.path().filename().string(), "node", id)) {
            return id;
        }
    }
    return LAPLACE_EXECUTION_UNKNOWN_ID;
}

struct CacheObservation final {
    std::uint64_t size_bytes{};
    std::uint32_t level{};
    std::uint32_t kind{};
    std::uint32_t line_bytes{};
    std::vector<std::uint32_t> processors;
};

[[nodiscard]] std::string cache_key(const CacheObservation& cache) {
    std::ostringstream key;
    key << cache.level << ':' << cache.kind << ':' << cache.size_bytes << ':'
        << cache.line_bytes << ':';
    for (const std::uint32_t processor : cache.processors) {
        key << processor << ',';
    }
    return key.str();
}

[[nodiscard]] MeasuredValue<std::pair<std::uint64_t, std::uint64_t>> read_node_memory(
    const std::filesystem::path& path) {
    std::ifstream stream(path / "meminfo");
    std::string line;
    std::uint64_t total{};
    std::uint64_t free{};
    bool saw_total = false;
    bool saw_free = false;
    while (std::getline(stream, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string label = line.substr(0U, colon);
        std::istringstream parser(line.substr(colon + 1U));
        std::uint64_t kibibytes{};
        parser >> kibibytes;
        std::uint64_t bytes{};
        if (!parser || multiply_overflows(kibibytes, UINT64_C(1024), bytes)) {
            return {};
        }
        if (label.ends_with("MemTotal")) {
            total = bytes;
            saw_total = true;
        } else if (label.ends_with("MemFree")) {
            free = bytes;
            saw_free = true;
        }
    }
    if (!saw_total || !saw_free) {
        return {};
    }
    return std::pair{total, free};
}

class HostTopologyObservationProvider final : public TopologyObservationProvider {
public:
    [[nodiscard]] laplace_execution_status observe(Observation& output) const override {
#if !defined(__linux__)
        static_cast<void>(output);
        return LAPLACE_EXECUTION_PLATFORM_UNSUPPORTED;
#else
        Observation next;
        const std::filesystem::path cpu_root("/sys/devices/system/cpu");
        std::vector<std::uint32_t> cpu_ids;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(cpu_root, error)) {
            if (error) {
                return LAPLACE_EXECUTION_OBSERVATION_FAILED;
            }
            std::uint32_t id{};
            if (entry.is_directory(error) && !error &&
                is_numbered_name(entry.path().filename().string(), "cpu", id)) {
                cpu_ids.push_back(id);
            }
        }
        if (cpu_ids.empty()) {
            return LAPLACE_EXECUTION_OBSERVATION_FAILED;
        }
        std::sort(cpu_ids.begin(), cpu_ids.end());

        const std::uint32_t maximum_cpu = cpu_ids.back();
        if (maximum_cpu >= static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            return LAPLACE_EXECUTION_OVERFLOW;
        }
        const int affinity_count = static_cast<int>(maximum_cpu) + 1;
        cpu_set_t* const affinity = CPU_ALLOC(affinity_count);
        if (affinity == nullptr) {
            return LAPLACE_EXECUTION_OBSERVATION_FAILED;
        }
        const std::size_t affinity_bytes = CPU_ALLOC_SIZE(affinity_count);
        CPU_ZERO_S(affinity_bytes, affinity);
        const bool affinity_known = sched_getaffinity(0, affinity_bytes, affinity) == 0;

        std::size_t online_count = 0U;
        std::size_t allowed_count = 0U;
        bool saw_performance = false;
        bool saw_efficiency = false;
        for (const std::uint32_t id : cpu_ids) {
            const std::filesystem::path cpu_path = cpu_root / ("cpu" + std::to_string(id));
            const auto online_value = read_integer<std::uint32_t>(cpu_path / "online");
            const bool online = !online_value.has_value() || *online_value != 0U;
            const bool allowed = online &&
                (!affinity_known || CPU_ISSET_S(static_cast<int>(id), affinity_bytes, affinity));
            laplace_execution_processor processor{};
            processor.logical_id = id;
            processor.package_id = read_integer<std::uint32_t>(
                cpu_path / "topology/physical_package_id").value_or(LAPLACE_EXECUTION_UNKNOWN_ID);
            processor.core_id = read_integer<std::uint32_t>(
                cpu_path / "topology/core_id").value_or(LAPLACE_EXECUTION_UNKNOWN_ID);
            processor.memory_domain_id = processor_memory_domain(cpu_path);
            const auto frequency_khz = read_integer<std::uint64_t>(
                cpu_path / "cpufreq/cpuinfo_max_freq");
            if (frequency_khz.has_value() &&
                *frequency_khz <= std::numeric_limits<std::uint64_t>::max() / UINT64_C(1000)) {
                processor.maximum_frequency_hz = *frequency_khz * UINT64_C(1000);
            }
            processor.core_kind = processor_core_kind(cpu_path);
            processor.flags = (online ? LAPLACE_EXECUTION_PROCESSOR_ONLINE : 0U) |
                (allowed ? LAPLACE_EXECUTION_PROCESSOR_ALLOWED : 0U);
            online_count += online ? 1U : 0U;
            allowed_count += allowed ? 1U : 0U;
            saw_performance = saw_performance ||
                processor.core_kind == LAPLACE_EXECUTION_CORE_PERFORMANCE;
            saw_efficiency = saw_efficiency ||
                processor.core_kind == LAPLACE_EXECUTION_CORE_EFFICIENCY;
            next.processors.push_back(processor);
        }
        CPU_FREE(affinity);
        if (allowed_count == 0U) {
            return LAPLACE_EXECUTION_OBSERVATION_FAILED;
        }
        if (affinity_known && allowed_count < online_count) {
            next.flags |= LAPLACE_EXECUTION_TOPOLOGY_AFFINITY_CONSTRAINED;
        }
        if (saw_performance && saw_efficiency) {
            next.flags |= LAPLACE_EXECUTION_TOPOLOGY_HYBRID_CORES;
        }

        std::map<std::string, CacheObservation> cache_observations;
        for (const std::uint32_t id : cpu_ids) {
            const std::filesystem::path cache_root =
                cpu_root / ("cpu" + std::to_string(id)) / "cache";
            error.clear();
            for (const auto& entry : std::filesystem::directory_iterator(cache_root, error)) {
                if (error) {
                    break;
                }
                std::uint32_t index{};
                if (!entry.is_directory(error) || error ||
                    !is_numbered_name(entry.path().filename().string(), "index", index)) {
                    continue;
                }
                static_cast<void>(index);
                const auto level = read_integer<std::uint32_t>(entry.path() / "level");
                const auto type = read_text(entry.path() / "type");
                const auto size_text = read_text(entry.path() / "size");
                const auto line = read_integer<std::uint32_t>(
                    entry.path() / "coherency_line_size");
                const auto shared = read_text(entry.path() / "shared_cpu_list");
                if (!level.has_value() || !type.has_value() || !size_text.has_value() ||
                    !line.has_value() || !shared.has_value()) {
                    continue;
                }
                CacheObservation cache{};
                cache.level = *level;
                cache.kind = cache_kind_from_text(*type);
                cache.size_bytes = parse_scaled_bytes(*size_text).value_or(0U);
                cache.line_bytes = *line;
                cache.processors = parse_id_list(*shared);
                if (cache.kind == 0U || cache.size_bytes == 0U || cache.line_bytes == 0U ||
                    cache.processors.empty()) {
                    continue;
                }
                cache_observations.emplace(cache_key(cache), std::move(cache));
            }
        }
        for (const auto& [key, observed] : cache_observations) {
            static_cast<void>(key);
            if (next.cache_processor_ids.size() >
                std::numeric_limits<std::uint32_t>::max()) {
                return LAPLACE_EXECUTION_OVERFLOW;
            }
            laplace_execution_cache cache{};
            cache.size_bytes = observed.size_bytes;
            cache.level = observed.level;
            cache.kind = observed.kind;
            cache.line_bytes = observed.line_bytes;
            cache.processor_id_offset =
                static_cast<std::uint32_t>(next.cache_processor_ids.size());
            if (observed.processors.size() > std::numeric_limits<std::uint32_t>::max()) {
                return LAPLACE_EXECUTION_OVERFLOW;
            }
            cache.processor_id_count = static_cast<std::uint32_t>(observed.processors.size());
            next.cache_processor_ids.insert(
                next.cache_processor_ids.end(),
                observed.processors.begin(),
                observed.processors.end());
            next.caches.push_back(cache);
        }

        const auto total_memory = read_proc_memory_value("MemTotal:");
        const auto usable_memory = read_proc_memory_value("MemAvailable:");
        const long page_bytes = sysconf(_SC_PAGESIZE);
        if (!total_memory.has_value() || !usable_memory.has_value() || page_bytes <= 0) {
            return LAPLACE_EXECUTION_OBSERVATION_FAILED;
        }
        next.total_memory_bytes = *total_memory;
        next.usable_memory_bytes = *usable_memory;
        next.page_bytes = static_cast<std::uint64_t>(page_bytes);
        next.isa_flags = observe_isa_flags();
        bool memory_constrained = false;
        const auto cgroup_available = cgroup_memory_available(memory_constrained);
        if (cgroup_available.has_value() && *cgroup_available < next.usable_memory_bytes) {
            next.usable_memory_bytes = *cgroup_available;
        }
        if (memory_constrained) {
            next.flags |= LAPLACE_EXECUTION_TOPOLOGY_MEMORY_CONSTRAINED;
        }

        const std::filesystem::path node_root("/sys/devices/system/node");
        error.clear();
        for (const auto& entry : std::filesystem::directory_iterator(node_root, error)) {
            if (error) {
                break;
            }
            std::uint32_t id{};
            if (!entry.is_directory(error) || error ||
                !is_numbered_name(entry.path().filename().string(), "node", id)) {
                continue;
            }
            const auto memory = read_node_memory(entry.path());
            if (!memory.has_value()) {
                continue;
            }
            laplace_execution_memory_domain domain{};
            domain.domain_id = id;
            domain.total_bytes = memory->first;
            domain.usable_bytes = memory->second;
            domain.processor_count = static_cast<std::uint32_t>(std::count_if(
                next.processors.begin(), next.processors.end(),
                [id](const laplace_execution_processor& processor) {
                    return processor.memory_domain_id == id;
                }));
            next.memory_domains.push_back(domain);
        }
        std::sort(
            next.memory_domains.begin(), next.memory_domains.end(),
            [](const laplace_execution_memory_domain& left,
               const laplace_execution_memory_domain& right) {
                return left.domain_id < right.domain_id;
            });
        if (next.memory_domains.empty()) {
            laplace_execution_memory_domain domain{};
            domain.domain_id = 0U;
            domain.processor_count = static_cast<std::uint32_t>(next.processors.size());
            domain.total_bytes = next.total_memory_bytes;
            domain.usable_bytes = next.usable_memory_bytes;
            next.memory_domains.push_back(domain);
            for (auto& processor : next.processors) {
                processor.memory_domain_id = 0U;
            }
        } else {
            std::uint64_t remaining = next.usable_memory_bytes;
            for (auto& domain : next.memory_domains) {
                domain.usable_bytes = std::min(domain.usable_bytes, remaining);
                remaining -= domain.usable_bytes;
            }
        }
        output = std::move(next);
        return LAPLACE_EXECUTION_OK;
#endif
    }
};

[[nodiscard]] laplace_execution_status observe_host(Observation& observation) {
    const HostTopologyObservationProvider provider;
    return provider.observe(observation);
}

[[nodiscard]] bool contains_processor(
    const laplace_execution_topology& topology,
    const std::uint32_t logical_id) {
    return std::any_of(
        topology.processors,
        topology.processors + topology.processor_count,
        [logical_id](const laplace_execution_processor& processor) {
            return processor.logical_id == logical_id;
        });
}

[[nodiscard]] bool contains_domain(
    const laplace_execution_topology& topology,
    const std::uint32_t domain_id) {
    return std::any_of(
        topology.memory_domains,
        topology.memory_domains + topology.memory_domain_count,
        [domain_id](const laplace_execution_memory_domain& domain) {
            return domain.domain_id == domain_id;
        });
}

[[nodiscard]] laplace_execution_status validate_topology(
    const laplace_execution_topology& topology) {
    if (topology.reserved != 0U ||
        (topology.flags & ~known_topology_flags) != 0U ||
        (topology.isa_flags & ~known_isa_flags) != 0U ||
        topology.page_bytes == 0U ||
        topology.processor_count == 0U ||
        topology.processor_count > topology.processor_capacity ||
        topology.cache_count > topology.cache_capacity ||
        topology.cache_processor_id_count > topology.cache_processor_id_capacity ||
        topology.memory_domain_count == 0U ||
        topology.memory_domain_count > topology.memory_domain_capacity ||
        topology.processors == nullptr || topology.memory_domains == nullptr ||
        (topology.cache_count != 0U && topology.caches == nullptr) ||
        (topology.cache_processor_id_count != 0U && topology.cache_processor_ids == nullptr) ||
        topology.usable_memory_bytes > topology.total_memory_bytes) {
        return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
    }

    std::size_t allowed_processors = 0U;
    for (std::uint32_t index = 0U; index < topology.processor_count; ++index) {
        const auto& processor = topology.processors[index];
        if ((processor.flags & ~known_processor_flags) != 0U ||
            processor.core_kind > LAPLACE_EXECUTION_CORE_EFFICIENCY ||
            ((processor.flags & LAPLACE_EXECUTION_PROCESSOR_ALLOWED) != 0U &&
             (processor.flags & LAPLACE_EXECUTION_PROCESSOR_ONLINE) == 0U)) {
            return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
        }
        for (std::uint32_t prior = 0U; prior < index; ++prior) {
            if (topology.processors[prior].logical_id == processor.logical_id) {
                return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
            }
        }
        if (processor.memory_domain_id != LAPLACE_EXECUTION_UNKNOWN_ID &&
            !contains_domain(topology, processor.memory_domain_id)) {
            return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
        }
        allowed_processors +=
            (processor.flags & LAPLACE_EXECUTION_PROCESSOR_ALLOWED) != 0U ? 1U : 0U;
    }
    if (allowed_processors == 0U) {
        return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
    }

    std::uint64_t domain_usable_total{};
    for (std::uint32_t index = 0U; index < topology.memory_domain_count; ++index) {
        const auto& domain = topology.memory_domains[index];
        if (domain.usable_bytes > domain.total_bytes) {
            return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
        }
        for (std::uint32_t prior = 0U; prior < index; ++prior) {
            if (topology.memory_domains[prior].domain_id == domain.domain_id) {
                return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
            }
        }
        const auto observed_processor_count = static_cast<std::uint32_t>(std::count_if(
            topology.processors,
            topology.processors + topology.processor_count,
            [&domain](const laplace_execution_processor& processor) {
                return processor.memory_domain_id == domain.domain_id;
            }));
        if (observed_processor_count != domain.processor_count ||
            add_overflows(domain_usable_total, domain.usable_bytes, domain_usable_total)) {
            return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
        }
    }
    if (domain_usable_total > topology.usable_memory_bytes) {
        return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
    }

    for (std::uint32_t index = 0U; index < topology.cache_count; ++index) {
        const auto& cache = topology.caches[index];
        const std::uint64_t member_end =
            static_cast<std::uint64_t>(cache.processor_id_offset) + cache.processor_id_count;
        if (cache.reserved != 0U || cache.level == 0U || cache.size_bytes == 0U ||
            cache.line_bytes == 0U || cache.processor_id_count == 0U ||
            cache.kind < LAPLACE_EXECUTION_CACHE_DATA ||
            cache.kind > LAPLACE_EXECUTION_CACHE_UNIFIED ||
            member_end > topology.cache_processor_id_count) {
            return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
        }
        std::uint32_t prior_id{};
        for (std::uint32_t member = 0U; member < cache.processor_id_count; ++member) {
            const std::uint32_t id =
                topology.cache_processor_ids[cache.processor_id_offset + member];
            if (!contains_processor(topology, id) || (member != 0U && id <= prior_id)) {
                return LAPLACE_EXECUTION_TOPOLOGY_INVALID;
            }
            prior_id = id;
        }
    }
    return LAPLACE_EXECUTION_OK;
}

[[nodiscard]] laplace_execution_status publish_observation(
    Observation& observation,
    laplace_execution_topology& topology) {
    if (observation.processors.size() > std::numeric_limits<std::uint32_t>::max() ||
        observation.caches.size() > std::numeric_limits<std::uint32_t>::max() ||
        observation.cache_processor_ids.size() > std::numeric_limits<std::uint32_t>::max() ||
        observation.memory_domains.size() > std::numeric_limits<std::uint32_t>::max()) {
        return LAPLACE_EXECUTION_OVERFLOW;
    }
    const auto processor_count = static_cast<std::uint32_t>(observation.processors.size());
    const auto cache_count = static_cast<std::uint32_t>(observation.caches.size());
    const auto cache_processor_count =
        static_cast<std::uint32_t>(observation.cache_processor_ids.size());
    const auto domain_count = static_cast<std::uint32_t>(observation.memory_domains.size());
    laplace_execution_topology observed{};
    observed.processors = observation.processors.data();
    observed.caches = observation.caches.data();
    observed.cache_processor_ids = observation.cache_processor_ids.data();
    observed.memory_domains = observation.memory_domains.data();
    observed.processor_count = processor_count;
    observed.processor_capacity = processor_count;
    observed.cache_count = cache_count;
    observed.cache_capacity = cache_count;
    observed.cache_processor_id_count = cache_processor_count;
    observed.cache_processor_id_capacity = cache_processor_count;
    observed.memory_domain_count = domain_count;
    observed.memory_domain_capacity = domain_count;
    observed.total_memory_bytes = observation.total_memory_bytes;
    observed.usable_memory_bytes = observation.usable_memory_bytes;
    observed.page_bytes = observation.page_bytes;
    observed.isa_flags = observation.isa_flags;
    observed.flags = observation.flags;
    const laplace_execution_status validation = validate_topology(observed);
    if (validation != LAPLACE_EXECUTION_OK) {
        return validation;
    }
    const bool enough = topology.processor_capacity >= processor_count &&
        topology.cache_capacity >= cache_count &&
        topology.cache_processor_id_capacity >= cache_processor_count &&
        topology.memory_domain_capacity >= domain_count &&
        (processor_count == 0U || topology.processors != nullptr) &&
        (cache_count == 0U || topology.caches != nullptr) &&
        (cache_processor_count == 0U || topology.cache_processor_ids != nullptr) &&
        (domain_count == 0U || topology.memory_domains != nullptr);
    topology.processor_count = processor_count;
    topology.cache_count = cache_count;
    topology.cache_processor_id_count = cache_processor_count;
    topology.memory_domain_count = domain_count;
    if (!enough) {
        return LAPLACE_EXECUTION_CAPACITY_INSUFFICIENT;
    }

    std::copy(observation.processors.begin(), observation.processors.end(), topology.processors);
    std::copy(observation.caches.begin(), observation.caches.end(), topology.caches);
    std::copy(
        observation.cache_processor_ids.begin(),
        observation.cache_processor_ids.end(),
        topology.cache_processor_ids);
    std::copy(
        observation.memory_domains.begin(),
        observation.memory_domains.end(),
        topology.memory_domains);
    topology.total_memory_bytes = observation.total_memory_bytes;
    topology.usable_memory_bytes = observation.usable_memory_bytes;
    topology.page_bytes = observation.page_bytes;
    topology.isa_flags = observation.isa_flags;
    topology.flags = observation.flags;
    topology.reserved = 0U;
    return LAPLACE_EXECUTION_OK;
}

template <typename Minimum, typename Weight, typename Setter>
[[nodiscard]] laplace_execution_status partition_resource(
    const std::uint64_t total,
    const laplace_execution_partition_request* requests,
    const std::size_t count,
    Minimum minimum,
    Weight weight,
    Setter setter) {
    std::uint64_t minimum_sum{};
    std::uint64_t weight_sum{};
    for (std::size_t index = 0U; index < count; ++index) {
        std::uint64_t next{};
        if (add_overflows(minimum_sum, minimum(requests[index]), next)) {
            return LAPLACE_EXECUTION_OVERFLOW;
        }
        minimum_sum = next;
        if (add_overflows(weight_sum, weight(requests[index]), next) ||
            next > std::numeric_limits<std::uint32_t>::max()) {
            return LAPLACE_EXECUTION_OVERFLOW;
        }
        weight_sum = next;
    }
    if (minimum_sum > total) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }
    const std::uint64_t remainder = total - minimum_sum;
    if (remainder != 0U && weight_sum == 0U) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    std::uint64_t assigned{};
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t item_weight = weight(requests[index]);
        std::uint64_t share{};
        if (weight_sum != 0U && item_weight != 0U) {
            const std::uint64_t quotient = remainder / weight_sum;
            const std::uint64_t residual = remainder % weight_sum;
            share = quotient * item_weight + (residual * item_weight) / weight_sum;
        }
        setter(index, minimum(requests[index]) + share);
        assigned += share;
    }
    std::uint64_t unassigned = remainder - assigned;
    for (std::size_t index = 0U; index < count && unassigned != 0U; ++index) {
        if (weight(requests[index]) != 0U) {
            setter(index, setter(index) + 1U);
            --unassigned;
        }
    }
    return LAPLACE_EXECUTION_OK;
}

}  // namespace

}  // namespace laplace::execution

extern "C" laplace_execution_status laplace_execution_topology_measure_host(
    laplace_execution_topology_size* const required) {
    if (required == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    laplace::execution::Observation observation;
    const laplace_execution_status status = laplace::execution::observe_host(observation);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }
    if (observation.processors.size() > std::numeric_limits<std::uint32_t>::max() ||
        observation.caches.size() > std::numeric_limits<std::uint32_t>::max() ||
        observation.cache_processor_ids.size() > std::numeric_limits<std::uint32_t>::max() ||
        observation.memory_domains.size() > std::numeric_limits<std::uint32_t>::max()) {
        return LAPLACE_EXECUTION_OVERFLOW;
    }
    required->processor_count = static_cast<std::uint32_t>(observation.processors.size());
    required->cache_count = static_cast<std::uint32_t>(observation.caches.size());
    required->cache_processor_id_count =
        static_cast<std::uint32_t>(observation.cache_processor_ids.size());
    required->memory_domain_count =
        static_cast<std::uint32_t>(observation.memory_domains.size());
    return LAPLACE_EXECUTION_OK;
}

extern "C" laplace_execution_status laplace_execution_topology_observe_host(
    laplace_execution_topology* const topology) {
    if (topology == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    laplace::execution::Observation observation;
    const laplace_execution_status status = laplace::execution::observe_host(observation);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }
    return laplace::execution::publish_observation(observation, *topology);
}

extern "C" laplace_execution_status laplace_execution_topology_validate(
    const laplace_execution_topology* const topology) {
    if (topology == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    return laplace::execution::validate_topology(*topology);
}

extern "C" laplace_execution_status laplace_execution_root_grant(
    const laplace_execution_topology* const topology,
    const laplace_execution_external_ownership* const external_ownership,
    laplace_execution_grant* const grant) {
    if (topology == nullptr || external_ownership == nullptr || grant == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    const laplace_execution_status status = laplace::execution::validate_topology(*topology);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }
    std::uint64_t allowed_processors{};
    for (std::uint32_t index = 0U; index < topology->processor_count; ++index) {
        allowed_processors +=
            (topology->processors[index].flags & LAPLACE_EXECUTION_PROCESSOR_ALLOWED) != 0U
            ? 1U
            : 0U;
    }
    if (external_ownership->externally_owned_cpu_slots >= allowed_processors ||
        external_ownership->externally_owned_memory_bytes >= topology->usable_memory_bytes) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }
    laplace_execution_grant next{};
    next.cpu_slots = static_cast<std::uint32_t>(
        allowed_processors - external_ownership->externally_owned_cpu_slots);
    next.memory_bytes = topology->usable_memory_bytes -
        external_ownership->externally_owned_memory_bytes;
    next.io_slots = external_ownership->available_io_slots;
    *grant = next;
    return LAPLACE_EXECUTION_OK;
}

extern "C" laplace_execution_status laplace_execution_partition_grant(
    const laplace_execution_grant* const parent,
    const laplace_execution_partition_request* const requests,
    const size_t request_count,
    laplace_execution_grant* const children) {
    if (parent == nullptr || requests == nullptr || children == nullptr ||
        request_count == 0U ||
        request_count > std::numeric_limits<std::uint32_t>::max() / UINT16_MAX) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    std::vector<laplace_execution_grant> next(request_count);
    for (std::size_t index = 0U; index < request_count; ++index) {
        if (requests[index].reserved != 0U) {
            return LAPLACE_EXECUTION_INVALID_ARGUMENT;
        }
    }
#if defined(LAPLACE_TEST_DUPLICATE_PARENT_GRANT)
    for (std::size_t index = 0U; index < request_count; ++index) {
        next[index] = *parent;
    }
    std::copy(next.begin(), next.end(), children);
    return LAPLACE_EXECUTION_OK;
#endif

    auto memory_setter = [&next](const std::size_t index, const auto... value) -> std::uint64_t {
        if constexpr (sizeof...(value) == 0U) {
            return next[index].memory_bytes;
        } else {
            next[index].memory_bytes = static_cast<std::uint64_t>((value, ...));
            return next[index].memory_bytes;
        }
    };
    laplace_execution_status status = laplace::execution::partition_resource(
        parent->memory_bytes,
        requests,
        request_count,
        [](const laplace_execution_partition_request& request) {
            return request.minimum_memory_bytes;
        },
        [](const laplace_execution_partition_request& request) {
            return static_cast<std::uint64_t>(request.memory_weight);
        },
        memory_setter);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }

    auto cpu_setter = [&next](const std::size_t index, const auto... value) -> std::uint64_t {
        if constexpr (sizeof...(value) == 0U) {
            return next[index].cpu_slots;
        } else {
            next[index].cpu_slots = static_cast<std::uint32_t>((value, ...));
            return next[index].cpu_slots;
        }
    };
    status = laplace::execution::partition_resource(
        parent->cpu_slots,
        requests,
        request_count,
        [](const laplace_execution_partition_request& request) {
            return static_cast<std::uint64_t>(request.minimum_cpu_slots);
        },
        [](const laplace_execution_partition_request& request) {
            return static_cast<std::uint64_t>(request.cpu_weight);
        },
        cpu_setter);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }

    auto io_setter = [&next](const std::size_t index, const auto... value) -> std::uint64_t {
        if constexpr (sizeof...(value) == 0U) {
            return next[index].io_slots;
        } else {
            next[index].io_slots = static_cast<std::uint32_t>((value, ...));
            return next[index].io_slots;
        }
    };
    status = laplace::execution::partition_resource(
        parent->io_slots,
        requests,
        request_count,
        [](const laplace_execution_partition_request& request) {
            return static_cast<std::uint64_t>(request.minimum_io_slots);
        },
        [](const laplace_execution_partition_request& request) {
            return static_cast<std::uint64_t>(request.io_weight);
        },
        io_setter);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }
    std::copy(next.begin(), next.end(), children);
    return LAPLACE_EXECUTION_OK;
}

extern "C" laplace_execution_status laplace_execution_plan_work(
    const laplace_execution_grant* const grant,
    const laplace_execution_work_request* const request,
    laplace_execution_work_plan* const plan) {
    if (grant == nullptr || request == nullptr || plan == nullptr ||
        request->reserved != 0U || request->item_count == 0U ||
        request->minimum_chunk_items == 0U || request->outer_worker_limit == 0U ||
        request->inner_threads_per_worker == 0U) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    if (grant->cpu_slots < request->inner_threads_per_worker ||
        grant->memory_bytes < request->resident_memory_bytes ||
        grant->io_slots < request->required_io_slots) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }

    std::uint64_t workers = grant->cpu_slots / request->inner_threads_per_worker;
    workers = std::min(workers, static_cast<std::uint64_t>(request->outer_worker_limit));
    workers = std::min(workers, request->item_count);
    const std::uint64_t working_memory = grant->memory_bytes - request->resident_memory_bytes;
    if (request->memory_bytes_per_item != 0U) {
        const std::uint64_t max_live_items = working_memory / request->memory_bytes_per_item;
        const std::uint64_t workers_by_minimum_chunk =
            max_live_items / request->minimum_chunk_items;
        workers = std::min(workers, workers_by_minimum_chunk);
    }
    if (workers == 0U) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }

    std::uint64_t chunk_items = laplace::execution::ceil_divide(request->item_count, workers);
    if (request->memory_bytes_per_item != 0U) {
        const std::uint64_t max_chunk_items =
            working_memory / request->memory_bytes_per_item / workers;
        chunk_items = std::min(chunk_items, max_chunk_items);
    }
    if (chunk_items < request->minimum_chunk_items) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }
    const std::uint64_t chunk_count =
        laplace::execution::ceil_divide(request->item_count, chunk_items);
    workers = std::min(workers, chunk_count);

    std::uint64_t live_items{};
    std::uint64_t variable_memory{};
    std::uint64_t peak_memory{};
    if (laplace::execution::multiply_overflows(workers, chunk_items, live_items) ||
        laplace::execution::multiply_overflows(
            live_items, request->memory_bytes_per_item, variable_memory) ||
        laplace::execution::add_overflows(
            request->resident_memory_bytes, variable_memory, peak_memory) ||
        peak_memory > grant->memory_bytes ||
        workers > std::numeric_limits<std::uint32_t>::max()) {
        return LAPLACE_EXECUTION_OVERFLOW;
    }
    laplace_execution_work_plan next{};
    next.chunk_items = chunk_items;
    next.chunk_count = chunk_count;
    next.peak_memory_bytes = peak_memory;
    next.outer_workers = static_cast<std::uint32_t>(workers);
    next.inner_threads_per_worker = request->inner_threads_per_worker;
    next.io_slots = request->required_io_slots;
    *plan = next;
    return LAPLACE_EXECUTION_OK;
}
