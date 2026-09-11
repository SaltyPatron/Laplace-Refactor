#include "laplace/source/cili_pwn_mappings_20260903_profile.h"
#include "tabular_profile_fixture.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
namespace profile = laplace::generated::cili_pwn_mappings_20260903;
using Fixture = laplace::test::TabularProfileFixture<profile::Profile>;
struct Destroy {
    void operator()(laplace_tabular_source_plan* plan) const {
        laplace_tabular_source_plan_destroy(&plan);
    }
};
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "expected exact source-root argument");
        if (!std::filesystem::is_directory(argv[1])) {
            std::fprintf(stderr, "exact preserved CILI source root is not mounted\n");
            return 77;
        }
        Fixture fixture;
        Require(fixture.Load(argv[1]), fixture.error.c_str());
        laplace_tabular_source_plan* raw = nullptr;
        Require(laplace_tabular_source_plan_create(&fixture.input, &raw) ==
                    LAPLACE_TABULAR_SOURCE_OK, "current recipe did not compile");
        std::unique_ptr<laplace_tabular_source_plan, Destroy> plan(raw);
        laplace_tabular_source_plan_view view{};
        Require(laplace_tabular_source_plan_view_get(plan.get(), &view) ==
                    LAPLACE_TABULAR_SOURCE_OK, "plan view unavailable");
        Require(view.profile.byte_count == profile::expected_bytes &&
                    view.profile.record_count == profile::expected_records &&
                    view.profile.field_count == profile::expected_fields &&
                    view.claim_count == profile::expected_claims &&
                    view.mapping_occurrence_count == profile::expected_mappings &&
                    view.reference_occurrence_count == profile::expected_references,
                "current release denominators do not close");
        for (std::size_t index = 1; index < fixture.storage.size(); ++index) {
            std::vector<std::uint8_t> bytes(fixture.storage[index].size());
            std::size_t written = 0;
            Require(laplace_tabular_source_recompose_artifact(
                        plan.get(), index, bytes.data(), bytes.size(), &written) ==
                        LAPLACE_TABULAR_SOURCE_OK && written == bytes.size() &&
                        bytes == fixture.storage[index],
                    "selected mapping member did not reconstruct exactly");
        }
        plan.reset();
        // A one-byte edit to the source must fail before producing a plan.
        fixture.storage[1][0] ^= 1u;
        raw = nullptr;
        const auto status = laplace_tabular_source_plan_create(&fixture.input, &raw);
        std::unique_ptr<laplace_tabular_source_plan, Destroy> mutant(raw);
        Require(status == LAPLACE_TABULAR_SOURCE_DIGEST_MISMATCH && raw == nullptr,
                "altered source accepted by current recipe");
        std::printf("current CILI: 235246 mappings; 470492 references; "
                    "both members reconstructed; changed byte rejected\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
