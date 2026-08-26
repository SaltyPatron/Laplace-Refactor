#include "laplace/perfcache_registry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "blake3.h"

namespace {

constexpr std::uint8_t ArtifactSetDomain[] =
    "laplace-perfcache-artifact-set-v1";
constexpr std::uint8_t ManifestDomain[] =
    "laplace-perfcache-generation-manifest-v1";
constexpr std::uint8_t DependencyDomain[] =
    "laplace-perfcache-module-dependencies-v1";
constexpr std::uint8_t RequiredModuleSetDomain[] =
    "laplace-perfcache-required-module-set-v1";
constexpr std::uint8_t LoadedObjectsDomain[] =
    "laplace-perfcache-loaded-objects-v1";
constexpr std::uint8_t ReceiptDomain[] =
    "laplace-perfcache-generation-receipt-v1";

bool BytesEqual(const std::uint8_t* left,
                const std::uint8_t* right,
                std::size_t count) {
    return std::memcmp(left, right, count) == 0;
}

bool IdEqual(const laplace_id128& left, const laplace_id128& right) {
    return BytesEqual(left.bytes, right.bytes, sizeof(left.bytes));
}

bool DigestEqual(const laplace_digest256& left,
                 const laplace_digest256& right) {
    return BytesEqual(left.bytes, right.bytes, sizeof(left.bytes));
}

bool IdLess(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

void HashU32(blake3_hasher* hasher, std::uint32_t value) {
    std::uint8_t bytes[4]{};
    for (std::size_t index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

void HashU64(blake3_hasher* hasher, std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

void Finish(blake3_hasher* hasher, laplace_digest256* output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

struct RegisteredModule {
    laplace_perfcache_module_v2 value{};
};

struct LoadedArtifact {
    RegisteredModule* module{};
    std::string path;
    laplace_perfcache_contract contract{};
    laplace_digest256 expected_artifact_digest{};
    std::vector<laplace_perfcache_generation_dependency> dependencies;
    std::uint32_t flags{};
    laplace_perfcache_artifact_handle handle{};
    bool opened{};
};

struct LoadedGeneration {
    laplace_perfcache_registry* registry{};
    laplace_perfcache_artifact_provider_v1 provider{};
    std::vector<LoadedArtifact> artifacts;
    laplace_digest256 context_fingerprint{};
    std::uint32_t context_flags{};
    laplace_id128 activation_epoch_id{};
    laplace_digest256 epoch_fingerprint{};
    laplace_digest256 staged_receipt_id{};
    laplace_digest256 stream_fingerprint{};
    laplace_digest256 staged_sink_artifacts_fingerprint{};
    laplace_digest256 sink_artifact_set_fingerprint{};
    laplace_digest256 required_module_set_fingerprint{};
    laplace_digest256 manifest_fingerprint{};
    laplace_digest256 artifact_set_fingerprint{};
    laplace_digest256 loaded_objects_fingerprint{};
    std::uint64_t mapped_bytes{};
    std::uint64_t prefaulted_bytes{};
    std::uint64_t prefaulted_pages{};
    std::uint64_t readers{};
    std::uint64_t staged_sink_count{};
    std::uint64_t perfcache_sink_index{};
    bool retired{};
    LoadedGeneration* retired_next{};
};

void CloseGeneration(LoadedGeneration* generation) {
    if (generation == nullptr) {
        return;
    }
    for (auto iterator = generation->artifacts.rbegin();
         iterator != generation->artifacts.rend(); ++iterator) {
        if (iterator->opened) {
            generation->provider.close(
                generation->provider.state, &iterator->handle);
            iterator->opened = false;
        }
    }
    delete generation;
}

bool ModuleValid(const laplace_perfcache_module_v2& module) {
    if (module.validate_record == nullptr || module.validate_view == nullptr ||
        module.abi_major != LAPLACE_PERFCACHE_MODULE_ABI_MAJOR ||
        module.abi_minor > LAPLACE_PERFCACHE_MODULE_ABI_MINOR ||
        (module.flags & ~LAPLACE_PERFCACHE_MODULE_REQUIRED) != 0u ||
        module.key_bytes == 0u || module.value_bytes == 0u ||
        module.key_bytes > UINT32_MAX - module.value_bytes ||
        module.reserved != 0u) {
        return false;
    }
    if (module.access_law == LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED) {
        return module.lookup_batch != nullptr;
    }
    return module.access_law == LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED ||
        module.access_law == LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED;
}

bool ProviderValid(const laplace_perfcache_artifact_provider_v1& provider) {
    return provider.open != nullptr && provider.prefault != nullptr &&
        provider.close != nullptr &&
        provider.abi_major == LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <= LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MINOR &&
        provider.flags == 0u && provider.reserved == 0u;
}

void HashContract(blake3_hasher* hasher,
                  const laplace_perfcache_contract& contract) {
    blake3_hasher_update(hasher, contract.module_id.bytes,
                         sizeof(contract.module_id.bytes));
    blake3_hasher_update(hasher, contract.key_schema_id.bytes,
                         sizeof(contract.key_schema_id.bytes));
    blake3_hasher_update(hasher, contract.value_schema_id.bytes,
                         sizeof(contract.value_schema_id.bytes));
    blake3_hasher_update(hasher, contract.activation_epoch_id.bytes,
                         sizeof(contract.activation_epoch_id.bytes));
    blake3_hasher_update(hasher, contract.activation_epoch_fingerprint.bytes,
                         sizeof(contract.activation_epoch_fingerprint.bytes));
    blake3_hasher_update(hasher, contract.module_contract_fingerprint.bytes,
                         sizeof(contract.module_contract_fingerprint.bytes));
    blake3_hasher_update(hasher, contract.source_fingerprint.bytes,
                         sizeof(contract.source_fingerprint.bytes));
    blake3_hasher_update(hasher, contract.recipe_fingerprint.bytes,
                         sizeof(contract.recipe_fingerprint.bytes));
    blake3_hasher_update(hasher, contract.dependency_fingerprint.bytes,
                         sizeof(contract.dependency_fingerprint.bytes));
    HashU32(hasher, contract.key_bytes);
    HashU32(hasher, contract.value_bytes);
    HashU32(hasher, contract.access_law);
    HashU64(hasher, contract.flags);
}

laplace_digest256 DependencyFingerprint(
    const laplace_perfcache_generation_dependency* dependencies,
    std::size_t dependency_count) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, DependencyDomain, sizeof(DependencyDomain) - 1u);
    HashU64(&hasher, dependency_count);
    for (std::size_t index = 0; index < dependency_count; ++index) {
        blake3_hasher_update(
            &hasher, dependencies[index].module_id.bytes,
            sizeof(dependencies[index].module_id.bytes));
        blake3_hasher_update(
            &hasher, dependencies[index].artifact_digest.bytes,
            sizeof(dependencies[index].artifact_digest.bytes));
    }
    Finish(&hasher, &result);
    return result;
}

laplace_digest256 RequiredModuleSetFingerprint(
    const std::vector<RegisteredModule>& modules) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    std::uint64_t required_count = 0;
    for (const RegisteredModule& module : modules) {
        if ((module.value.flags & LAPLACE_PERFCACHE_MODULE_REQUIRED) != 0u) {
            required_count += 1u;
        }
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, RequiredModuleSetDomain, sizeof(RequiredModuleSetDomain) - 1u);
    HashU64(&hasher, required_count);
    for (const RegisteredModule& module : modules) {
        if ((module.value.flags & LAPLACE_PERFCACHE_MODULE_REQUIRED) == 0u) {
            continue;
        }
        blake3_hasher_update(&hasher, module.value.module_id.bytes,
                             sizeof(module.value.module_id.bytes));
        blake3_hasher_update(
            &hasher, module.value.module_contract_fingerprint.bytes,
            sizeof(module.value.module_contract_fingerprint.bytes));
    }
    Finish(&hasher, &result);
    return result;
}

laplace_digest256 ArtifactSetFingerprint(const LoadedGeneration& generation) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ArtifactSetDomain, sizeof(ArtifactSetDomain) - 1u);
    blake3_hasher_update(&hasher, generation.activation_epoch_id.bytes,
                         sizeof(generation.activation_epoch_id.bytes));
    blake3_hasher_update(&hasher, generation.epoch_fingerprint.bytes,
                         sizeof(generation.epoch_fingerprint.bytes));
    HashU64(&hasher, generation.artifacts.size());
    for (const LoadedArtifact& artifact : generation.artifacts) {
        HashContract(&hasher, artifact.contract);
        blake3_hasher_update(&hasher, artifact.handle.view.artifact_digest.bytes,
                             sizeof(artifact.handle.view.artifact_digest.bytes));
    }
    Finish(&hasher, &result);
    return result;
}

laplace_digest256 LoadedObjectsFingerprint(const LoadedGeneration& generation) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LoadedObjectsDomain, sizeof(LoadedObjectsDomain) - 1u);
    HashU64(&hasher, generation.artifacts.size());
    for (const LoadedArtifact& artifact : generation.artifacts) {
        blake3_hasher_update(&hasher, artifact.contract.module_id.bytes,
                             sizeof(artifact.contract.module_id.bytes));
        blake3_hasher_update(&hasher, artifact.handle.loaded_identity.bytes,
                             sizeof(artifact.handle.loaded_identity.bytes));
    }
    Finish(&hasher, &result);
    return result;
}

laplace_digest256 ManifestFingerprint(
    const laplace_perfcache_generation_request& request) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ManifestDomain, sizeof(ManifestDomain) - 1u);
    blake3_hasher_update(&hasher, request.activation_epoch_id.bytes,
                         sizeof(request.activation_epoch_id.bytes));
    blake3_hasher_update(&hasher, request.epoch_fingerprint.bytes,
                         sizeof(request.epoch_fingerprint.bytes));
    blake3_hasher_update(&hasher, request.staged_receipt_id.bytes,
                         sizeof(request.staged_receipt_id.bytes));
    blake3_hasher_update(&hasher, request.stream_fingerprint.bytes,
                         sizeof(request.stream_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, request.staged_sink_artifacts_fingerprint.bytes,
        sizeof(request.staged_sink_artifacts_fingerprint.bytes));
    blake3_hasher_update(&hasher, request.sink_artifact_set_fingerprint.bytes,
                         sizeof(request.sink_artifact_set_fingerprint.bytes));
    HashU64(&hasher, static_cast<std::uint64_t>(request.staged_sink_count));
    HashU64(&hasher, static_cast<std::uint64_t>(request.perfcache_sink_index));
    for (std::size_t index = 0u; index < request.staged_sink_count; ++index) {
        blake3_hasher_update(
            &hasher, request.staged_sink_artifact_fingerprints[index].bytes,
            sizeof(request.staged_sink_artifact_fingerprints[index].bytes));
    }
    blake3_hasher_update(&hasher, request.required_module_set_fingerprint.bytes,
                         sizeof(request.required_module_set_fingerprint.bytes));
    HashU64(&hasher, request.artifact_count);
    for (std::size_t index = 0; index < request.artifact_count; ++index) {
        const auto& artifact = request.artifacts[index];
        const std::size_t path_bytes = std::strlen(artifact.path);
        HashContract(&hasher, artifact.contract);
        blake3_hasher_update(&hasher, artifact.expected_artifact_digest.bytes,
                             sizeof(artifact.expected_artifact_digest.bytes));
        HashU64(&hasher, path_bytes);
        blake3_hasher_update(&hasher, artifact.path, path_bytes);
        HashU64(&hasher, artifact.dependency_count);
        for (std::size_t dependency_index = 0;
             dependency_index < artifact.dependency_count;
             ++dependency_index) {
            blake3_hasher_update(
                &hasher,
                artifact.dependencies[dependency_index].module_id.bytes,
                sizeof(artifact.dependencies[dependency_index].module_id.bytes));
            blake3_hasher_update(
                &hasher,
                artifact.dependencies[dependency_index].artifact_digest.bytes,
                sizeof(artifact.dependencies[dependency_index].artifact_digest.bytes));
        }
        HashU32(&hasher, artifact.flags);
    }
    Finish(&hasher, &result);
    return result;
}

void FillReceipt(const LoadedGeneration& generation,
                 std::uint32_t disposition,
                 laplace_perfcache_registry_status status,
                 laplace_perfcache_generation_receipt* receipt) {
    blake3_hasher hasher{};
    std::memset(receipt, 0, sizeof(*receipt));
    receipt->activation_epoch_id = generation.activation_epoch_id;
    receipt->context_fingerprint = generation.context_fingerprint;
    receipt->epoch_fingerprint = generation.epoch_fingerprint;
    receipt->provider_fingerprint = generation.provider.provider_fingerprint;
    receipt->manifest_fingerprint = generation.manifest_fingerprint;
    receipt->artifact_set_fingerprint = generation.artifact_set_fingerprint;
    receipt->loaded_objects_fingerprint = generation.loaded_objects_fingerprint;
    receipt->staged_receipt_id = generation.staged_receipt_id;
    receipt->stream_fingerprint = generation.stream_fingerprint;
    receipt->staged_sink_artifacts_fingerprint =
        generation.staged_sink_artifacts_fingerprint;
    receipt->sink_artifact_set_fingerprint =
        generation.sink_artifact_set_fingerprint;
    receipt->required_module_set_fingerprint =
        generation.required_module_set_fingerprint;
    receipt->artifact_count = generation.artifacts.size();
    receipt->mapped_bytes = generation.mapped_bytes;
    receipt->prefaulted_bytes = generation.prefaulted_bytes;
    receipt->prefaulted_pages = generation.prefaulted_pages;
    receipt->active_reader_count = generation.readers;
    receipt->staged_sink_count = generation.staged_sink_count;
    receipt->perfcache_sink_index = generation.perfcache_sink_index;
    receipt->disposition = disposition;
    receipt->status = static_cast<std::uint32_t>(status);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1u);
    blake3_hasher_update(&hasher, receipt->activation_epoch_id.bytes,
                         sizeof(receipt->activation_epoch_id.bytes));
    blake3_hasher_update(&hasher, receipt->context_fingerprint.bytes,
                         sizeof(receipt->context_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->epoch_fingerprint.bytes,
                         sizeof(receipt->epoch_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->provider_fingerprint.bytes,
                         sizeof(receipt->provider_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->manifest_fingerprint.bytes,
                         sizeof(receipt->manifest_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->artifact_set_fingerprint.bytes,
                         sizeof(receipt->artifact_set_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->loaded_objects_fingerprint.bytes,
                         sizeof(receipt->loaded_objects_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->staged_receipt_id.bytes,
                         sizeof(receipt->staged_receipt_id.bytes));
    blake3_hasher_update(&hasher, receipt->stream_fingerprint.bytes,
                         sizeof(receipt->stream_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->staged_sink_artifacts_fingerprint.bytes,
        sizeof(receipt->staged_sink_artifacts_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->sink_artifact_set_fingerprint.bytes,
                         sizeof(receipt->sink_artifact_set_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->required_module_set_fingerprint.bytes,
                         sizeof(receipt->required_module_set_fingerprint.bytes));
    HashU64(&hasher, receipt->artifact_count);
    HashU64(&hasher, receipt->mapped_bytes);
    HashU64(&hasher, receipt->prefaulted_bytes);
    HashU64(&hasher, receipt->prefaulted_pages);
    HashU64(&hasher, receipt->active_reader_count);
    HashU64(&hasher, receipt->staged_sink_count);
    HashU64(&hasher, receipt->perfcache_sink_index);
    HashU32(&hasher, disposition);
    HashU32(&hasher, receipt->status);
    Finish(&hasher, &receipt->receipt_id);
}

}  // namespace

struct laplace_perfcache_activation_ticket;

struct laplace_perfcache_registry {
    std::mutex mutex;
    std::vector<RegisteredModule> modules;
    LoadedGeneration* active{};
    LoadedGeneration* retired{};
    laplace_perfcache_activation_ticket* reservation{};
    std::uint64_t prepared_count{};
};

struct laplace_perfcache_prepared_generation {
    laplace_perfcache_registry* registry{};
    LoadedGeneration* generation{};
};

struct laplace_perfcache_activation_ticket {
    laplace_perfcache_registry* registry{};
    LoadedGeneration* generation{};
};

struct laplace_perfcache_activation {
    laplace_perfcache_registry* registry{};
    laplace_perfcache_prepared_generation* prepared{};
    laplace_perfcache_activation_ticket* ticket{};
    laplace_perfcache_epoch expected_epoch{};
    laplace_perfcache_generation_receipt receipt{};
    std::uint32_t has_expected_epoch{};
};

struct laplace_perfcache_generation_manifest {
    laplace_framework_context context{};
    laplace_framework_stream_receipt staged_receipt{};
    laplace_perfcache_generation_request request{};
    std::vector<laplace_perfcache_generation_artifact> artifacts;
    std::vector<std::string> paths;
    std::vector<std::vector<laplace_perfcache_generation_dependency>>
        dependencies;
    std::vector<laplace_digest256> staged_sink_artifact_fingerprints;
};

namespace {

RegisteredModule* FindModule(laplace_perfcache_registry* registry,
                             const laplace_id128& module_id) {
    const auto found = std::lower_bound(
        registry->modules.begin(), registry->modules.end(), module_id,
        [](const RegisteredModule& left, const laplace_id128& right) {
            return IdLess(left.value.module_id, right);
        });
    return found != registry->modules.end() &&
            IdEqual(found->value.module_id, module_id)
        ? &*found
        : nullptr;
}

const LoadedArtifact* FindArtifact(const LoadedGeneration& generation,
                                   const laplace_id128& module_id) {
    const auto found = std::lower_bound(
        generation.artifacts.begin(), generation.artifacts.end(), module_id,
        [](const LoadedArtifact& left, const laplace_id128& right) {
            return IdLess(left.contract.module_id, right);
        });
    return found != generation.artifacts.end() &&
            IdEqual(found->contract.module_id, module_id)
        ? &*found
        : nullptr;
}

LoadedGeneration* FindGeneration(laplace_perfcache_registry* registry,
                                 const laplace_perfcache_epoch& epoch) {
    if (registry->active != nullptr &&
        IdEqual(registry->active->activation_epoch_id,
                epoch.activation_epoch_id) &&
        DigestEqual(registry->active->epoch_fingerprint,
                    epoch.epoch_fingerprint)) {
        return registry->active;
    }
    for (LoadedGeneration* generation = registry->retired;
         generation != nullptr; generation = generation->retired_next) {
        if (IdEqual(generation->activation_epoch_id,
                    epoch.activation_epoch_id) &&
            DigestEqual(generation->epoch_fingerprint,
                        epoch.epoch_fingerprint)) {
            return generation;
        }
    }
    return nullptr;
}

bool ModuleContractMatches(const RegisteredModule& module,
                           const laplace_perfcache_contract& contract) {
    return IdEqual(module.value.module_id, contract.module_id) &&
        IdEqual(module.value.key_schema_id, contract.key_schema_id) &&
        IdEqual(module.value.value_schema_id, contract.value_schema_id) &&
        DigestEqual(module.value.module_contract_fingerprint,
                    contract.module_contract_fingerprint) &&
        module.value.access_law == contract.access_law &&
        module.value.key_bytes == contract.key_bytes &&
        module.value.value_bytes == contract.value_bytes;
}

bool ContractEqual(const laplace_perfcache_contract& left,
                   const laplace_perfcache_contract& right) {
    return IdEqual(left.module_id, right.module_id) &&
        IdEqual(left.key_schema_id, right.key_schema_id) &&
        IdEqual(left.value_schema_id, right.value_schema_id) &&
        IdEqual(left.activation_epoch_id, right.activation_epoch_id) &&
        DigestEqual(left.activation_epoch_fingerprint,
                    right.activation_epoch_fingerprint) &&
        DigestEqual(left.module_contract_fingerprint,
                    right.module_contract_fingerprint) &&
        DigestEqual(left.source_fingerprint, right.source_fingerprint) &&
        DigestEqual(left.recipe_fingerprint, right.recipe_fingerprint) &&
        DigestEqual(left.dependency_fingerprint,
                    right.dependency_fingerprint) &&
        left.key_bytes == right.key_bytes &&
        left.value_bytes == right.value_bytes &&
        left.access_law == right.access_law && left.flags == right.flags;
}

bool DependenciesValid(const LoadedGeneration& generation) {
    std::vector<std::uint64_t> remaining_dependencies(
        generation.artifacts.size(), 0u);
    for (std::size_t index = 0; index < generation.artifacts.size(); ++index) {
        const LoadedArtifact& artifact = generation.artifacts[index];
        remaining_dependencies[index] = artifact.dependencies.size();
        for (std::size_t dependency_index = 0;
             dependency_index < artifact.dependencies.size(); ++dependency_index) {
            const auto& dependency = artifact.dependencies[dependency_index];
            if ((dependency_index != 0u &&
                 !IdLess(artifact.dependencies[dependency_index - 1u].module_id,
                         dependency.module_id)) ||
                IdEqual(artifact.contract.module_id, dependency.module_id)) {
                return false;
            }
            const LoadedArtifact* target =
                FindArtifact(generation, dependency.module_id);
            if (target == nullptr ||
                !DigestEqual(target->expected_artifact_digest,
                             dependency.artifact_digest)) {
                return false;
            }
        }
        const laplace_digest256 dependency_fingerprint = DependencyFingerprint(
            artifact.dependencies.data(), artifact.dependencies.size());
        if (!DigestEqual(dependency_fingerprint,
                         artifact.contract.dependency_fingerprint)) {
            return false;
        }
    }
#if defined(LAPLACE_TEST_SKIP_PERFCACHE_DEPENDENCY_CYCLE_VALIDATION)
    return true;
#endif
    std::vector<std::uint8_t> removed(generation.artifacts.size(), 0u);
    std::size_t removed_count = 0;
    bool progress = true;
    while (progress) {
        progress = false;
        for (std::size_t index = 0; index < generation.artifacts.size(); ++index) {
            if (removed[index] != 0u || remaining_dependencies[index] != 0u) {
                continue;
            }
            removed[index] = 1u;
            removed_count += 1u;
            progress = true;
            const laplace_id128 completed =
                generation.artifacts[index].contract.module_id;
            for (std::size_t dependent_index = 0;
                 dependent_index < generation.artifacts.size();
                 ++dependent_index) {
                if (removed[dependent_index] != 0u) {
                    continue;
                }
                const auto& dependencies =
                    generation.artifacts[dependent_index].dependencies;
                if (std::any_of(
                        dependencies.begin(), dependencies.end(),
                        [&completed](const auto& dependency) {
                            return IdEqual(dependency.module_id, completed);
                        })) {
                    remaining_dependencies[dependent_index] -= 1u;
                }
            }
        }
    }
    return removed_count == generation.artifacts.size();
}

bool StagedReceiptMatches(
    const laplace_framework_context& context,
    const laplace_framework_stream_receipt& staged_receipt,
    const laplace_perfcache_generation_request& request) {
    laplace_digest256 context_fingerprint{};
#if defined(LAPLACE_TEST_SKIP_PERFCACHE_SINK_AGGREGATE_VALIDATION)
    const bool sink_evidence_matches = true;
#else
    laplace_digest256 expected_sink_fingerprint{};
    const bool sink_evidence_matches =
        laplace_framework_sink_artifacts_fingerprint(
            request.staged_sink_artifact_fingerprints,
            request.staged_sink_count,
            &expected_sink_fingerprint) == LAPLACE_FRAMEWORK_OK &&
        DigestEqual(
            request.staged_sink_artifact_fingerprints[
                request.perfcache_sink_index],
            request.sink_artifact_set_fingerprint) &&
        DigestEqual(staged_receipt.sink_artifacts_fingerprint,
                    expected_sink_fingerprint);
#endif
    return laplace_framework_stream_receipt_validate(
               &context, &staged_receipt) == LAPLACE_FRAMEWORK_OK &&
        laplace_framework_context_fingerprint(
               &context, &context_fingerprint) == LAPLACE_FRAMEWORK_OK &&
        staged_receipt.status == LAPLACE_FRAMEWORK_OK &&
        staged_receipt.effect_disposition == LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT &&
        staged_receipt.failed_batch_index == LAPLACE_FRAMEWORK_NO_INDEX &&
        staged_receipt.failed_sink_index == LAPLACE_FRAMEWORK_NO_INDEX &&
        staged_receipt.sink_count == request.staged_sink_count &&
        DigestEqual(staged_receipt.context_fingerprint, context_fingerprint) &&
        DigestEqual(staged_receipt.receipt_id, request.staged_receipt_id) &&
        DigestEqual(staged_receipt.stream_fingerprint,
                    request.stream_fingerprint) &&
        DigestEqual(staged_receipt.sink_artifacts_fingerprint,
                    request.staged_sink_artifacts_fingerprint) &&
        sink_evidence_matches;
}

bool ArtifactRequestShapeValid(
    const laplace_perfcache_generation_request& request) {
    if (request.artifacts == nullptr || request.artifact_count == 0u ||
        request.flags != 0u || request.reserved != 0u) {
        return false;
    }
    for (std::size_t index = 0; index < request.artifact_count; ++index) {
        const auto& artifact = request.artifacts[index];
        if (artifact.path == nullptr || artifact.path[0] == '\0' ||
            (artifact.flags &
             ~LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED) != 0u ||
            artifact.reserved != 0u ||
            (artifact.dependency_count != 0u &&
             artifact.dependencies == nullptr) ||
            (index != 0u &&
             !IdLess(request.artifacts[index - 1u].contract.module_id,
                     artifact.contract.module_id))) {
            return false;
        }
        for (std::size_t dependency = 1u;
             dependency < artifact.dependency_count; ++dependency) {
            if (!IdLess(artifact.dependencies[dependency - 1u].module_id,
                        artifact.dependencies[dependency].module_id)) {
                return false;
            }
        }
    }
    return true;
}

bool RequestShapeValid(const laplace_perfcache_generation_request& request) {
    return ArtifactRequestShapeValid(request) &&
        request.staged_sink_artifact_fingerprints != nullptr &&
        request.staged_sink_count != 0u &&
        request.perfcache_sink_index < request.staged_sink_count;
}

constexpr std::uint8_t EncodedManifestMagic[8] = {
    'L', 'P', 'C', 'M', 'N', 'F', '0', '2'};
constexpr std::uint32_t EncodedManifestVersion = 2u;
constexpr std::uint8_t EncodedManifestDomain[] =
    "laplace-perfcache-encoded-generation-manifest-v2";

void AppendBytes(std::vector<std::uint8_t>* output,
                 const std::uint8_t* bytes,
                 std::size_t count) {
    output->insert(output->end(), bytes, bytes + count);
}

void AppendU16(std::vector<std::uint8_t>* output, std::uint16_t value) {
    output->push_back(static_cast<std::uint8_t>(value));
    output->push_back(static_cast<std::uint8_t>(value >> 8u));
}

void AppendU32(std::vector<std::uint8_t>* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4u; ++index) {
        output->push_back(
            static_cast<std::uint8_t>(value >> (index * 8u)));
    }
}

void AppendU64(std::vector<std::uint8_t>* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8u; ++index) {
        output->push_back(
            static_cast<std::uint8_t>(value >> (index * 8u)));
    }
}

void AppendDigest(std::vector<std::uint8_t>* output,
                  const laplace_digest256& value) {
    AppendBytes(output, value.bytes, sizeof(value.bytes));
}

void AppendId(std::vector<std::uint8_t>* output,
              const laplace_id128& value) {
    AppendBytes(output, value.bytes, sizeof(value.bytes));
}

void AppendContract(std::vector<std::uint8_t>* output,
                    const laplace_perfcache_contract& contract) {
    AppendId(output, contract.module_id);
    AppendId(output, contract.key_schema_id);
    AppendId(output, contract.value_schema_id);
    AppendId(output, contract.activation_epoch_id);
    AppendDigest(output, contract.activation_epoch_fingerprint);
    AppendDigest(output, contract.module_contract_fingerprint);
    AppendDigest(output, contract.source_fingerprint);
    AppendDigest(output, contract.recipe_fingerprint);
    AppendDigest(output, contract.dependency_fingerprint);
    AppendU32(output, contract.key_bytes);
    AppendU32(output, contract.value_bytes);
    AppendU32(output, contract.access_law);
    AppendU64(output, contract.flags);
}

laplace_digest256 EncodedManifestFingerprint(
    const std::uint8_t* bytes,
    std::size_t count) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, EncodedManifestDomain,
                         sizeof(EncodedManifestDomain) - 1u);
    blake3_hasher_update(&hasher, bytes, count);
    Finish(&hasher, &result);
    return result;
}

bool EncodeManifest(
    const laplace_framework_context& context,
    const laplace_framework_stream_receipt& staged_receipt,
    const laplace_perfcache_generation_request& request,
    std::vector<std::uint8_t>* output,
    laplace_digest256* fingerprint) {
    if (!RequestShapeValid(request) ||
        !StagedReceiptMatches(context, staged_receipt, request) ||
        request.artifact_count >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    output->clear();
    AppendBytes(output, EncodedManifestMagic, sizeof(EncodedManifestMagic));
    AppendU32(output, EncodedManifestVersion);
    AppendU32(output, static_cast<std::uint32_t>(request.artifact_count));
    for (std::size_t index = 0; index < LAPLACE_FRAMEWORK_EPOCH_COUNT;
         ++index) {
        AppendDigest(output, context.epochs[index]);
    }
    AppendDigest(output, context.authority_fingerprint);
    AppendU64(output, context.resource_grant.memory_bytes);
    AppendU32(output, context.resource_grant.cpu_slots);
    AppendU32(output, context.resource_grant.io_slots);
    AppendU64(output, context.epoch_mask);
    AppendU16(output, context.major);
    AppendU16(output, context.minor);
    AppendU32(output, context.flags);
    AppendU32(output, context.reserved);

    AppendDigest(output, staged_receipt.receipt_id);
    AppendDigest(output, staged_receipt.context_fingerprint);
    AppendDigest(output, staged_receipt.source_fingerprint);
    AppendDigest(output, staged_receipt.recipe_fingerprint);
    AppendDigest(output, staged_receipt.stream_fingerprint);
    AppendDigest(output, staged_receipt.sink_artifacts_fingerprint);
    AppendU64(output, staged_receipt.total_records);
    AppendU64(output, staged_receipt.total_bytes);
    AppendU64(output, staged_receipt.batch_count);
    AppendU64(output, staged_receipt.sink_count);
    AppendU64(output, staged_receipt.failed_batch_index);
    AppendU64(output, staged_receipt.failed_sink_index);
    AppendU32(output, staged_receipt.record_type);
    AppendU32(output, staged_receipt.effect_disposition);
    AppendU32(output, static_cast<std::uint32_t>(staged_receipt.status));
    AppendU32(output, staged_receipt.reserved);

    AppendId(output, request.activation_epoch_id);
    AppendDigest(output, request.epoch_fingerprint);
    AppendDigest(output, request.staged_receipt_id);
    AppendDigest(output, request.stream_fingerprint);
    AppendDigest(output, request.staged_sink_artifacts_fingerprint);
    AppendDigest(output, request.sink_artifact_set_fingerprint);
    AppendU64(output, request.staged_sink_count);
    AppendU64(output, request.perfcache_sink_index);
    for (std::size_t index = 0u; index < request.staged_sink_count; ++index) {
        AppendDigest(output, request.staged_sink_artifact_fingerprints[index]);
    }
    AppendDigest(output, request.required_module_set_fingerprint);
    AppendU32(output, request.flags);
    AppendU32(output, request.reserved);
    for (std::size_t index = 0; index < request.artifact_count; ++index) {
        const auto& artifact = request.artifacts[index];
        const std::size_t path_bytes = std::strlen(artifact.path);
        if (path_bytes == 0u || path_bytes >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()) ||
            artifact.dependency_count >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        AppendU32(output, static_cast<std::uint32_t>(path_bytes));
        AppendBytes(output,
                    reinterpret_cast<const std::uint8_t*>(artifact.path),
                    path_bytes);
        AppendContract(output, artifact.contract);
        AppendDigest(output, artifact.expected_artifact_digest);
        AppendU32(output,
                  static_cast<std::uint32_t>(artifact.dependency_count));
        AppendU32(output, artifact.flags);
        AppendU32(output, artifact.reserved);
        for (std::size_t dependency = 0u;
             dependency < artifact.dependency_count; ++dependency) {
            AppendId(output, artifact.dependencies[dependency].module_id);
            AppendDigest(output,
                         artifact.dependencies[dependency].artifact_digest);
        }
    }
    *fingerprint = EncodedManifestFingerprint(output->data(), output->size());
    AppendDigest(output, *fingerprint);
    return true;
}

class ManifestReader {
 public:
    ManifestReader(const std::uint8_t* bytes, std::size_t count)
        : current_(bytes), remaining_(count) {}

    bool Bytes(std::uint8_t* output, std::size_t count) {
        if (count > remaining_) {
            return false;
        }
        std::memcpy(output, current_, count);
        current_ += count;
        remaining_ -= count;
        return true;
    }

    bool SkipBytes(const std::uint8_t** output, std::size_t count) {
        if (count > remaining_) {
            return false;
        }
        *output = current_;
        current_ += count;
        remaining_ -= count;
        return true;
    }

    bool U16(std::uint16_t* output) {
        std::uint8_t bytes[2]{};
        if (!Bytes(bytes, sizeof(bytes))) {
            return false;
        }
        *output = static_cast<std::uint16_t>(bytes[0]) |
            static_cast<std::uint16_t>(bytes[1] << 8u);
        return true;
    }

    bool U32(std::uint32_t* output) {
        std::uint8_t bytes[4]{};
        if (!Bytes(bytes, sizeof(bytes))) {
            return false;
        }
        *output = 0u;
        for (std::size_t index = 0; index < sizeof(bytes); ++index) {
            *output |= static_cast<std::uint32_t>(bytes[index]) <<
                (index * 8u);
        }
        return true;
    }

    bool U64(std::uint64_t* output) {
        std::uint8_t bytes[8]{};
        if (!Bytes(bytes, sizeof(bytes))) {
            return false;
        }
        *output = 0u;
        for (std::size_t index = 0; index < sizeof(bytes); ++index) {
            *output |= static_cast<std::uint64_t>(bytes[index]) <<
                (index * 8u);
        }
        return true;
    }

    std::size_t remaining() const { return remaining_; }

 private:
    const std::uint8_t* current_;
    std::size_t remaining_;
};

bool ReadDigest(ManifestReader* reader, laplace_digest256* value) {
    return reader->Bytes(value->bytes, sizeof(value->bytes));
}

bool ReadId(ManifestReader* reader, laplace_id128* value) {
    return reader->Bytes(value->bytes, sizeof(value->bytes));
}

bool ReadContract(ManifestReader* reader,
                  laplace_perfcache_contract* contract) {
    return ReadId(reader, &contract->module_id) &&
        ReadId(reader, &contract->key_schema_id) &&
        ReadId(reader, &contract->value_schema_id) &&
        ReadId(reader, &contract->activation_epoch_id) &&
        ReadDigest(reader, &contract->activation_epoch_fingerprint) &&
        ReadDigest(reader, &contract->module_contract_fingerprint) &&
        ReadDigest(reader, &contract->source_fingerprint) &&
        ReadDigest(reader, &contract->recipe_fingerprint) &&
        ReadDigest(reader, &contract->dependency_fingerprint) &&
        reader->U32(&contract->key_bytes) &&
        reader->U32(&contract->value_bytes) &&
        reader->U32(&contract->access_law) &&
        reader->U64(&contract->flags);
}

bool DecodeManifest(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    laplace_perfcache_generation_manifest* manifest,
    laplace_digest256* fingerprint) {
    if (bytes == nullptr || manifest == nullptr || fingerprint == nullptr ||
        byte_count < sizeof(EncodedManifestMagic) + 8u +
            sizeof(fingerprint->bytes)) {
        return false;
    }
    const std::size_t payload_bytes = byte_count - sizeof(fingerprint->bytes);
    *fingerprint = EncodedManifestFingerprint(bytes, payload_bytes);
#if !defined(LAPLACE_TEST_SKIP_ENCODED_MANIFEST_DIGEST)
    if (!BytesEqual(fingerprint->bytes, bytes + payload_bytes,
                    sizeof(fingerprint->bytes))) {
        return false;
    }
#endif
    ManifestReader reader(bytes, payload_bytes);
    const std::uint8_t* magic = nullptr;
    std::uint32_t version = 0u;
    std::uint32_t artifact_count = 0u;
    if (!reader.SkipBytes(&magic, sizeof(EncodedManifestMagic)) ||
        !BytesEqual(magic, EncodedManifestMagic,
                    sizeof(EncodedManifestMagic)) ||
        !reader.U32(&version) || version != EncodedManifestVersion ||
        !reader.U32(&artifact_count) || artifact_count == 0u ||
        static_cast<std::size_t>(artifact_count) > payload_bytes / 64u) {
        return false;
    }
    for (std::size_t index = 0; index < LAPLACE_FRAMEWORK_EPOCH_COUNT;
         ++index) {
        if (!ReadDigest(&reader, &manifest->context.epochs[index])) {
            return false;
        }
    }
    if (!ReadDigest(&reader, &manifest->context.authority_fingerprint) ||
        !reader.U64(&manifest->context.resource_grant.memory_bytes) ||
        !reader.U32(&manifest->context.resource_grant.cpu_slots) ||
        !reader.U32(&manifest->context.resource_grant.io_slots) ||
        !reader.U64(&manifest->context.epoch_mask) ||
        !reader.U16(&manifest->context.major) ||
        !reader.U16(&manifest->context.minor) ||
        !reader.U32(&manifest->context.flags) ||
        !reader.U32(&manifest->context.reserved)) {
        return false;
    }
    std::uint32_t staged_status = 0u;
    if (!ReadDigest(&reader, &manifest->staged_receipt.receipt_id) ||
        !ReadDigest(&reader,
                    &manifest->staged_receipt.context_fingerprint) ||
        !ReadDigest(&reader, &manifest->staged_receipt.source_fingerprint) ||
        !ReadDigest(&reader, &manifest->staged_receipt.recipe_fingerprint) ||
        !ReadDigest(&reader, &manifest->staged_receipt.stream_fingerprint) ||
        !ReadDigest(&reader,
                    &manifest->staged_receipt.sink_artifacts_fingerprint) ||
        !reader.U64(&manifest->staged_receipt.total_records) ||
        !reader.U64(&manifest->staged_receipt.total_bytes) ||
        !reader.U64(&manifest->staged_receipt.batch_count) ||
        !reader.U64(&manifest->staged_receipt.sink_count) ||
        !reader.U64(&manifest->staged_receipt.failed_batch_index) ||
        !reader.U64(&manifest->staged_receipt.failed_sink_index) ||
        !reader.U32(&manifest->staged_receipt.record_type) ||
        !reader.U32(&manifest->staged_receipt.effect_disposition) ||
        !reader.U32(&staged_status) ||
        staged_status >
            static_cast<std::uint32_t>(LAPLACE_FRAMEWORK_REPLAY_MISMATCH) ||
        !reader.U32(&manifest->staged_receipt.reserved)) {
        return false;
    }
    manifest->staged_receipt.status =
        static_cast<laplace_framework_status>(staged_status);
    std::uint64_t staged_sink_count = 0u;
    std::uint64_t perfcache_sink_index = 0u;
    if (!ReadId(&reader, &manifest->request.activation_epoch_id) ||
        !ReadDigest(&reader, &manifest->request.epoch_fingerprint) ||
        !ReadDigest(&reader, &manifest->request.staged_receipt_id) ||
        !ReadDigest(&reader, &manifest->request.stream_fingerprint) ||
        !ReadDigest(
            &reader,
            &manifest->request.staged_sink_artifacts_fingerprint) ||
        !ReadDigest(&reader,
                    &manifest->request.sink_artifact_set_fingerprint) ||
        !reader.U64(&staged_sink_count) ||
        !reader.U64(&perfcache_sink_index) || staged_sink_count == 0u ||
        perfcache_sink_index >= staged_sink_count ||
        staged_sink_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        perfcache_sink_index >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        staged_sink_count > reader.remaining() / sizeof(laplace_digest256)) {
        return false;
    }
    manifest->staged_sink_artifact_fingerprints.resize(
        static_cast<std::size_t>(staged_sink_count));
    for (auto& sink_fingerprint :
         manifest->staged_sink_artifact_fingerprints) {
        if (!ReadDigest(&reader, &sink_fingerprint)) {
            return false;
        }
    }
    if (!ReadDigest(&reader,
                    &manifest->request.required_module_set_fingerprint) ||
        !reader.U32(&manifest->request.flags) ||
        !reader.U32(&manifest->request.reserved)) {
        return false;
    }
    manifest->artifacts.resize(artifact_count);
    manifest->paths.resize(artifact_count);
    manifest->dependencies.resize(artifact_count);
    for (std::size_t index = 0u; index < artifact_count; ++index) {
        auto& artifact = manifest->artifacts[index];
        std::uint32_t path_bytes = 0u;
        std::uint32_t dependency_count = 0u;
        const std::uint8_t* path = nullptr;
        if (!reader.U32(&path_bytes) || path_bytes == 0u ||
            !reader.SkipBytes(&path, path_bytes) ||
            std::memchr(path, '\0', path_bytes) != nullptr ||
            !ReadContract(&reader, &artifact.contract) ||
            !ReadDigest(&reader, &artifact.expected_artifact_digest) ||
            !reader.U32(&dependency_count) ||
            static_cast<std::size_t>(dependency_count) >
                reader.remaining() / 48u ||
            !reader.U32(&artifact.flags) ||
            !reader.U32(&artifact.reserved)) {
            return false;
        }
        manifest->paths[index].assign(
            reinterpret_cast<const char*>(path), path_bytes);
        manifest->dependencies[index].resize(dependency_count);
        for (std::size_t dependency = 0u;
             dependency < dependency_count; ++dependency) {
            if (!ReadId(
                    &reader,
                    &manifest->dependencies[index][dependency].module_id) ||
                !ReadDigest(
                    &reader,
                    &manifest->dependencies[index][dependency]
                         .artifact_digest)) {
                return false;
            }
        }
    }
    if (reader.remaining() != 0u) {
        return false;
    }
    for (std::size_t index = 0u; index < artifact_count; ++index) {
        manifest->artifacts[index].path = manifest->paths[index].c_str();
        manifest->artifacts[index].dependencies =
            manifest->dependencies[index].empty()
            ? nullptr
            : manifest->dependencies[index].data();
        manifest->artifacts[index].dependency_count =
            manifest->dependencies[index].size();
    }
    manifest->request.artifacts = manifest->artifacts.data();
    manifest->request.artifact_count = manifest->artifacts.size();
    manifest->request.staged_sink_artifact_fingerprints =
        manifest->staged_sink_artifact_fingerprints.data();
    manifest->request.staged_sink_count =
        manifest->staged_sink_artifact_fingerprints.size();
    manifest->request.perfcache_sink_index =
        static_cast<std::size_t>(perfcache_sink_index);
    return RequestShapeValid(manifest->request) &&
        StagedReceiptMatches(manifest->context, manifest->staged_receipt,
                             manifest->request);
}

}  // namespace

extern "C" laplace_perfcache_registry_status laplace_perfcache_registry_create(
    const laplace_perfcache_module_v2* modules,
    size_t module_count,
    laplace_perfcache_registry** registry) {
    if (modules == nullptr || module_count == 0u || registry == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *registry = nullptr;
    try {
        auto result = std::make_unique<laplace_perfcache_registry>();
        result->modules.reserve(module_count);
        for (std::size_t index = 0; index < module_count; ++index) {
            if (!ModuleValid(modules[index])) {
                return LAPLACE_PERFCACHE_REGISTRY_MODULE_INVALID;
            }
            result->modules.push_back(RegisteredModule{modules[index]});
        }
        std::sort(result->modules.begin(), result->modules.end(),
                  [](const RegisteredModule& left,
                     const RegisteredModule& right) {
                      return IdLess(left.value.module_id, right.value.module_id);
                  });
        for (std::size_t index = 1; index < result->modules.size(); ++index) {
            if (IdEqual(result->modules[index - 1u].value.module_id,
                        result->modules[index].value.module_id)) {
                return LAPLACE_PERFCACHE_REGISTRY_MODULE_INVALID;
            }
        }
        *registry = result.release();
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    } catch (...) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_dependency_fingerprint(
    const laplace_perfcache_generation_dependency* dependencies,
    size_t dependency_count,
    laplace_digest256* fingerprint) {
    if (fingerprint == nullptr ||
        (dependency_count != 0u && dependencies == nullptr)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    for (std::size_t index = 1; index < dependency_count; ++index) {
        if (!IdLess(dependencies[index - 1u].module_id,
                    dependencies[index].module_id)) {
            return LAPLACE_PERFCACHE_REGISTRY_DEPENDENCY_INVALID;
        }
    }
    *fingerprint = DependencyFingerprint(dependencies, dependency_count);
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_required_module_set_fingerprint(
    const laplace_perfcache_module_v2* modules,
    size_t module_count,
    laplace_digest256* fingerprint) {
    if (modules == nullptr || module_count == 0u || fingerprint == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    try {
        std::vector<RegisteredModule> ordered;
        ordered.reserve(module_count);
        for (std::size_t index = 0; index < module_count; ++index) {
            if (!ModuleValid(modules[index])) {
                return LAPLACE_PERFCACHE_REGISTRY_MODULE_INVALID;
            }
            ordered.push_back(RegisteredModule{modules[index]});
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const RegisteredModule& left,
                     const RegisteredModule& right) {
                      return IdLess(left.value.module_id, right.value.module_id);
                  });
        for (std::size_t index = 1; index < ordered.size(); ++index) {
            if (IdEqual(ordered[index - 1u].value.module_id,
                        ordered[index].value.module_id)) {
                return LAPLACE_PERFCACHE_REGISTRY_MODULE_INVALID;
            }
        }
        *fingerprint = RequiredModuleSetFingerprint(ordered);
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    } catch (...) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_generation_manifest_fingerprint(
    const laplace_perfcache_generation_request* request,
    laplace_digest256* fingerprint) {
    if (request == nullptr || fingerprint == nullptr ||
        !RequestShapeValid(*request)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *fingerprint = ManifestFingerprint(*request);
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_generation_artifact_set_fingerprint(
    const laplace_perfcache_generation_request* request,
    laplace_digest256* fingerprint) {
    if (request == nullptr || fingerprint == nullptr ||
        !ArtifactRequestShapeValid(*request)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ArtifactSetDomain, sizeof(ArtifactSetDomain) - 1u);
    blake3_hasher_update(&hasher, request->activation_epoch_id.bytes,
                         sizeof(request->activation_epoch_id.bytes));
    blake3_hasher_update(&hasher, request->epoch_fingerprint.bytes,
                         sizeof(request->epoch_fingerprint.bytes));
    HashU64(&hasher, request->artifact_count);
    for (std::size_t index = 0; index < request->artifact_count; ++index) {
        const auto& artifact = request->artifacts[index];
        if (index != 0u &&
            !IdLess(request->artifacts[index - 1u].contract.module_id,
                    artifact.contract.module_id)) {
            return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
        }
        HashContract(&hasher, artifact.contract);
        blake3_hasher_update(&hasher, artifact.expected_artifact_digest.bytes,
                             sizeof(artifact.expected_artifact_digest.bytes));
    }
    Finish(&hasher, fingerprint);
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_generation_manifest_measure(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_perfcache_generation_request* request,
    std::size_t* manifest_bytes) {
    if (context == nullptr || staged_receipt == nullptr || request == nullptr ||
        manifest_bytes == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    try {
        std::vector<std::uint8_t> encoded;
        laplace_digest256 fingerprint{};
        if (!EncodeManifest(*context, *staged_receipt, *request, &encoded,
                            &fingerprint)) {
            return LAPLACE_PERFCACHE_REGISTRY_MANIFEST_INVALID;
        }
        *manifest_bytes = encoded.size();
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    } catch (...) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_generation_manifest_write(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_perfcache_generation_request* request,
    std::uint8_t* output,
    std::size_t output_capacity,
    std::size_t* manifest_bytes,
    laplace_digest256* encoded_fingerprint) {
    if (context == nullptr || staged_receipt == nullptr || request == nullptr ||
        manifest_bytes == nullptr || encoded_fingerprint == nullptr ||
        (output_capacity != 0u && output == nullptr)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    try {
        std::vector<std::uint8_t> encoded;
        if (!EncodeManifest(*context, *staged_receipt, *request, &encoded,
                            encoded_fingerprint)) {
            return LAPLACE_PERFCACHE_REGISTRY_MANIFEST_INVALID;
        }
        *manifest_bytes = encoded.size();
        if (output_capacity < encoded.size()) {
            return LAPLACE_PERFCACHE_REGISTRY_BUFFER_TOO_SMALL;
        }
        std::memcpy(output, encoded.data(), encoded.size());
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    } catch (...) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_generation_manifest_open(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    laplace_perfcache_generation_manifest** manifest,
    laplace_digest256* encoded_fingerprint) {
    if (bytes == nullptr || byte_count == 0u || manifest == nullptr ||
        encoded_fingerprint == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *manifest = nullptr;
    try {
        auto result = std::make_unique<laplace_perfcache_generation_manifest>();
        if (!DecodeManifest(bytes, byte_count, result.get(),
                            encoded_fingerprint)) {
            return LAPLACE_PERFCACHE_REGISTRY_MANIFEST_INVALID;
        }
        *manifest = result.release();
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    } catch (...) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_generation_manifest_view(
    const laplace_perfcache_generation_manifest* manifest,
    const laplace_framework_context** context,
    const laplace_framework_stream_receipt** staged_receipt,
    const laplace_perfcache_generation_request** request) {
    if (manifest == nullptr || context == nullptr || staged_receipt == nullptr ||
        request == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *context = &manifest->context;
    *staged_receipt = &manifest->staged_receipt;
    *request = &manifest->request;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" void laplace_perfcache_generation_manifest_close(
    laplace_perfcache_generation_manifest* manifest) {
    delete manifest;
}

extern "C" laplace_perfcache_registry_status laplace_perfcache_registry_destroy(
    laplace_perfcache_registry* registry) {
    if (registry == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    LoadedGeneration* active = nullptr;
    LoadedGeneration* retired = nullptr;
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        bool retired_reader = false;
        for (LoadedGeneration* generation = registry->retired;
             generation != nullptr; generation = generation->retired_next) {
            retired_reader = retired_reader || generation->readers != 0u;
        }
        if (registry->reservation != nullptr || registry->prepared_count != 0u ||
            (registry->active != nullptr && registry->active->readers != 0u) ||
            retired_reader) {
            return LAPLACE_PERFCACHE_REGISTRY_BUSY;
        }
        active = std::exchange(registry->active, nullptr);
        retired = std::exchange(registry->retired, nullptr);
    }
    CloseGeneration(active);
    while (retired != nullptr) {
        LoadedGeneration* generation = retired;
        retired = retired->retired_next;
        CloseGeneration(generation);
    }
    delete registry;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status laplace_perfcache_registry_prepare(
    laplace_perfcache_registry* registry,
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_perfcache_artifact_provider_v1* provider,
    const laplace_perfcache_generation_request* request,
    laplace_perfcache_prepared_generation** prepared,
    laplace_perfcache_generation_receipt* receipt) {
    LoadedGeneration* generation = nullptr;
    if (registry == nullptr || context == nullptr || staged_receipt == nullptr ||
        provider == nullptr || request == nullptr || prepared == nullptr ||
        receipt == nullptr || !RequestShapeValid(*request) ||
        !ProviderValid(*provider) ||
        !StagedReceiptMatches(*context, *staged_receipt, *request)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *prepared = nullptr;
    std::memset(receipt, 0, sizeof(*receipt));
    try {
        generation = new LoadedGeneration{};
        generation->registry = registry;
        generation->provider = *provider;
        if (laplace_framework_context_fingerprint(
                context, &generation->context_fingerprint) !=
            LAPLACE_FRAMEWORK_OK) {
            CloseGeneration(generation);
            return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
        }
        generation->context_flags = context->flags;
        generation->activation_epoch_id = request->activation_epoch_id;
        generation->epoch_fingerprint = request->epoch_fingerprint;
        generation->staged_receipt_id = request->staged_receipt_id;
        generation->stream_fingerprint = request->stream_fingerprint;
        generation->staged_sink_artifacts_fingerprint =
            request->staged_sink_artifacts_fingerprint;
        generation->sink_artifact_set_fingerprint =
            request->sink_artifact_set_fingerprint;
        generation->staged_sink_count =
            static_cast<std::uint64_t>(request->staged_sink_count);
        generation->perfcache_sink_index =
            static_cast<std::uint64_t>(request->perfcache_sink_index);
        generation->required_module_set_fingerprint =
            request->required_module_set_fingerprint;
        generation->manifest_fingerprint = ManifestFingerprint(*request);
        const laplace_digest256 expected_required =
            RequiredModuleSetFingerprint(registry->modules);
        if (!DigestEqual(expected_required,
                         request->required_module_set_fingerprint)) {
            CloseGeneration(generation);
            return LAPLACE_PERFCACHE_REGISTRY_MANIFEST_MISMATCH;
        }
        generation->artifacts.reserve(request->artifact_count);
        for (std::size_t index = 0; index < request->artifact_count; ++index) {
            const laplace_perfcache_generation_artifact& source =
                request->artifacts[index];
            RegisteredModule* module =
                FindModule(registry, source.contract.module_id);
            const bool module_required = module != nullptr &&
                (module->value.flags & LAPLACE_PERFCACHE_MODULE_REQUIRED) != 0u;
            const bool artifact_required =
                (source.flags & LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED) != 0u;
            if (module == nullptr || !ModuleContractMatches(*module, source.contract) ||
                module_required != artifact_required ||
                !IdEqual(source.contract.activation_epoch_id,
                         request->activation_epoch_id) ||
                !DigestEqual(source.contract.activation_epoch_fingerprint,
                             request->epoch_fingerprint) ||
                !DigestEqual(source.contract.source_fingerprint,
                             staged_receipt->source_fingerprint) ||
                !DigestEqual(source.contract.recipe_fingerprint,
                             staged_receipt->recipe_fingerprint)) {
                CloseGeneration(generation);
                return LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH;
            }
            LoadedArtifact artifact{};
            artifact.module = module;
            artifact.path = source.path;
            artifact.contract = source.contract;
            artifact.expected_artifact_digest = source.expected_artifact_digest;
            artifact.flags = source.flags;
            if (source.dependency_count != 0u) {
                artifact.dependencies.assign(
                    source.dependencies,
                    source.dependencies + source.dependency_count);
            }
            generation->artifacts.push_back(std::move(artifact));
        }
        for (const RegisteredModule& module : registry->modules) {
            if ((module.value.flags & LAPLACE_PERFCACHE_MODULE_REQUIRED) != 0u &&
                FindArtifact(*generation, module.value.module_id) == nullptr) {
                CloseGeneration(generation);
                return LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH;
            }
        }
        if (!DependenciesValid(*generation)) {
            CloseGeneration(generation);
            return LAPLACE_PERFCACHE_REGISTRY_DEPENDENCY_INVALID;
        }
        for (std::size_t index = 0; index < generation->artifacts.size(); ++index) {
            LoadedArtifact& artifact = generation->artifacts[index];
            std::uint64_t invalid_record_index = UINT64_MAX;
            const laplace_perfcache_status open_status = provider->open(
                provider->state, artifact.path.c_str(), &artifact.contract,
                &artifact.expected_artifact_digest,
                artifact.module->value.validate_record,
                artifact.module->value.state, &invalid_record_index,
                &artifact.handle);
            artifact.opened = open_status == LAPLACE_PERFCACHE_OK;
            if (open_status != LAPLACE_PERFCACHE_OK ||
                !ContractEqual(artifact.handle.view.contract, artifact.contract) ||
                !DigestEqual(artifact.handle.view.artifact_digest,
                             artifact.expected_artifact_digest)) {
                FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_REJECTED,
                            LAPLACE_PERFCACHE_REGISTRY_ARTIFACT_OPEN_FAILED,
                            receipt);
                CloseGeneration(generation);
                return LAPLACE_PERFCACHE_REGISTRY_ARTIFACT_OPEN_FAILED;
            }
            std::uint64_t invalid_view_record_index = UINT64_MAX;
#if !defined(LAPLACE_TEST_SKIP_PERFCACHE_MODULE_VIEW_VALIDATION)
            if (artifact.module->value.validate_view(
                    artifact.module->value.state, &artifact.handle.view,
                    &invalid_view_record_index) != LAPLACE_PERFCACHE_OK) {
                FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_REJECTED,
                            LAPLACE_PERFCACHE_REGISTRY_ARTIFACT_OPEN_FAILED,
                            receipt);
                CloseGeneration(generation);
                return LAPLACE_PERFCACHE_REGISTRY_ARTIFACT_OPEN_FAILED;
            }
#else
            (void)invalid_view_record_index;
#endif
            if (UINT64_MAX - generation->mapped_bytes <
                    artifact.handle.view.artifact_bytes ||
                generation->mapped_bytes + artifact.handle.view.artifact_bytes >
                    context->resource_grant.memory_bytes) {
                FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_REJECTED,
                            LAPLACE_PERFCACHE_REGISTRY_PREFAULT_FAILED, receipt);
                CloseGeneration(generation);
                return LAPLACE_PERFCACHE_REGISTRY_PREFAULT_FAILED;
            }
            generation->mapped_bytes += artifact.handle.view.artifact_bytes;
            std::uint64_t touched_bytes = 0;
            std::uint64_t touched_pages = 0;
#if defined(LAPLACE_TEST_SKIP_PERFCACHE_PREFAULT)
            const laplace_perfcache_status prefault_status = LAPLACE_PERFCACHE_OK;
            touched_bytes = artifact.handle.view.artifact_bytes;
            touched_pages = 1u;
#else
            const laplace_perfcache_status prefault_status = provider->prefault(
                provider->state, &artifact.handle,
                &context->resource_grant,
                &touched_bytes, &touched_pages);
#endif
            if (prefault_status != LAPLACE_PERFCACHE_OK ||
                touched_bytes != artifact.handle.view.artifact_bytes ||
                touched_pages == 0u ||
                UINT64_MAX - generation->prefaulted_bytes < touched_bytes ||
                UINT64_MAX - generation->prefaulted_pages < touched_pages) {
                FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_REJECTED,
                            LAPLACE_PERFCACHE_REGISTRY_PREFAULT_FAILED,
                            receipt);
                CloseGeneration(generation);
                return LAPLACE_PERFCACHE_REGISTRY_PREFAULT_FAILED;
            }
            generation->prefaulted_bytes += touched_bytes;
            generation->prefaulted_pages += touched_pages;
        }
        generation->artifact_set_fingerprint =
            ArtifactSetFingerprint(*generation);
        generation->loaded_objects_fingerprint =
            LoadedObjectsFingerprint(*generation);
        if (!DigestEqual(generation->artifact_set_fingerprint,
                         request->sink_artifact_set_fingerprint)) {
            FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_REJECTED,
                        LAPLACE_PERFCACHE_REGISTRY_MANIFEST_MISMATCH, receipt);
            CloseGeneration(generation);
            return LAPLACE_PERFCACHE_REGISTRY_MANIFEST_MISMATCH;
        }
        auto* prepared_result = new laplace_perfcache_prepared_generation{};
        prepared_result->registry = registry;
        prepared_result->generation = generation;
        {
            std::lock_guard<std::mutex> lock(registry->mutex);
            registry->prepared_count += 1u;
        }
        *prepared = prepared_result;
        FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_PREPARED,
                    LAPLACE_PERFCACHE_REGISTRY_OK, receipt);
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    } catch (const std::bad_alloc&) {
        CloseGeneration(generation);
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    } catch (...) {
        CloseGeneration(generation);
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
}

static laplace_perfcache_registry_status RegistryReserve(
    laplace_perfcache_registry* registry,
    laplace_perfcache_prepared_generation* prepared,
    std::uint32_t has_expected_epoch,
    const laplace_perfcache_epoch* expected_epoch,
    laplace_perfcache_activation_ticket** ticket,
    laplace_perfcache_generation_receipt* receipt) {
    if (registry == nullptr || prepared == nullptr || ticket == nullptr ||
        receipt == nullptr ||
        prepared->registry != registry || prepared->generation == nullptr ||
        has_expected_epoch > 1u ||
        ((has_expected_epoch != 0u) != (expected_epoch != nullptr))) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *ticket = nullptr;
    if ((prepared->generation->context_flags &
         LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        FillReceipt(*prepared->generation,
                    LAPLACE_PERFCACHE_GENERATION_REJECTED,
                    LAPLACE_PERFCACHE_REGISTRY_NOT_AUTHORIZED, receipt);
        return LAPLACE_PERFCACHE_REGISTRY_NOT_AUTHORIZED;
    }
    auto* reservation = new (std::nothrow) laplace_perfcache_activation_ticket{};
    if (reservation == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    }
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        if (registry->reservation != nullptr) {
            delete reservation;
            FillReceipt(*prepared->generation,
                        LAPLACE_PERFCACHE_GENERATION_REJECTED,
                        LAPLACE_PERFCACHE_REGISTRY_ALREADY_RESERVED, receipt);
            return LAPLACE_PERFCACHE_REGISTRY_ALREADY_RESERVED;
        }
        const bool expected_matches = has_expected_epoch != 0u
            ? registry->active != nullptr &&
                IdEqual(registry->active->activation_epoch_id,
                        expected_epoch->activation_epoch_id) &&
                DigestEqual(registry->active->epoch_fingerprint,
                            expected_epoch->epoch_fingerprint)
            : registry->active == nullptr;
        if (!expected_matches) {
            delete reservation;
            FillReceipt(*prepared->generation,
                        LAPLACE_PERFCACHE_GENERATION_REJECTED,
                        LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH, receipt);
            return LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH;
        }
        reservation->registry = registry;
        reservation->generation = prepared->generation;
        prepared->generation = nullptr;
        if (registry->prepared_count == 0u) {
            delete reservation;
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
        registry->prepared_count -= 1u;
        registry->reservation = reservation;
        FillReceipt(*reservation->generation,
                    LAPLACE_PERFCACHE_GENERATION_RESERVED,
                    LAPLACE_PERFCACHE_REGISTRY_OK, receipt);
    }
    delete prepared;
    *ticket = reservation;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

static laplace_perfcache_registry_status RegistryCommit(
    laplace_perfcache_activation_ticket* ticket,
    laplace_perfcache_generation_receipt* receipt) noexcept {
    if (ticket == nullptr || ticket->registry == nullptr ||
        ticket->generation == nullptr || receipt == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    laplace_perfcache_registry* registry = ticket->registry;
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        if (registry->reservation != ticket) {
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
        LoadedGeneration* previous = registry->active;
        registry->active = ticket->generation;
        registry->reservation = nullptr;
        ticket->generation = nullptr;
        if (previous != nullptr) {
            previous->retired = true;
#if defined(LAPLACE_TEST_CLOSE_PINNED_PERFCACHE_GENERATION)
            CloseGeneration(previous);
#else
            previous->retired_next = registry->retired;
            registry->retired = previous;
#endif
        }
        FillReceipt(*registry->active,
                    LAPLACE_PERFCACHE_GENERATION_ACTIVATED,
                    LAPLACE_PERFCACHE_REGISTRY_OK, receipt);
    }
    delete ticket;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

static laplace_perfcache_registry_status RegistryAbort(
    laplace_perfcache_activation_ticket* ticket,
    laplace_perfcache_generation_receipt* receipt) {
    if (ticket == nullptr || ticket->registry == nullptr ||
        ticket->generation == nullptr || receipt == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    laplace_perfcache_registry* registry = ticket->registry;
    LoadedGeneration* generation = nullptr;
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        if (registry->reservation != ticket) {
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
        registry->reservation = nullptr;
        generation = std::exchange(ticket->generation, nullptr);
        FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_ABORTED,
                    LAPLACE_PERFCACHE_REGISTRY_OK, receipt);
    }
    CloseGeneration(generation);
    delete ticket;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

static laplace_framework_status PrepareFrameworkActivation(
    void* state,
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    laplace_digest256* preparation_fingerprint) {
    auto* activation = static_cast<laplace_perfcache_activation*>(state);
    if (activation == nullptr || activation->prepared == nullptr ||
        activation->ticket != nullptr || context == nullptr ||
        staged_receipt == nullptr || request == nullptr ||
        preparation_fingerprint == nullptr ||
        request->epoch_slot != LAPLACE_FRAMEWORK_EPOCH_PERFCACHE) {
        return LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
    }
    LoadedGeneration* generation = activation->prepared->generation;
    laplace_digest256 context_fingerprint{};
    if (generation == nullptr ||
        laplace_framework_context_fingerprint(
            context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK ||
        !DigestEqual(context_fingerprint, generation->context_fingerprint) ||
        !DigestEqual(staged_receipt->receipt_id,
                     generation->staged_receipt_id) ||
        !DigestEqual(request->next_epoch, generation->epoch_fingerprint) ||
        (activation->has_expected_epoch != 0u &&
         !DigestEqual(request->expected_epoch,
                      activation->expected_epoch.epoch_fingerprint))) {
        return LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
    }
    laplace_perfcache_prepared_generation* prepared = activation->prepared;
    const laplace_perfcache_registry_status status = RegistryReserve(
        activation->registry, prepared, activation->has_expected_epoch,
        activation->has_expected_epoch != 0u
            ? &activation->expected_epoch
            : nullptr,
        &activation->ticket, &activation->receipt);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
    }
    activation->prepared = nullptr;
    *preparation_fingerprint = activation->receipt.receipt_id;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status CommitFrameworkActivation(
    void* state,
    const laplace_framework_activation_request*,
    const laplace_digest256* preparation_fingerprint,
    laplace_digest256* activation_fingerprint) noexcept {
    auto* activation = static_cast<laplace_perfcache_activation*>(state);
    if (activation == nullptr || activation->ticket == nullptr ||
        preparation_fingerprint == nullptr || activation_fingerprint == nullptr ||
        !DigestEqual(*preparation_fingerprint,
                     activation->receipt.receipt_id)) {
        return LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
    }
    laplace_perfcache_activation_ticket* ticket = activation->ticket;
    const laplace_perfcache_registry_status status =
        RegistryCommit(ticket, &activation->receipt);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
    }
    activation->ticket = nullptr;
    *activation_fingerprint = activation->receipt.receipt_id;
    return LAPLACE_FRAMEWORK_OK;
}

static void AbortFrameworkActivation(
    void* state,
    const laplace_framework_activation_request*,
    const laplace_digest256*) {
    auto* activation = static_cast<laplace_perfcache_activation*>(state);
    if (activation == nullptr || activation->ticket == nullptr) {
        return;
    }
    laplace_perfcache_activation_ticket* ticket = activation->ticket;
    if (RegistryAbort(ticket, &activation->receipt) ==
        LAPLACE_PERFCACHE_REGISTRY_OK) {
        activation->ticket = nullptr;
    }
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_activation_create(
    laplace_perfcache_registry* registry,
    laplace_perfcache_prepared_generation** prepared,
    std::uint32_t has_expected_epoch,
    const laplace_perfcache_epoch* expected_epoch,
    laplace_perfcache_activation** activation,
    laplace_framework_activation_provider_v1* provider) {
    if (registry == nullptr || prepared == nullptr || *prepared == nullptr ||
        (*prepared)->registry != registry ||
        (*prepared)->generation == nullptr ||
        has_expected_epoch > 1u ||
        ((has_expected_epoch != 0u) != (expected_epoch != nullptr)) ||
        activation == nullptr || provider == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *activation = nullptr;
    std::memset(provider, 0, sizeof(*provider));
    auto* result = new (std::nothrow) laplace_perfcache_activation{};
    if (result == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED;
    }
    result->registry = registry;
    result->prepared = *prepared;
    result->has_expected_epoch = has_expected_epoch;
    if (expected_epoch != nullptr) {
        result->expected_epoch = *expected_epoch;
    }
    provider->state = result;
    provider->prepare = PrepareFrameworkActivation;
    provider->commit = CommitFrameworkActivation;
    provider->abort = AbortFrameworkActivation;
    provider->abi_major = LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR;
#if !defined(LAPLACE_TEST_RETAIN_PREPARED_OWNERSHIP)
    *prepared = nullptr;
#endif
    *activation = result;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_activation_receipt_get(
    const laplace_perfcache_activation* activation,
    laplace_perfcache_generation_receipt* receipt) {
    if (activation == nullptr || receipt == nullptr ||
        activation->receipt.disposition == 0u) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *receipt = activation->receipt;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_activation_commit_ready(
    const laplace_perfcache_activation* activation) {
    if (activation == nullptr || activation->registry == nullptr ||
        activation->ticket == nullptr || activation->ticket->generation == nullptr ||
        activation->receipt.disposition !=
            LAPLACE_PERFCACHE_GENERATION_RESERVED ||
        activation->receipt.status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(activation->registry->mutex);
    return activation->registry->reservation == activation->ticket
        ? LAPLACE_PERFCACHE_REGISTRY_OK
        : LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
}

extern "C" void laplace_perfcache_activation_destroy(
    laplace_perfcache_activation* activation) {
    if (activation == nullptr) {
        return;
    }
    if (activation->ticket != nullptr) {
        AbortFrameworkActivation(activation, nullptr, nullptr);
    }
    if (activation->prepared != nullptr) {
        laplace_perfcache_registry_discard_prepared(&activation->prepared);
    }
    delete activation;
}

extern "C" void laplace_perfcache_registry_discard_prepared(
    laplace_perfcache_prepared_generation** prepared) {
    if (prepared == nullptr || *prepared == nullptr) {
        return;
    }
    laplace_perfcache_prepared_generation* owned = *prepared;
    *prepared = nullptr;
    if (owned->registry != nullptr && owned->generation != nullptr) {
        std::lock_guard<std::mutex> lock(owned->registry->mutex);
        if (owned->registry->prepared_count != 0u) {
            owned->registry->prepared_count -= 1u;
        }
    }
    CloseGeneration(owned->generation);
    owned->generation = nullptr;
    delete owned;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_registry_materialize_prepared(
    laplace_perfcache_registry* registry,
    laplace_perfcache_prepared_generation** prepared,
    laplace_perfcache_generation_receipt* receipt) {
    if (registry == nullptr || prepared == nullptr || *prepared == nullptr ||
        receipt == nullptr || (*prepared)->registry != registry ||
        (*prepared)->generation == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    laplace_perfcache_prepared_generation* owned = *prepared;
    LoadedGeneration* generation = owned->generation;
    const laplace_perfcache_epoch epoch{
        generation->activation_epoch_id, generation->epoch_fingerprint};
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        if (FindGeneration(registry, epoch) != nullptr) {
            return LAPLACE_PERFCACHE_REGISTRY_BUSY;
        }
        if (registry->prepared_count == 0u) {
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
        registry->prepared_count -= 1u;
#if defined(LAPLACE_TEST_MATERIALIZE_PERFCACHE_AS_ACTIVE)
        generation->retired = false;
        registry->active = generation;
#else
        generation->retired = true;
        generation->retired_next = registry->retired;
        registry->retired = generation;
#endif
        owned->generation = nullptr;
        FillReceipt(*generation, LAPLACE_PERFCACHE_GENERATION_MATERIALIZED,
                    LAPLACE_PERFCACHE_REGISTRY_OK, receipt);
    }
    *prepared = nullptr;
    delete owned;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status laplace_perfcache_registry_pin(
    laplace_perfcache_registry* registry,
    std::uint32_t has_expected_epoch,
    const laplace_perfcache_epoch* expected_epoch,
    laplace_perfcache_pin* pin) {
    if (registry == nullptr || pin == nullptr || pin->registry != nullptr ||
        pin->generation != nullptr || has_expected_epoch > 1u ||
        ((has_expected_epoch != 0u) != (expected_epoch != nullptr))) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(registry->mutex);
    if (registry->active == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_NO_ACTIVE_GENERATION;
    }
    if (has_expected_epoch != 0u &&
        (!IdEqual(registry->active->activation_epoch_id,
                  expected_epoch->activation_epoch_id) ||
         !DigestEqual(registry->active->epoch_fingerprint,
                      expected_epoch->epoch_fingerprint))) {
        return LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH;
    }
    if (registry->active->readers == UINT64_MAX) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
    registry->active->readers += 1u;
    pin->generation = registry->active;
    pin->registry = registry;
    pin->epoch.activation_epoch_id = registry->active->activation_epoch_id;
    pin->epoch.epoch_fingerprint = registry->active->epoch_fingerprint;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_registry_pin_epoch(
    laplace_perfcache_registry* registry,
    const laplace_perfcache_epoch* epoch,
    laplace_perfcache_pin* pin) {
    if (registry == nullptr || epoch == nullptr || pin == nullptr ||
        pin->registry != nullptr || pin->generation != nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(registry->mutex);
    LoadedGeneration* generation = FindGeneration(registry, *epoch);
    if (generation == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH;
    }
    if (generation->readers == UINT64_MAX) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
    generation->readers += 1u;
    pin->generation = generation;
    pin->registry = registry;
    pin->epoch = *epoch;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_pin_lookup_batch(
    const laplace_perfcache_pin* pin,
    const laplace_id128* module_id,
    const std::uint8_t* keys,
    size_t key_count,
    std::uint64_t* record_indexes,
    std::uint8_t* found) {
    if (pin == nullptr || pin->generation == nullptr || module_id == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    const auto* generation =
        static_cast<const LoadedGeneration*>(pin->generation);
    const LoadedArtifact* artifact = FindArtifact(*generation, *module_id);
    if (artifact == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_MODULE_NOT_FOUND;
    }
    const laplace_perfcache_status status =
#if defined(LAPLACE_TEST_OVERRIDE_FIXED_PERFCACHE_ACCESS_LAW)
        artifact->module->value.lookup_batch != nullptr
#else
        artifact->module->value.access_law ==
            LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED
#endif
        ? artifact->module->value.lookup_batch(
              artifact->module->value.state, &artifact->handle.view,
              keys, key_count, record_indexes, found)
        : laplace_perfcache_lookup_batch(
              &artifact->handle.view, keys, key_count, record_indexes, found);
    return status == LAPLACE_PERFCACHE_OK
        ? LAPLACE_PERFCACHE_REGISTRY_OK
        : status == LAPLACE_PERFCACHE_KEY_NOT_FOUND
            ? LAPLACE_PERFCACHE_REGISTRY_MODULE_NOT_FOUND
            : LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
}

extern "C" laplace_perfcache_registry_status laplace_perfcache_pin_view(
    const laplace_perfcache_pin* pin,
    const laplace_id128* module_id,
    const laplace_perfcache_view** view) {
    if (pin == nullptr || pin->generation == nullptr || module_id == nullptr ||
        view == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    const auto* generation =
        static_cast<const LoadedGeneration*>(pin->generation);
    const LoadedArtifact* artifact = FindArtifact(*generation, *module_id);
    if (artifact == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_MODULE_NOT_FOUND;
    }
    *view = &artifact->handle.view;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status laplace_perfcache_pin_release(
    laplace_perfcache_pin* pin) {
    if (pin == nullptr || pin->registry == nullptr || pin->generation == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    auto* generation = static_cast<LoadedGeneration*>(pin->generation);
    laplace_perfcache_registry* registry = pin->registry;
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        if (generation->readers == 0u) {
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
        generation->readers -= 1u;
    }
    std::memset(pin, 0, sizeof(*pin));
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status laplace_perfcache_registry_collect(
    laplace_perfcache_registry* registry,
    std::uint64_t* collected_generation_count,
    std::uint64_t* collected_artifact_count,
    std::uint64_t* released_bytes) {
    if (registry == nullptr || collected_generation_count == nullptr ||
        collected_artifact_count == nullptr || released_bytes == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    *collected_generation_count = 0u;
    *collected_artifact_count = 0u;
    *released_bytes = 0u;
    LoadedGeneration* collected = nullptr;
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        std::uint64_t generation_total = 0u;
        std::uint64_t artifact_total = 0u;
        std::uint64_t byte_total = 0u;
        for (LoadedGeneration* generation = registry->retired;
             generation != nullptr; generation = generation->retired_next) {
            if (generation->readers != 0u) {
                continue;
            }
            const std::uint64_t generation_artifacts =
                static_cast<std::uint64_t>(generation->artifacts.size());
            if (generation_total == UINT64_MAX ||
                generation_artifacts > UINT64_MAX - artifact_total ||
                generation->mapped_bytes > UINT64_MAX - byte_total) {
                return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
            }
            generation_total += 1u;
            artifact_total += generation_artifacts;
            byte_total += generation->mapped_bytes;
        }
        LoadedGeneration** link = &registry->retired;
        while (*link != nullptr) {
            LoadedGeneration* generation = *link;
            if (generation->readers != 0u) {
                link = &generation->retired_next;
                continue;
            }
            *link = generation->retired_next;
            generation->retired_next = collected;
            collected = generation;
            *collected_generation_count += 1u;
            *collected_artifact_count +=
                static_cast<std::uint64_t>(generation->artifacts.size());
            *released_bytes += generation->mapped_bytes;
        }
        if (*collected_generation_count != generation_total ||
            *collected_artifact_count != artifact_total ||
            *released_bytes != byte_total) {
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
    }
    while (collected != nullptr) {
        LoadedGeneration* generation = collected;
        collected = collected->retired_next;
        CloseGeneration(generation);
    }
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

extern "C" laplace_perfcache_registry_status
laplace_perfcache_registry_snapshot_get(
    laplace_perfcache_registry* registry,
    laplace_perfcache_registry_snapshot* snapshot) {
    if (registry == nullptr || snapshot == nullptr) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    std::memset(snapshot, 0, sizeof(*snapshot));
    std::lock_guard<std::mutex> lock(registry->mutex);
    if (registry->active != nullptr) {
        snapshot->active_activation_epoch_id =
            registry->active->activation_epoch_id;
        snapshot->active_epoch_fingerprint =
            registry->active->epoch_fingerprint;
        snapshot->active_artifact_set_fingerprint =
            registry->active->artifact_set_fingerprint;
        snapshot->active_artifact_count = registry->active->artifacts.size();
        snapshot->active_reader_count = registry->active->readers;
        snapshot->has_active_generation = 1u;
    }
    for (const LoadedGeneration* generation = registry->retired;
         generation != nullptr; generation = generation->retired_next) {
        snapshot->retired_generation_count += 1u;
        snapshot->retired_reader_count += generation->readers;
    }
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}
