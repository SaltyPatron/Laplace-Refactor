#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/statvfs.h>

#include "blake3.h"
#include "laplace/execution.h"

namespace {

struct Options {
    std::string data_path;
    std::string wal_path;
    std::string temporary_path;
    std::uint32_t maximum_cpu_slots{};
    std::uint64_t maximum_memory_bytes{};
    std::uint32_t maximum_io_slots{};
};

struct StorageObservation {
    std::string requested_path;
    std::string backing_path;
    std::uint64_t available_bytes{};
    std::uint64_t fragment_bytes{};
};

class ReceiptHasher final {
public:
    explicit ReceiptHasher(const std::string_view domain) {
        blake3_hasher_init(&hasher_);
        String(domain);
    }

    void U32(const std::uint32_t value) {
        std::array<std::uint8_t, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        blake3_hasher_update(&hasher_, bytes.data(), bytes.size());
    }

    void U64(const std::uint64_t value) {
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        blake3_hasher_update(&hasher_, bytes.data(), bytes.size());
    }

    void String(const std::string_view value) {
        U64(static_cast<std::uint64_t>(value.size()));
        blake3_hasher_update(&hasher_, value.data(), value.size());
    }

    [[nodiscard]] std::string Finish() {
        std::array<std::uint8_t, 32> digest{};
        blake3_hasher_finalize(&hasher_, digest.data(), digest.size());
        constexpr std::array<char, 16> alphabet{
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
        std::string result(digest.size() * 2U, '0');
        for (std::size_t index = 0; index < digest.size(); ++index) {
            result[index * 2U] = alphabet[digest[index] >> 4U];
            result[index * 2U + 1U] = alphabet[digest[index] & 0x0fU];
        }
        return result;
    }

private:
    blake3_hasher hasher_{};
};

[[nodiscard]] std::string Json(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2U);
    result.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20U) {
                    constexpr std::array<char, 16> alphabet{
                        '0', '1', '2', '3', '4', '5', '6', '7',
                        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
                    result += "\\u00";
                    result.push_back(alphabet[character >> 4U]);
                    result.push_back(alphabet[character & 0x0fU]);
                } else {
                    result.push_back(static_cast<char>(character));
                }
        }
    }
    result.push_back('"');
    return result;
}

template <typename Integer>
[[nodiscard]] bool ParseInteger(const std::string_view text, Integer& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}

[[nodiscard]] bool ParseOptions(const int argc, char** const argv, Options& options) {
    if (argc != 13) {
        return false;
    }
    for (int index = 1; index < argc; index += 2) {
        const std::string_view name(argv[index]);
        const std::string_view value(argv[index + 1]);
        if (name == "--data") {
            options.data_path = value;
        } else if (name == "--wal") {
            options.wal_path = value;
        } else if (name == "--temporary") {
            options.temporary_path = value;
        } else if (name == "--maximum-cpu-slots") {
            if (!ParseInteger(value, options.maximum_cpu_slots)) return false;
        } else if (name == "--maximum-memory-bytes") {
            if (!ParseInteger(value, options.maximum_memory_bytes)) return false;
        } else if (name == "--maximum-io-slots") {
            if (!ParseInteger(value, options.maximum_io_slots)) return false;
        } else {
            return false;
        }
    }
    return !options.data_path.empty() && !options.wal_path.empty() &&
        !options.temporary_path.empty() &&
        std::filesystem::path(options.data_path).is_absolute() &&
        std::filesystem::path(options.wal_path).is_absolute() &&
        std::filesystem::path(options.temporary_path).is_absolute();
}

[[nodiscard]] StorageObservation ObserveStorage(const std::string& requested) {
    std::filesystem::path backing(requested);
    std::error_code error;
    while (!std::filesystem::exists(backing, error)) {
        if (error || backing == backing.root_path()) {
            backing = backing.root_path();
            break;
        }
        backing = backing.parent_path();
    }
    struct statvfs status {};
    if (statvfs(backing.c_str(), &status) != 0) {
        throw std::system_error(errno, std::generic_category(), "statvfs");
    }
    const auto available = static_cast<std::uint64_t>(status.f_bavail);
    const auto fragment = static_cast<std::uint64_t>(status.f_frsize);
    if (fragment != 0U && available > std::numeric_limits<std::uint64_t>::max() / fragment) {
        throw std::overflow_error("available storage byte count overflows");
    }
    return {requested, backing.lexically_normal().string(), available * fragment, fragment};
}

void HashTopology(ReceiptHasher& hasher, const laplace_execution_topology& topology) {
    hasher.U32(topology.processor_count);
    hasher.U32(topology.cache_count);
    hasher.U32(topology.cache_processor_id_count);
    hasher.U32(topology.memory_domain_count);
    hasher.U64(topology.total_memory_bytes);
    hasher.U64(topology.usable_memory_bytes);
    hasher.U64(topology.page_bytes);
    hasher.U64(topology.isa_flags);
    hasher.U32(topology.flags);
    for (std::uint32_t index = 0; index < topology.processor_count; ++index) {
        const auto& item = topology.processors[index];
        hasher.U32(item.logical_id);
        hasher.U32(item.package_id);
        hasher.U32(item.core_id);
        hasher.U32(item.memory_domain_id);
        hasher.U64(item.maximum_frequency_hz);
        hasher.U32(item.core_kind);
        hasher.U32(item.flags);
    }
    for (std::uint32_t index = 0; index < topology.cache_count; ++index) {
        const auto& item = topology.caches[index];
        hasher.U64(item.size_bytes);
        hasher.U32(item.level);
        hasher.U32(item.kind);
        hasher.U32(item.line_bytes);
        hasher.U32(item.processor_id_offset);
        hasher.U32(item.processor_id_count);
    }
    for (std::uint32_t index = 0; index < topology.cache_processor_id_count; ++index) {
        hasher.U32(topology.cache_processor_ids[index]);
    }
    for (std::uint32_t index = 0; index < topology.memory_domain_count; ++index) {
        const auto& item = topology.memory_domains[index];
        hasher.U32(item.domain_id);
        hasher.U32(item.processor_count);
        hasher.U64(item.total_bytes);
        hasher.U64(item.usable_bytes);
    }
}

[[nodiscard]] std::string StorageReceipt(
    const std::array<StorageObservation, 3>& storage) {
    ReceiptHasher hasher("laplace.storage-observation/v1");
    for (const auto& item : storage) {
        hasher.String(item.requested_path);
        hasher.String(item.backing_path);
        hasher.U64(item.available_bytes);
        hasher.U64(item.fragment_bytes);
    }
    return hasher.Finish();
}

void EmitStorage(const char* name, const StorageObservation& item, const bool comma) {
    std::cout << "    \"" << name << "\": {\"available_bytes\": "
              << item.available_bytes << ", \"backing_path\": " << Json(item.backing_path)
              << ", \"fragment_bytes\": " << item.fragment_bytes << ", \"path\": "
              << Json(item.requested_path) << "}" << (comma ? "," : "") << "\n";
}

int Run(const Options& options) {
    laplace_execution_topology_size required{};
    if (laplace_execution_topology_measure_host(&required) != LAPLACE_EXECUTION_OK) return 2;
    std::vector<laplace_execution_processor> processors(required.processor_count);
    std::vector<laplace_execution_cache> caches(required.cache_count);
    std::vector<std::uint32_t> cache_processors(required.cache_processor_id_count);
    std::vector<laplace_execution_memory_domain> domains(required.memory_domain_count);
    laplace_execution_topology topology{};
    topology.processors = processors.data();
    topology.processor_capacity = required.processor_count;
    topology.caches = caches.data();
    topology.cache_capacity = required.cache_count;
    topology.cache_processor_ids = cache_processors.data();
    topology.cache_processor_id_capacity = required.cache_processor_id_count;
    topology.memory_domains = domains.data();
    topology.memory_domain_capacity = required.memory_domain_count;
    if (laplace_execution_topology_observe_host(&topology) != LAPLACE_EXECUTION_OK ||
        laplace_execution_topology_validate(&topology) != LAPLACE_EXECUTION_OK) return 3;

    std::vector<std::uint32_t> allowed;
    for (const auto& processor : processors) {
        if ((processor.flags & LAPLACE_EXECUTION_PROCESSOR_ALLOWED) != 0U) {
            allowed.push_back(processor.logical_id);
        }
    }
    std::sort(allowed.begin(), allowed.end());
    const auto target_cpu = static_cast<std::uint32_t>(
        std::min<std::size_t>(allowed.size(), options.maximum_cpu_slots));
    const auto target_memory = std::min(topology.usable_memory_bytes, options.maximum_memory_bytes);
    if (target_cpu == 0U || target_memory == 0U) return 4;
    const laplace_execution_external_ownership external{
        topology.usable_memory_bytes - target_memory,
        static_cast<std::uint32_t>(allowed.size()) - target_cpu,
        options.maximum_io_slots};
    laplace_execution_grant root_grant{};
    if (laplace_execution_root_grant(&topology, &external, &root_grant) != LAPLACE_EXECUTION_OK) {
        return 5;
    }
    const laplace_execution_partition_request request{
        root_grant.memory_bytes,
        1U,
        1U,
        1U,
        static_cast<std::uint16_t>(root_grant.cpu_slots),
        static_cast<std::uint16_t>(root_grant.io_slots),
        0U};
    laplace_execution_grant grant{};
    if (laplace_execution_partition_grant(&root_grant, &request, 1U, &grant) !=
        LAPLACE_EXECUTION_OK) return 6;
    allowed.resize(grant.cpu_slots);

    const std::array<StorageObservation, 3> storage{{
        ObserveStorage(options.data_path),
        ObserveStorage(options.wal_path),
        ObserveStorage(options.temporary_path)}};

    ReceiptHasher topology_hasher("laplace.execution-topology-observation/v1");
    HashTopology(topology_hasher, topology);
    const std::string topology_receipt = topology_hasher.Finish();
    ReceiptHasher root_hasher("laplace.execution-root-grant/v1");
    root_hasher.String(topology_receipt);
    root_hasher.U64(external.externally_owned_memory_bytes);
    root_hasher.U32(external.externally_owned_cpu_slots);
    root_hasher.U32(external.available_io_slots);
    root_hasher.U64(root_grant.memory_bytes);
    root_hasher.U32(root_grant.cpu_slots);
    root_hasher.U32(root_grant.io_slots);
    const std::string root_receipt = root_hasher.Finish();
    ReceiptHasher partition_hasher("laplace.execution-partition-grant/v1");
    partition_hasher.String(root_receipt);
    partition_hasher.U64(request.minimum_memory_bytes);
    partition_hasher.U32(request.memory_weight);
    partition_hasher.U32(request.cpu_weight);
    partition_hasher.U32(request.io_weight);
    partition_hasher.U32(request.minimum_cpu_slots);
    partition_hasher.U32(request.minimum_io_slots);
    partition_hasher.U64(grant.memory_bytes);
    partition_hasher.U32(grant.cpu_slots);
    partition_hasher.U32(grant.io_slots);
    const std::string partition_receipt = partition_hasher.Finish();
    ReceiptHasher processor_hasher("laplace.execution-processor-allocation/v1");
    processor_hasher.String(partition_receipt);
    for (const auto id : allowed) processor_hasher.U32(id);
    const std::string processor_receipt = processor_hasher.Finish();
    const std::string storage_receipt = StorageReceipt(storage);

    std::cout << "{\n"
              << "  \"grant\": {\"cpu_slots\": " << grant.cpu_slots
              << ", \"io_slots\": " << grant.io_slots
              << ", \"memory_bytes\": " << grant.memory_bytes
              << ", \"processor_ids\": [";
    for (std::size_t index = 0; index < allowed.size(); ++index) {
        if (index != 0U) std::cout << ", ";
        std::cout << allowed[index];
    }
    std::cout << "]},\n"
              << "  \"native_authority\": {\n"
              << "    \"allowed_processor_count\": " << allowed.size() + external.externally_owned_cpu_slots << ",\n"
              << "    \"external_ownership\": {\"cpu_slots\": " << external.externally_owned_cpu_slots
              << ", \"memory_bytes\": " << external.externally_owned_memory_bytes << "},\n"
              << "    \"observed_cache_count\": " << topology.cache_count << ",\n"
              << "    \"observed_memory_domain_count\": " << topology.memory_domain_count << ",\n"
              << "    \"observed_processor_count\": " << topology.processor_count << ",\n"
              << "    \"page_bytes\": " << topology.page_bytes << ",\n"
              << "    \"schema\": \"laplace.native-execution-resource-observation/v1\",\n"
              << "    \"total_memory_bytes\": " << topology.total_memory_bytes << ",\n"
              << "    \"usable_memory_bytes\": " << topology.usable_memory_bytes << "\n"
              << "  },\n"
              << "  \"partition_receipt\": " << Json(partition_receipt) << ",\n"
              << "  \"processor_allocation_receipt\": " << Json(processor_receipt) << ",\n"
              << "  \"root_grant_receipt\": " << Json(root_receipt) << ",\n"
              << "  \"schema\": \"laplace.execution-resource-observation-candidate/v1\",\n"
              << "  \"source\": \"laplace_native_execution_authority\",\n"
              << "  \"storage\": {\n";
    EmitStorage("data", storage[0], true);
    EmitStorage("temporary", storage[2], true);
    EmitStorage("wal", storage[1], false);
    std::cout << "  },\n"
              << "  \"storage_observation_receipt\": " << Json(storage_receipt) << ",\n"
              << "  \"topology_receipt\": " << Json(topology_receipt) << "\n"
              << "}\n";
    return 0;
}

}  // namespace

int main(const int argc, char** const argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        std::cerr << "usage: laplace_resource_observe --data PATH --wal PATH "
                     "--temporary PATH --maximum-cpu-slots N "
                     "--maximum-memory-bytes N --maximum-io-slots N\n";
        return 1;
    }
    try {
        return Run(options);
    } catch (const std::exception& error) {
        std::cerr << "laplace_resource_observe: " << error.what() << "\n";
        return 7;
    }
}
