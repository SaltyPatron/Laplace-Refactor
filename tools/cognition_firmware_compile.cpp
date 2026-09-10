#include "blake3.h"
#include "laplace/cognition_firmware.h"
#include "laplace/cognition_materialization.h"
#include "laplace/identity.h"
#include "laplace/observation_query.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ImageOwner final {
    laplace_cognition_firmware_image* value{};
    ~ImageOwner() { laplace_cognition_firmware_image_destroy(&value); }
};

std::uint32_t Relation(std::string_view name) {
    if (name == "container") return LAPLACE_OBSERVATION_QUERY_CONTAINER;
    if (name == "constituent") return LAPLACE_OBSERVATION_QUERY_CONSTITUENT;
    if (name == "predecessor") return LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    if (name == "successor") return LAPLACE_OBSERVATION_QUERY_SUCCESSOR;
    if (name == "cooccur") return LAPLACE_OBSERVATION_QUERY_COOCCUR;
    if (name == "semantic") return LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    return 0U;
}

laplace_id128 ContentIdentity(std::string_view ascii) {
    std::vector<laplace_id128> atoms(ascii.size());
    for (std::size_t index = 0; index < ascii.size(); ++index) {
        const auto codepoint = static_cast<std::uint32_t>(
            static_cast<unsigned char>(ascii[index]));
        if (laplace_identity_codepoint(codepoint, &atoms[index]) != LAPLACE_IDENTITY_OK) {
            std::cerr << "failed to identify modality atom\n";
            std::exit(2);
        }
    }
    laplace_id128 result{};
    if (laplace_identity_composite(atoms.data(), atoms.size(), &result) !=
        LAPLACE_IDENTITY_OK) {
        std::cerr << "failed to identify modality content\n";
        std::exit(2);
    }
    return result;
}

laplace_digest256 RecipeEpoch() {
    static constexpr char domain[] =
        "laplace-stock-exact-witnessed-realization-v1";
    laplace_digest256 result{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1U);
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

template <typename T>
std::string Hex(const T& value) {
    static constexpr char digits[] = "0123456789abcdef";
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    std::string result(sizeof(T) * 2U, '0');
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

std::string HexBytes(const std::uint8_t* bytes, std::size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(count * 2U, '0');
    for (std::size_t index = 0; index < count; ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--relation") {
        std::cerr << "usage: laplace_cognition_firmware_compile --relation "
                     "container|constituent|predecessor|successor|cooccur|semantic\n";
        return 64;
    }
    const std::string_view relation_name(argv[2]);
    const std::uint32_t relation = Relation(relation_name);
    if (relation == 0U) {
        std::cerr << "unknown relation family: " << relation_name << '\n';
        return 64;
    }

    std::array<laplace_cognition_firmware_step, 2> steps{};
    steps[0].kind = LAPLACE_COGNITION_FIRMWARE_INTERPRET;
    steps[0].anchor.source = LAPLACE_COGNITION_FIRMWARE_OBSERVATION;
    steps[0].relation_mask = relation;

    steps[1].kind = LAPLACE_COGNITION_FIRMWARE_EMIT;
    steps[1].anchor.source = LAPLACE_COGNITION_FIRMWARE_ANSWER;
    steps[1].anchor.step_index = 0U;
    steps[1].realization.modality_id = ContentIdentity("text/plain");
    steps[1].realization.realization_recipe_epoch = RecipeEpoch();
    steps[1].realization.maximum_candidates = 1U;
    steps[1].realization.version = LAPLACE_COGNITION_REALIZATION_VERSION;
    steps[1].output_encoding = LAPLACE_COGNITION_OUTPUT_UTF8;

    const laplace_cognition_firmware_program program{
        steps.data(), static_cast<std::uint32_t>(steps.size()),
        LAPLACE_COGNITION_FIRMWARE_VERSION};
    ImageOwner image;
    const auto status = laplace_cognition_firmware_image_create(
        &program, UINT64_C(1048576), &image.value);
    if (status != LAPLACE_COGNITION_FIRMWARE_OK || image.value == nullptr) {
        std::cerr << "firmware image creation failed: "
                  << static_cast<unsigned>(status) << '\n';
        return 2;
    }

    laplace_cognition_firmware_program view{};
    const std::uint8_t* bytes = nullptr;
    std::size_t byte_count = 0U;
    laplace_digest256 identity{};
    if (laplace_cognition_firmware_image_view(
            image.value, &view, &bytes, &byte_count, &identity) !=
            LAPLACE_COGNITION_FIRMWARE_OK ||
        bytes == nullptr || byte_count == 0U || view.step_count != steps.size()) {
        std::cerr << "firmware image readback failed\n";
        return 2;
    }

    std::cout << "{\"schema\":\"laplace.cognition-firmware-image/v1\","
              << "\"program\":\"one-hop-exact-witnessed-output\","
              << "\"relation\":\"" << relation_name << "\","
              << "\"program_id\":\"" << Hex(identity) << "\","
              << "\"image_bytes\":" << byte_count << ','
              << "\"image_hex\":\"" << HexBytes(bytes, byte_count) << "\"}\n";
    return 0;
}
