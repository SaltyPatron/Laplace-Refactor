#include <cstdio>
#include <string>

#include "laplace/machine_exception.h"

int main() {
    if (laplace_machine_exception_registry_validate() !=
        LAPLACE_MACHINE_EXCEPTION_OK) {
        std::fputs("native machine-exception registry is invalid\n", stderr);
        return 70;
    }
    const auto count = laplace_machine_exception_descriptor_count();
    const auto* descriptors = laplace_machine_exception_descriptors();
    if (count == 0u || descriptors == nullptr) {
        std::fputs("native machine-exception registry is empty\n", stderr);
        return 71;
    }

    std::string rows;
    for (std::size_t index = 0u; index < count; ++index) {
        const auto& descriptor = descriptors[index];
        char row[192]{};
        const int written = std::snprintf(
            row,
            sizeof(row),
            "%u,%u,%u,%u,%u,%u\n",
            descriptor.condition,
            descriptor.kind,
            descriptor.priority,
            descriptor.capability_flags,
            descriptor.recovery_disposition,
            descriptor.publication_disposition);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(row)) {
            std::fputs("native machine-exception row encoding failed\n", stderr);
            return 72;
        }
        rows.append(row, static_cast<std::size_t>(written));
    }

    std::fputs("MACHINE_EXCEPTION_EXPECTED_HEX=", stdout);
    for (const unsigned char byte : rows) {
        std::printf("%02x", static_cast<unsigned int>(byte));
    }
    std::putchar('\n');
    return 0;
}
