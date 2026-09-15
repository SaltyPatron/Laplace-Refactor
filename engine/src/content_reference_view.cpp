#include "laplace/content_reference_view.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "blake3.h"

namespace {
using Status = laplace_content_reference_status;
using Key = std::array<std::uint8_t, LAPLACE_IDENTITY_BYTES>;
constexpr std::string_view Recipe{
    "laplace-content-reference-view-v1;canonical-content-closure;"
    "all-source-forms-authenticated;ordered-id-runs;singleton-transparent;"
    "pinned-canonical-atoms;ordinary-composition;neutral-roles;no-occurrences;"
    "historical-geometry-not-selected"};
constexpr std::uint64_t NoIndex = UINT64_MAX;

Key key(const laplace_id128& id) {
    Key result{}; std::memcpy(result.data(), id.bytes, result.size()); return result;
}
bool equal(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}
bool zero(const laplace_digest256& value) {
    for (const auto byte : value.bytes) if (byte != 0U) return false;
    return true;
}
bool add(std::uint64_t& value, const std::uint64_t extra) {
    if (extra > UINT64_MAX - value) return false;
    value += extra; return true;
}
bool charge(std::uint64_t& value, const std::uint64_t count, const std::uint64_t width) {
    return count <= UINT64_MAX / width && add(value, count * width);
}
void hash_u64(blake3_hasher& hasher, std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (unsigned i = 0; i < 8U; ++i) { bytes[i] = static_cast<std::uint8_t>(value); value >>= 8U; }
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}
struct MemoryLimit final : std::bad_alloc {};
class Memory final : public std::pmr::memory_resource {
    std::uint64_t limit_, used_{};
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (bytes > limit_ - used_) throw MemoryLimit{};
        void* result = ::operator new(bytes, std::align_val_t(alignment));
        used_ += bytes; return result;
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        used_ -= bytes; ::operator delete(pointer, std::align_val_t(alignment));
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
public:
    explicit Memory(std::uint64_t limit) : limit_(limit) {}
};
struct Run { std::uint64_t node; std::uint64_t count; };
struct Node {
    laplace_id128 id{};
    laplace_digest256 witness{};
    std::uint64_t atom_index{NoIndex}, first_run{}, run_count{}, result_index{NoIndex}, height{1U};
    bool witnessed{}, topology{};
    std::uint8_t state{};
};
struct Frame { std::uint64_t node{}, next{}, depth{}; };
struct Shape {
    std::uint64_t carriers{}, logical{}, maximum_parent{}, node_capacity{}, bound{}, scratch{};
};

Status preflight(const laplace_content_reference_input* input, Shape& shape) {
    if (input == nullptr || input->context == nullptr ||
        (input->root_count != 0U && input->roots == nullptr) ||
        (input->source_count != 0U && input->sources == nullptr) ||
        (input->atom_count != 0U && input->atoms == nullptr))
        return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    const auto& cap = input->limits;
    if (cap.maximum_nodes == 0U || cap.maximum_depth == 0U || cap.maximum_memory_bytes == 0U)
        return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    std::uint64_t physicalities = input->source_count;
    if (!add(physicalities, input->atom_count) || physicalities > cap.maximum_physicalities ||
        input->root_count > cap.maximum_roots) return LAPLACE_CONTENT_REFERENCE_LIMIT;
    shape.logical = input->atom_count;
    if (shape.logical > cap.maximum_logical_count) return LAPLACE_CONTENT_REFERENCE_LIMIT;
    for (std::uint64_t i = 0; i < input->source_count; ++i) {
        const auto& source = input->sources[i];
        if (source.physicality == nullptr || (source.carrier_count != 0U && source.carriers == nullptr))
            return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
        if (!add(shape.carriers, source.carrier_count) || shape.carriers > cap.maximum_carriers ||
            !add(shape.logical, source.physicality->logical_count) || shape.logical > cap.maximum_logical_count)
            return LAPLACE_CONTENT_REFERENCE_LIMIT;
        shape.maximum_parent = std::max(shape.maximum_parent, source.carrier_count);
    }
    std::uint64_t potential = input->root_count;
    if (!add(potential, input->source_count) || !add(potential, input->atom_count) ||
        !add(potential, shape.carriers)) return LAPLACE_CONTENT_REFERENCE_LIMIT;
    shape.node_capacity = std::min(potential, cap.maximum_nodes);
    if (laplace_physicality_entity_validation_memory_bound(shape.maximum_parent, &shape.scratch)
        != LAPLACE_PHYSICALITY_ENTITY_OK) return LAPLACE_CONTENT_REFERENCE_LIMIT;
    // Covers copied nodes/maps, traversal, operands/results/known tuples, capacity
    // growth and allocator bookkeeping. PMR independently enforces its admitted
    // allocation bytes; shared validator scratch is reserved separately.
    shape.bound = UINT64_C(65536);
    if (!add(shape.bound, shape.scratch) || !charge(shape.bound, shape.node_capacity, 2048U) ||
        !charge(shape.bound, shape.carriers, 256U) || !charge(shape.bound, input->root_count, 128U) ||
        !charge(shape.bound, input->atom_count, 256U) || shape.bound > SIZE_MAX)
        return LAPLACE_CONTENT_REFERENCE_LIMIT;
    return LAPLACE_CONTENT_REFERENCE_OK;
}
}  // namespace

struct laplace_content_reference_plan {
    Memory memory;
    laplace_framework_context context{};
    laplace_digest256 source{}, recipe{};
    Shape shape{};
    std::uint64_t source_count{}, maximum_depth{};
    Status status{LAPLACE_CONTENT_REFERENCE_INCOMPLETE};
    std::pmr::map<Key, std::uint64_t> index;
    std::pmr::vector<Node> nodes;
    std::pmr::vector<Run> runs;
    std::pmr::vector<laplace_composition_known_entity> known;
    std::pmr::vector<laplace_composition_operand> operands;
    std::pmr::vector<laplace_composition_request> requests;
    std::pmr::vector<laplace_content_reference_root> roots;
    std::pmr::vector<laplace_id128> missing;
    std::pmr::vector<std::uint64_t> postorder, expected_results;
    explicit laplace_content_reference_plan(std::uint64_t bytes) : memory(bytes),
        index(&memory), nodes(&memory), runs(&memory), known(&memory), operands(&memory),
        requests(&memory), roots(&memory), missing(&memory), postorder(&memory), expected_results(&memory) {}
};

namespace {
Status node_for(laplace_content_reference_plan& plan, const laplace_id128& id,
                std::uint64_t& index) {
    const auto found = plan.index.find(key(id));
    if (found != plan.index.end()) { index = found->second; return LAPLACE_CONTENT_REFERENCE_OK; }
    if (plan.nodes.size() >= plan.shape.node_capacity) return LAPLACE_CONTENT_REFERENCE_LIMIT;
    index = plan.nodes.size(); plan.nodes.push_back(Node{}); plan.nodes.back().id = id;
    plan.index.emplace(key(id), index); return LAPLACE_CONTENT_REFERENCE_OK;
}
Status witness(Node& node, const laplace_digest256& value) {
    if (zero(value) || std::memcmp(node.id.bytes, value.bytes, sizeof(node.id.bytes)) != 0 ||
        (node.witnessed && !equal(node.witness, value))) return LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID;
    node.witness = value; node.witnessed = true; return LAPLACE_CONTENT_REFERENCE_OK;
}
Status admit_atoms(laplace_content_reference_plan& plan, const laplace_content_reference_input& input,
                   blake3_hasher& provenance) {
    for (std::uint64_t i = 0; i < input.atom_count; ++i) {
        const auto& atom = input.atoms[i]; const auto& known = atom.known;
        if (atom.physicality == nullptr || known.has_atom != 1U || known.tier_floor != 0U || known.reserved != 0U)
            return LAPLACE_CONTENT_REFERENCE_ATOM_INVALID;
        const auto& body = *atom.physicality;
        laplace_physicality_entity_validation checked{};
        laplace_id128 identity{}; laplace_digest256 full{};
        if (body.physicality_type != LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT ||
            laplace_physicality_entity_record_validate(&body, nullptr, 0U, 0U, 1U, &checked) != LAPLACE_PHYSICALITY_ENTITY_OK ||
            laplace_identity_codepoint_witness(known.atom, &identity, &full) != LAPLACE_IDENTITY_OK ||
            !laplace_identity_equal(&identity, &known.entity_id) || !equal(full, known.identity_witness) ||
            !laplace_identity_equal(&body.entity_id, &identity) || !equal(body.physicality_id, known.physicality_id) ||
            !equal(body.geometry_epoch, plan.context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY]) ||
            std::memcmp(&body.centroid, &known.centroid, sizeof(body.centroid)) != 0)
            return LAPLACE_CONTENT_REFERENCE_ATOM_INVALID;
        std::uint64_t index{}; auto status = node_for(plan, known.entity_id, index);
        if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
        auto& node = plan.nodes[index]; status = witness(node, full);
        if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
        if (node.atom_index != NoIndex) {
            const auto& prior = plan.known[node.atom_index];
            if (!equal(prior.physicality_id, known.physicality_id) ||
                std::memcmp(&prior.centroid, &known.centroid, sizeof(known.centroid)) != 0)
                return LAPLACE_CONTENT_REFERENCE_ATOM_INVALID;
        } else { node.atom_index = plan.known.size(); plan.known.push_back(known); }
        blake3_hasher_update(&provenance, body.physicality_id.bytes, sizeof(body.physicality_id.bytes));
        blake3_hasher_update(&provenance, full.bytes, sizeof(full.bytes));
    }
    return LAPLACE_CONTENT_REFERENCE_OK;
}
Status admit_sources(laplace_content_reference_plan& plan, const laplace_content_reference_input& input,
                     blake3_hasher& provenance) {
    std::pmr::vector<Run> normalized(&plan.memory);
    normalized.reserve(static_cast<std::size_t>(plan.shape.maximum_parent));
    for (std::uint64_t i = 0; i < input.source_count; ++i) {
        const auto& source = input.sources[i]; const auto& body = *source.physicality;
        laplace_physicality_entity_validation checked{};
#if !defined(LAPLACE_TEST_CONTENT_REFERENCE_SKIP_FORM_AUTHENTICATION)
        if (laplace_physicality_entity_record_validate(&body, source.carriers, source.carrier_count,
                input.limits.maximum_carriers, input.limits.maximum_logical_count, &checked) != LAPLACE_PHYSICALITY_ENTITY_OK)
            return LAPLACE_CONTENT_REFERENCE_SOURCE_INVALID;
#endif
        std::uint64_t index{}; auto status = node_for(plan, body.entity_id, index);
        if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
        status = witness(plan.nodes[index], source.identity_witness);
        if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
        if (checked.witness_available != 0U && !equal(checked.realized_identity_witness, source.identity_witness))
            return LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID;
        blake3_hasher_update(&provenance, body.physicality_id.bytes, sizeof(body.physicality_id.bytes));
        blake3_hasher_update(&provenance, source.identity_witness.bytes, sizeof(source.identity_witness.bytes));
        if (body.physicality_type == LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT)
            continue;
#if !defined(LAPLACE_TEST_CONTENT_REFERENCE_SINGLETON_AS_TOPOLOGY)
        if (body.logical_count == 1U)
            continue;  // Valid transparent form; it cannot supply its own closure.
#endif
        if (plan.nodes[index].atom_index != NoIndex) return LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID;
        normalized.clear(); std::uint64_t ordinal = 1U;
        for (std::uint64_t carrier = 0; carrier < source.carrier_count; ++carrier) {
            laplace_composition_occurrence occurrence{};
            if (laplace_trajectory_composition_decode_one(&source.carriers[carrier], ordinal, &occurrence) != LAPLACE_TRAJECTORY_OK)
                return LAPLACE_CONTENT_REFERENCE_SOURCE_INVALID;
            std::uint64_t child{}; status = node_for(plan, occurrence.entity_id, child);
            if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
            if (!normalized.empty() && normalized.back().node == child) {
                if (!add(normalized.back().count, occurrence.run_length)) return LAPLACE_CONTENT_REFERENCE_LIMIT;
            } else normalized.push_back({child, occurrence.run_length});
            if (carrier + 1U < source.carrier_count && !add(ordinal, occurrence.run_length))
                return LAPLACE_CONTENT_REFERENCE_LIMIT;
        }
        auto& node = plan.nodes[index];
        if (node.topology) {
            if (node.run_count != normalized.size()) return LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID;
            for (std::size_t run = 0; run < normalized.size(); ++run) {
                const auto& prior = plan.runs[node.first_run + run];
                if (prior.node != normalized[run].node || prior.count != normalized[run].count)
                    return LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID;
            }
        } else {
            node.first_run = plan.runs.size(); node.run_count = normalized.size(); node.topology = true;
            plan.runs.insert(plan.runs.end(), normalized.begin(), normalized.end());
        }
    }
    return LAPLACE_CONTENT_REFERENCE_OK;
}
Status close_graph(laplace_content_reference_plan& plan, const laplace_content_reference_input& input) {
    std::pmr::vector<Frame> stack(&plan.memory);
    stack.reserve(static_cast<std::size_t>(std::min(input.limits.maximum_depth, plan.shape.node_capacity)));
    // Validate closure of every supplied entity, including forms outside a root's
    // reachable slice. An unauthenticated atomic/full witness cannot hide there.
    for (const auto& entry : plan.index) {
        const auto start = entry.second;
        if (plan.nodes[start].state == 2U) continue;
        stack.push_back({start, 0U, 1U});
        while (!stack.empty()) {
            auto& frame = stack.back(); auto& node = plan.nodes[frame.node];
            if (frame.depth > input.limits.maximum_depth) return LAPLACE_CONTENT_REFERENCE_LIMIT;
            plan.maximum_depth = std::max(plan.maximum_depth, frame.depth);
            node.state = 1U;
            if (node.atom_index != NoIndex || !node.topology) {
                if (node.atom_index == NoIndex) plan.missing.push_back(node.id);
                node.state = 2U; stack.pop_back(); continue;
            }
            if (frame.next < node.run_count) {
                const auto child_index = plan.runs[node.first_run + frame.next].node;
                auto& child = plan.nodes[child_index];
                if (child.state == 1U) return LAPLACE_CONTENT_REFERENCE_CYCLE;
                if (child.state != 2U) {
                    if (frame.depth == UINT64_MAX) return LAPLACE_CONTENT_REFERENCE_LIMIT;
                    stack.push_back({child_index, 0U, frame.depth + 1U}); continue;
                }
                if (child.height >= input.limits.maximum_depth ||
                    frame.depth > input.limits.maximum_depth - child.height)
                    return LAPLACE_CONTENT_REFERENCE_LIMIT;
                node.height = std::max(node.height, child.height + 1U);
                plan.maximum_depth = std::max(plan.maximum_depth, frame.depth + child.height);
                ++frame.next;
            } else {
                node.state = 2U; plan.postorder.push_back(frame.node); stack.pop_back();
            }
        }
    }
    if (!plan.missing.empty()) {
        std::sort(plan.missing.begin(), plan.missing.end(), [](const auto& a, const auto& b) { return key(a) < key(b); });
        return LAPLACE_CONTENT_REFERENCE_INCOMPLETE;
    }
    return LAPLACE_CONTENT_REFERENCE_OK;
}
Status build_requests(laplace_content_reference_plan& plan, const laplace_content_reference_input& input) {
    for (const auto index : plan.postorder) {
        auto& node = plan.nodes[index]; const auto first = plan.operands.size();
        for (std::uint64_t i = 0; i < node.run_count; ++i) {
            const auto& run = plan.runs[node.first_run + i]; const auto& child = plan.nodes[run.node];
            const bool atomic = child.atom_index != NoIndex;
            plan.operands.push_back({atomic ? child.atom_index : child.result_index, run.count, 0U,
                static_cast<std::uint32_t>(atomic ? LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY : LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT), 0U});
        }
        if (!node.witnessed) return LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID;
        node.result_index = plan.requests.size();
        plan.requests.push_back({first, node.run_count, node.result_index + 1U,
            LAPLACE_CONTENT_REFERENCE_VIEW_VERSION, 0U, plan.recipe,
            plan.context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY], {}});
        plan.expected_results.push_back(index);
    }
    for (std::uint64_t i = 0; i < input.root_count; ++i) {
        const auto& node = plan.nodes[plan.index.at(key(input.roots[i]))];
        const bool atomic = node.atom_index != NoIndex;
        plan.roots.push_back({node.id, node.witness, atomic ? node.atom_index : node.result_index,
            static_cast<std::uint32_t>(atomic ? LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY : LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT), 0U});
    }
    return LAPLACE_CONTENT_REFERENCE_OK;
}
}  // namespace

laplace_content_reference_status laplace_content_reference_plan_memory_bound(
    const laplace_content_reference_input* input, std::uint64_t* bytes) {
    if (bytes == nullptr) return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    *bytes = 0U; Shape shape{}; const auto status = preflight(input, shape);
    if (status == LAPLACE_CONTENT_REFERENCE_OK) *bytes = shape.bound;
    return status;
}

laplace_content_reference_status laplace_content_reference_plan_create(
    const laplace_content_reference_input* input, laplace_content_reference_plan** output) {
    if (output == nullptr) return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    *output = nullptr; Shape shape{}; auto status = preflight(input, shape);
    if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
    if (laplace_framework_context_validate(input->context) != LAPLACE_FRAMEWORK_OK ||
        (input->context->epoch_mask & (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_GEOMETRY)) == 0U ||
        zero(input->context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY])) return LAPLACE_CONTENT_REFERENCE_CONTEXT_INVALID;
    if (shape.bound > input->limits.maximum_memory_bytes || shape.bound > input->context->resource_grant.memory_bytes)
        return LAPLACE_CONTENT_REFERENCE_LIMIT;
    try {
        auto plan = std::make_unique<laplace_content_reference_plan>(shape.bound - shape.scratch - sizeof(laplace_content_reference_plan));
        plan->context = *input->context; plan->shape = shape; plan->source_count = input->source_count + input->atom_count;
        plan->nodes.reserve(static_cast<std::size_t>(shape.node_capacity));
        plan->runs.reserve(static_cast<std::size_t>(shape.carriers));
        blake3_hasher recipe{}; blake3_hasher_init(&recipe);
        blake3_hasher_update(&recipe, Recipe.data(), Recipe.size());
        blake3_hasher_finalize(&recipe, plan->recipe.bytes, sizeof(plan->recipe.bytes));
        blake3_hasher provenance{}; blake3_hasher_init(&provenance);
        blake3_hasher_update(&provenance, Recipe.data(), Recipe.size());
        hash_u64(provenance, input->root_count); hash_u64(provenance, input->source_count); hash_u64(provenance, input->atom_count);
        for (std::uint64_t i = 0; i < input->root_count; ++i) {
            std::uint64_t index{}; status = node_for(*plan, input->roots[i], index);
            if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
            blake3_hasher_update(&provenance, input->roots[i].bytes, sizeof(input->roots[i].bytes));
        }
        status = admit_atoms(*plan, *input, provenance);
        if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
        status = admit_sources(*plan, *input, provenance);
        if (status != LAPLACE_CONTENT_REFERENCE_OK) return status;
        blake3_hasher_finalize(&provenance, plan->source.bytes, sizeof(plan->source.bytes));
        status = close_graph(*plan, *input);
        if (status != LAPLACE_CONTENT_REFERENCE_OK && status != LAPLACE_CONTENT_REFERENCE_INCOMPLETE) return status;
        if (status == LAPLACE_CONTENT_REFERENCE_OK) status = build_requests(*plan, *input);
        if (status != LAPLACE_CONTENT_REFERENCE_OK && status != LAPLACE_CONTENT_REFERENCE_INCOMPLETE) return status;
        plan->status = status; *output = plan.release(); return status;
    } catch (const MemoryLimit&) { return LAPLACE_CONTENT_REFERENCE_LIMIT; }
      catch (const std::length_error&) { return LAPLACE_CONTENT_REFERENCE_LIMIT; }
      catch (const std::bad_alloc&) { return LAPLACE_CONTENT_REFERENCE_MEMORY_FAILURE; }
}

laplace_content_reference_status laplace_content_reference_plan_view_get(
    const laplace_content_reference_plan* plan, laplace_content_reference_plan_view* view) {
    if (view == nullptr) return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    *view = {}; if (plan == nullptr) return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    view->context = &plan->context; view->source_fingerprint = plan->source; view->recipe_fingerprint = plan->recipe;
    view->view_geometry_epoch = plan->context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY];
    view->missing_entity_ids = plan->missing.data(); view->missing_entity_count = plan->missing.size();
    view->authenticated_physicality_count = plan->source_count; view->distinct_node_count = plan->nodes.size();
    view->carrier_count = plan->shape.carriers; view->logical_count = plan->shape.logical;
    view->maximum_observed_depth = plan->maximum_depth; view->memory_bound_bytes = plan->shape.bound;
    view->version = LAPLACE_CONTENT_REFERENCE_VIEW_VERSION; view->status = plan->status;
    if (plan->status == LAPLACE_CONTENT_REFERENCE_OK) {
        view->known_entities = plan->known.data(); view->known_entity_count = plan->known.size();
        view->operands = plan->operands.data(); view->operand_count = plan->operands.size();
        view->requests = plan->requests.data(); view->request_count = plan->requests.size();
        view->roots = plan->roots.data(); view->root_count = plan->roots.size();
    }
    return plan->status;
}

laplace_content_reference_status laplace_content_reference_plan_verify_results(
    const laplace_content_reference_plan* plan, const laplace_composition_result* results,
    std::uint64_t result_count) {
    if (plan == nullptr || (result_count != 0U && results == nullptr)) return LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT;
    if (plan->status != LAPLACE_CONTENT_REFERENCE_OK) return LAPLACE_CONTENT_REFERENCE_INCOMPLETE;
    if (result_count != plan->expected_results.size()) return LAPLACE_CONTENT_REFERENCE_RESULT_INVALID;
    for (std::uint64_t i = 0; i < result_count; ++i) {
        const auto& expected = plan->nodes[plan->expected_results[i]];
        if (!laplace_identity_equal(&expected.id, &results[i].entity_id)) return LAPLACE_CONTENT_REFERENCE_RESULT_INVALID;
#if !defined(LAPLACE_TEST_CONTENT_REFERENCE_IGNORE_RESULT_WITNESS)
        if (!equal(expected.witness, results[i].identity_witness)) return LAPLACE_CONTENT_REFERENCE_RESULT_INVALID;
#endif
    }
    return LAPLACE_CONTENT_REFERENCE_OK;
}
void laplace_content_reference_plan_destroy(laplace_content_reference_plan** plan) {
    if (plan == nullptr) return;
    delete *plan;
    *plan = nullptr;
}
