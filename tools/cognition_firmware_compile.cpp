#include "blake3.h"
#include "laplace/cognition_firmware.h"
#include "laplace/cognition_materialization.h"
#include "laplace/identity.h"
#include "laplace/observation_query.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
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

std::vector<std::string> ParseRelations(std::string_view value) {
    std::vector<std::string> relations;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? value.size() : comma;
        const std::string_view item = value.substr(start, end - start);
        if (item.empty() || Relation(item) == 0U) {
            std::cerr << "unknown or empty relation family: " << item << '\n';
            std::exit(64);
        }
        relations.emplace_back(item);
        if (comma == std::string_view::npos) break;
        start = comma + 1U;
    }
    return relations;
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

std::string JsonEscape(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (byte < 0x20U) {
                    result += "\\u00";
                    result.push_back(digits[byte >> 4U]);
                    result.push_back(digits[byte & 0x0fU]);
                } else {
                    result.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    return result;
}

void Usage() {
    std::cerr
        << "usage: laplace_cognition_firmware_compile "
           "(--auto | --relation NAME | --relations NAME[,NAME...]) [--output IMAGE]\n"
        << "relations: container constituent predecessor successor cooccur semantic\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool automatic = false;
    std::vector<std::string> relations;
    std::string output_path;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--auto") {
            if (automatic || !relations.empty()) {
                Usage();
                return 64;
            }
            automatic = true;
        } else if (argument == "--relation" || argument == "--relations") {
            if (automatic || !relations.empty() || index + 1 >= argc) {
                Usage();
                return 64;
            }
            ++index;
            relations = ParseRelations(argv[index]);
        } else if (argument == "--output") {
            if (!output_path.empty() || index + 1 >= argc) {
                Usage();
                return 64;
            }
            ++index;
            output_path = argv[index];
        } else if (argument == "--help") {
            Usage();
            return 0;
        } else {
            Usage();
            return 64;
        }
    }
    if ((!automatic && relations.empty()) ||
        (automatic && !relations.empty()) ||
        relations.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        Usage();
        return 64;
    }

    std::vector<laplace_cognition_firmware_step> steps(
        automatic ? 2U : relations.size() + 1U);
    if (automatic) {
        auto& interpret = steps.front();
        interpret.kind = LAPLACE_COGNITION_FIRMWARE_INTERPRET;
        interpret.anchor.source = LAPLACE_COGNITION_FIRMWARE_OBSERVATION;
        interpret.anchor.step_index = 0U;
        interpret.relation_mask = LAPLACE_OBSERVATION_QUERY_RELATION_MASK;
    } else {
        for (std::size_t index = 0U; index < relations.size(); ++index) {
            auto& step = steps[index];
            step.kind = index == 0U
                ? LAPLACE_COGNITION_FIRMWARE_INTERPRET
                : LAPLACE_COGNITION_FIRMWARE_EXECUTE;
            step.anchor.source = index == 0U
                ? LAPLACE_COGNITION_FIRMWARE_OBSERVATION
                : LAPLACE_COGNITION_FIRMWARE_ANSWER;
            step.anchor.step_index = index == 0U
                ? 0U
                : static_cast<std::uint32_t>(index - 1U);
            step.relation_mask = Relation(relations[index]);
        }
    }

    auto& emit = steps.back();
    emit.kind = LAPLACE_COGNITION_FIRMWARE_EMIT;
    emit.anchor.source = LAPLACE_COGNITION_FIRMWARE_ANSWER;
    emit.anchor.step_index = automatic
        ? 0U
        : static_cast<std::uint32_t>(relations.size() - 1U);
    emit.realization.modality_id = ContentIdentity("text/plain");
    emit.realization.realization_recipe_epoch = RecipeEpoch();
    emit.realization.maximum_candidates = 1U;
    emit.realization.version = LAPLACE_COGNITION_REALIZATION_VERSION;
    emit.output_encoding = LAPLACE_COGNITION_OUTPUT_UTF8;

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

    if (!output_path.empty()) {
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "cannot open firmware output: " << output_path << '\n';
            return 73;
        }
        output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(byte_count));
        if (!output) {
            std::cerr << "cannot write firmware output: " << output_path << '\n';
            return 74;
        }
    }

    std::cout << "{\"schema\":\"laplace.cognition-firmware-image/v1\",";
    if (automatic) {
        std::cout
            << "\"program\":\"adaptive-active-relation-microcycle\","
            << "\"mode\":\"auto\","
            << "\"step_count\":" << steps.size() << ','
            << "\"relation_mask\":" << LAPLACE_OBSERVATION_QUERY_RELATION_MASK << ','
            << "\"eligible_relations\":[\"container\",\"constituent\","
               "\"predecessor\",\"successor\",\"cooccur\",\"semantic\"],";
    } else {
        std::cout
            << "\"program\":\"relation-chain-exact-witnessed-output\","
            << "\"mode\":\"explicit\","
            << "\"step_count\":" << steps.size() << ',';
        if (relations.size() == 1U) {
            std::cout << "\"relation\":\"" << relations.front() << "\",";
        }
        std::cout << "\"relations\":[";
        for (std::size_t index = 0U; index < relations.size(); ++index) {
            if (index != 0U) std::cout << ',';
            std::cout << '"' << relations[index] << '"';
        }
        std::cout << "],";
    }
    std::cout << "\"program_id\":\"" << Hex(identity) << "\","
              << "\"image_bytes\":" << byte_count << ','
              << "\"image_hex\":\"" << HexBytes(bytes, byte_count) << "\"";
    if (!output_path.empty()) {
        std::cout << ",\"output\":\"" << JsonEscape(output_path) << "\"";
    }
    std::cout << "}\n";
    return 0;
}
