#include "laplace/execution.h"

#include <array>
#include <cstdint>

int main() {
    const laplace_execution_grant parent{101U, 7U, 5U};
    const std::array<laplace_execution_partition_request, 2> requests{{
        {1U, 1U, 1U, 1U, 1U, 1U, 0U},
        {1U, 1U, 1U, 1U, 1U, 1U, 0U},
    }};
    std::array<laplace_execution_grant, 2> children{};
    if (laplace_execution_partition_grant(
            &parent, requests.data(), requests.size(), children.data()) !=
        LAPLACE_EXECUTION_OK) {
        return 1;
    }
    std::uint64_t memory{};
    std::uint32_t cpu{};
    std::uint32_t io{};
    for (const auto& child : children) {
        memory += child.memory_bytes;
        cpu += child.cpu_slots;
        io += child.io_slots;
    }
    return memory == parent.memory_bytes && cpu == parent.cpu_slots && io == parent.io_slots
        ? 0
        : 2;
}
