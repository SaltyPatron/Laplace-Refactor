#include "laplace/perfcache_modules.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void Fill(std::uint8_t* output, std::size_t size, std::uint8_t value) {
    std::memset(output, value, size);
}

laplace_id128 Id(std::uint8_t value) {
    laplace_id128 result{};
    Fill(result.bytes, sizeof(result.bytes), value);
    return result;
}

laplace_digest256 Digest(std::uint8_t value) {
    laplace_digest256 result{};
    Fill(result.bytes, sizeof(result.bytes), value);
    return result;
}

void StoreU32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0u; index < 4u; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

void StoreU64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0u; index < 8u; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

struct SinkState {
    laplace_digest256 artifact{};
};

laplace_framework_status BeginSink(
    void*, const laplace_framework_context*, std::uint32_t,
    std::uint64_t, std::uint64_t) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void*, const laplace_framework_canonical_batch*) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealSink(
    void* opaque, const laplace_digest256*, laplace_digest256* artifact) {
    *artifact = static_cast<SinkState*>(opaque)->artifact;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void*) {}

bool StageReceipt(
    const laplace_framework_context& context,
    const laplace_digest256& source,
    const laplace_digest256& recipe,
    const laplace_digest256& artifact,
    laplace_framework_stream_receipt* receipt) {
    const std::array<std::uint8_t, 4> bytes{{1u, 2u, 3u, 4u}};
    const laplace_framework_canonical_batch batch{
        bytes.data(), bytes.size(), 1u, 0u, LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    laplace_framework_canonical_stream stream{};
    stream.batches = &batch;
    stream.source_fingerprint = source;
    stream.recipe_fingerprint = recipe;
    stream.batch_count = 1u;
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    SinkState state{artifact};
    laplace_framework_sink_v1 sink{
        &state, BeginSink, StageSink, SealSink, AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR, 0u, 0u};
    return laplace_framework_stage_canonical_stream(
               &context, &stream, &sink, 1u, receipt) ==
        LAPLACE_FRAMEWORK_OK;
}

laplace_framework_context Context(const laplace_digest256& expected_epoch) {
    laplace_framework_context context{};
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.epoch_mask =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_IDENTITY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DATABASE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_NUMERIC) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PACKAGE);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY] = Digest(0x10u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY] = Digest(0x30u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE] = Digest(0x40u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE] = expected_epoch;
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC] = Digest(0x50u);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_PACKAGE] = Digest(0x70u);
    context.authority_fingerprint = Digest(0x90u);
    context.resource_grant = {UINT64_C(1) << 20u, 2u, 1u};
    return context;
}

bool Generate(
    const std::filesystem::path& root,
    const laplace_perfcache_module_v1& module,
    std::uint8_t ordinal,
    const laplace_id128& activation,
    const laplace_digest256& expected_epoch,
    const laplace_digest256& next_epoch,
    std::uint64_t value_base,
    std::vector<std::uint8_t>* manifest) {
    const laplace_digest256 source = Digest(
        static_cast<std::uint8_t>(0x20u + ordinal));
    const laplace_digest256 recipe = Digest(
        static_cast<std::uint8_t>(0x40u + ordinal));
    laplace_perfcache_contract contract{};
    contract.module_id = module.module_id;
    contract.key_schema_id = module.key_schema_id;
    contract.value_schema_id = module.value_schema_id;
    contract.activation_epoch_id = activation;
    contract.activation_epoch_fingerprint = next_epoch;
    contract.module_contract_fingerprint = module.module_contract_fingerprint;
    contract.source_fingerprint = source;
    contract.recipe_fingerprint = recipe;
    if (laplace_perfcache_dependency_fingerprint(
            nullptr, 0u, &contract.dependency_fingerprint) !=
        LAPLACE_PERFCACHE_REGISTRY_OK) {
        return false;
    }
    contract.key_bytes = module.key_bytes;
    contract.value_bytes = module.value_bytes;
    contract.access_law = module.access_law;

    std::vector<std::uint8_t> records(36u, 0u);
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        StoreU32(records.data() + static_cast<std::size_t>(index) * 12u,
                 index);
        StoreU64(records.data() + static_cast<std::size_t>(index) * 12u + 4u,
                 value_base + index);
    }
    const laplace_perfcache_spec spec{
        contract, records.data(), 3u, nullptr, 0u};
    std::size_t artifact_size = 0u;
    if (laplace_perfcache_measure(&spec, &artifact_size) !=
        LAPLACE_PERFCACHE_OK) {
        return false;
    }
    std::vector<std::uint8_t> artifact_bytes(artifact_size, 0u);
    std::size_t written = 0u;
    if (laplace_perfcache_write(
            &spec, artifact_bytes.data(), artifact_bytes.size(), &written) !=
            LAPLACE_PERFCACHE_OK || written != artifact_bytes.size()) {
        return false;
    }
    laplace_perfcache_view view{};
    if (laplace_perfcache_validate(
            artifact_bytes.data(), artifact_bytes.size(), &contract, &view) !=
        LAPLACE_PERFCACHE_OK) {
        return false;
    }
    const std::filesystem::path artifact_path =
        root / ("framework-probe-" + std::to_string(ordinal) + ".lpc");
    std::uint64_t invalid_record = UINT64_MAX;
    if (laplace_perfcache_publish_file(
            artifact_path.c_str(), artifact_bytes.data(), artifact_bytes.size(),
            &contract, module.validate_record, module.state,
            &invalid_record) != LAPLACE_PERFCACHE_OK) {
        return false;
    }

    const std::string artifact_path_string = artifact_path.string();
    laplace_perfcache_generation_artifact generation_artifact{};
    generation_artifact.path = artifact_path_string.c_str();
    generation_artifact.contract = contract;
    generation_artifact.expected_artifact_digest = view.artifact_digest;
    generation_artifact.flags =
        LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
    laplace_perfcache_generation_request request{};
    request.artifacts = &generation_artifact;
    request.artifact_count = 1u;
    request.activation_epoch_id = activation;
    request.epoch_fingerprint = next_epoch;
    if (laplace_perfcache_required_module_set_fingerprint(
            &module, 1u, &request.required_module_set_fingerprint) !=
            LAPLACE_PERFCACHE_REGISTRY_OK ||
        laplace_perfcache_generation_artifact_set_fingerprint(
            &request, &request.sink_artifact_set_fingerprint) !=
            LAPLACE_PERFCACHE_REGISTRY_OK) {
        return false;
    }
    const laplace_framework_context context = Context(expected_epoch);
    laplace_framework_stream_receipt staged{};
    if (!StageReceipt(context, source, recipe,
                      request.sink_artifact_set_fingerprint, &staged)) {
        return false;
    }
    const std::array<laplace_digest256, 1> sink_artifacts{{
        request.sink_artifact_set_fingerprint}};
    request.staged_sink_artifact_fingerprints = sink_artifacts.data();
    request.staged_sink_count = sink_artifacts.size();
    request.perfcache_sink_index = 0u;
    request.staged_receipt_id = staged.receipt_id;
    request.stream_fingerprint = staged.stream_fingerprint;
    request.staged_sink_artifacts_fingerprint =
        staged.sink_artifacts_fingerprint;
    std::size_t manifest_size = 0u;
    if (laplace_perfcache_generation_manifest_measure(
            &context, &staged, &request, &manifest_size) !=
        LAPLACE_PERFCACHE_REGISTRY_OK) {
        return false;
    }
    manifest->assign(manifest_size, 0u);
    laplace_digest256 encoded_fingerprint{};
    return laplace_perfcache_generation_manifest_write(
               &context, &staged, &request, manifest->data(), manifest->size(),
               &written, &encoded_fingerprint) ==
            LAPLACE_PERFCACHE_REGISTRY_OK &&
        written == manifest->size();
}

void PrintHex(const char* name, const std::vector<std::uint8_t>& value) {
    std::printf("%s=", name);
    for (const std::uint8_t byte : value) {
        std::printf("%02x", static_cast<unsigned int>(byte));
    }
    std::putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: perfcache_manifest_probe ROOT\n", stderr);
        return 64;
    }
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(argv[1], error);
    if (error || root.empty() || !std::filesystem::create_directories(root, error) ||
        error) {
        if (!error && std::filesystem::is_directory(root)) {
            /* create_directories reports false for an existing directory. */
        } else {
            std::fputs("cannot create perfcache probe root\n", stderr);
            return 65;
        }
    }
    laplace_perfcache_module_v1 module{};
    if (laplace_perfcache_framework_probe_module(&module) !=
        LAPLACE_PERFCACHE_REGISTRY_OK) {
        return 66;
    }
    const std::array<laplace_id128, 4> activations{{
        Id(0x00u), Id(0x22u), Id(0x33u), Id(0x44u)}};
    const std::array<laplace_digest256, 4> epochs{{
        Digest(0x00u), Digest(0xb2u), Digest(0xc3u), Digest(0xd4u)}};
    const std::array<laplace_digest256, 4> expected{{
        Digest(0xa1u), epochs[0], epochs[1], epochs[2]}};
    const std::array<std::uint64_t, 4> bases{{100u, 200u, 300u, 400u}};
    for (std::size_t index = 0u; index < activations.size(); ++index) {
        std::vector<std::uint8_t> manifest;
        if (!Generate(root, module, static_cast<std::uint8_t>(index + 1u),
                      activations[index], expected[index], epochs[index],
                      bases[index], &manifest)) {
            return 67;
        }
        const std::string name =
            "PERFCACHE_MANIFEST_" + std::to_string(index + 1u);
        PrintHex(name.c_str(), manifest);
    }
    const std::filesystem::path outside_root =
        root.parent_path() / "outside-perfcache-root";
    std::filesystem::create_directories(outside_root, error);
    if (error) {
        return 68;
    }
    std::vector<std::uint8_t> outside_manifest;
    if (!Generate(outside_root, module, 5u, Id(0x55u), epochs[1],
                  Digest(0xe5u), 500u, &outside_manifest)) {
        return 69;
    }
    PrintHex("PERFCACHE_MANIFEST_5", outside_manifest);
    std::vector<std::uint8_t> missing_manifest;
    if (!Generate(root, module, 6u, Id(0x66u), epochs[1],
                  Digest(0xf6u), 600u, &missing_manifest)) {
        return 70;
    }
    if (!std::filesystem::remove(root / "framework-probe-6.lpc", error) ||
        error) {
        return 71;
    }
    PrintHex("PERFCACHE_MANIFEST_6", missing_manifest);
    std::vector<std::uint8_t> competing_manifest;
    if (!Generate(root, module, 7u, Id(0x77u), epochs[0],
                  Digest(0xe7u), 700u, &competing_manifest)) {
        return 72;
    }
    PrintHex("PERFCACHE_MANIFEST_7", competing_manifest);
    return 0;
}
