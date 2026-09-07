#ifndef LAPLACE_COGNITION_INTERPRETATION_H
#define LAPLACE_COGNITION_INTERPRETATION_H

#include <stddef.h>
#include <stdint.h>
#include "laplace/export.h"
#include "laplace/identity.h"
#include "laplace/types.h"
#include "laplace/cognition_turn.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { LAPLACE_COGNITION_INTERPRETATION_VERSION = 1 };

/* A factor is an admitted relation over interpretation slots, not a text
 * pattern. Rows retain their exact evidence or calculation provenance. All
 * factors constrain the same observation; alternatives within a factor remain
 * alternatives. No frequency, token class, or geometric score elects a row. */
typedef struct laplace_cognition_interpretation_row {
    const laplace_id128* values;
    laplace_digest256 observation_id;
    laplace_digest256 evidence_root_id;
    laplace_digest256 calculation_receipt_id;
} laplace_cognition_interpretation_row;

typedef struct laplace_cognition_interpretation_factor {
    laplace_digest256 factor_id;
    laplace_digest256 law_id;
    const uint32_t* slots;
    size_t slot_count;
    const laplace_cognition_interpretation_row* rows;
    size_t row_count;
} laplace_cognition_interpretation_factor;

typedef struct laplace_cognition_interpretation_program {
    laplace_id128 observation_root;
    laplace_digest256 occurrence_id;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_id;
    laplace_digest256 evidence_epoch;
    laplace_digest256 boundary_id;
    laplace_digest256 authority_id;
    const laplace_digest256* slot_ids;
    size_t slot_count;
    uint64_t maximum_comparisons;
    uint64_t maximum_states;
    uint64_t maximum_memory_bytes;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_interpretation_program;

typedef enum laplace_cognition_interpretation_status {
    LAPLACE_COGNITION_INTERPRETATION_OK = 0,
    LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_INTERPRETATION_EXHAUSTED = 2,
    LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE = 3,
    LAPLACE_COGNITION_INTERPRETATION_RANGE = 4,
    LAPLACE_COGNITION_INTERPRETATION_NOT_UNIQUE = 5,
    LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH = 6,
    LAPLACE_COGNITION_INTERPRETATION_TURN_FAILURE = 7,
    LAPLACE_COGNITION_INTERPRETATION_GUIDANCE_FAILURE = 8,
    LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE = 9
} laplace_cognition_interpretation_status;

typedef enum laplace_cognition_interpretation_disposition {
    LAPLACE_COGNITION_INTERPRETATION_UNRESOLVED = 0,
    LAPLACE_COGNITION_INTERPRETATION_UNIQUE = 1,
    LAPLACE_COGNITION_INTERPRETATION_AMBIGUOUS = 2,
    LAPLACE_COGNITION_INTERPRETATION_NO_COMPATIBLE_BINDING = 3
} laplace_cognition_interpretation_disposition;

typedef struct laplace_cognition_interpretation_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 result_fingerprint;
    uint64_t comparisons;
    uint64_t rejected_pairs;
    uint64_t generated_states;
    uint64_t peak_frontier_states;
    uint64_t derivation_count;
    uint64_t completed_factor_count;
    uint32_t disposition;
    uint32_t version;
} laplace_cognition_interpretation_receipt;

typedef struct laplace_cognition_interpretation_result
    laplace_cognition_interpretation_result;

typedef struct laplace_cognition_interpretation_witness {
    laplace_digest256 factor_id;
    laplace_digest256 law_id;
    laplace_digest256 observation_id;
    laplace_digest256 evidence_root_id;
    laplace_digest256 calculation_receipt_id;
} laplace_cognition_interpretation_witness;

/* A witnessed program binds typed queries to the joint interpretation. Operands
 * are slot identities, not prompt strings. Every obligation starts OPEN;
 * selecting a program does not assert that any of its work has succeeded. */
typedef struct laplace_cognition_interpretation_obligation_rule {
    laplace_digest256 law_id;
    laplace_digest256 result_contract_fingerprint;
    const laplace_digest256* operand_slots;
    size_t operand_count;
    /* Query kind is defined by the selected program, independently of the
     * scheduler's finite operation-kind enumeration. */
    uint32_t kind;
    uint32_t flags;
} laplace_cognition_interpretation_obligation_rule;

typedef struct laplace_cognition_interpretation_program_rule {
    laplace_id128 program_entity_id;
    laplace_digest256 program_slot_id;
    laplace_digest256 goal_slot_id;
    laplace_digest256 recipe_id;
    laplace_digest256 witness_id;
    laplace_digest256 result_contract_fingerprint;
    const laplace_cognition_interpretation_obligation_rule* obligations;
    size_t obligation_count;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_interpretation_program_rule;

/* Executes the finite conjunction of witnessed candidate relations. A complete
 * result retains every compatible joint binding AND every derivation, including
 * different provenance paths to identical bindings. UNIQUE means uniqueness
 * within this exact supplied boundary, not independent truth or answerability.
 * Exhaustion never publishes a unique interpretation from an incomplete scan.
 * Slot IDs and factor IDs are strictly ordered by exact digest bytes. Slot
 * indices within each factor are strictly increasing. These orders canonicalize
 * the input declaration; they do not assign semantic importance to a slot.
 * Inputs are borrowed for the call; a successful result owns all returned data. */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_execute(
    const laplace_cognition_interpretation_program* program,
    const laplace_cognition_interpretation_factor* factors, size_t factor_count,
    laplace_cognition_interpretation_result** result,
    laplace_cognition_interpretation_receipt* receipt);

LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_read(
    const laplace_cognition_interpretation_result* result, size_t derivation,
    laplace_id128* values, size_t value_capacity,
    uint64_t* selected_rows, size_t row_capacity);

/* The complete supplied provenance remains readable, including rows excluded
 * by joint constraints. Neither rejection nor reuse deletes source history. */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_read_witness(
    const laplace_cognition_interpretation_result* result,
    size_t factor_index, size_t row_index,
    laplace_cognition_interpretation_witness* witness);

/* Compile the interpreted task into the existing native guidance machine.
 * The full program's obligations remain separately addressable. The exact goal
 * entity used by a targeted observation search is not the completion condition
 * for this task program. The result is consumed by the canonical forward pass. */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_compile_guidance(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_cognition_interpretation_program_rule* rule,
    laplace_cognition_guidance_state** guidance);

/* Execute all task obligations through the existing native forward machine.
 * Observation, occurrence and evidence scope come from the owned interpretation.
 * Limits supplies physical execution bounds; its program and result-contract
 * identities are replaced by those of the compiled witnessed task. An OK return
 * owns a readable result even for a typed incomplete/exhausted disposition;
 * only its native receipt establishes whether the task completed. */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_forward_execute(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_cognition_interpretation_program_rule* rule,
    const laplace_cognition_forward_program* limits,
    const laplace_cognition_forward_provider_v1* provider,
    laplace_cognition_forward_result** result,
    laplace_cognition_forward_receipt* receipt);

/* Retrieve an actual canonical operand for a projected obligation. Callers do
 * not attempt to reconstruct bindings from a fingerprint or use a topic ID. */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_read_binding(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* slot_id, laplace_id128* value);

/* Bind the declared goal slot only after the complete joint interpretation is
 * unique. Policy supplies execution bounds and admissibility, with GOAL_PRESENT
 * absent and goal_entity_id zero. Every inferred slot and retained derivation
 * enters the request context through the interpretation receipt. This is not a
 * rank-one topic selector: any remaining joint ambiguity prevents publication. */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_prepare_turn(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    laplace_cognition_turn_input* inferred_turn,
    laplace_cognition_turn_policy* inferred_policy);

LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_compile_turn(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    const uint8_t* previous_frame, size_t previous_frame_bytes,
    laplace_cognition_observation_request* request,
    laplace_cognition_turn_receipt* receipt);

LAPLACE_API void laplace_cognition_interpretation_destroy(
    laplace_cognition_interpretation_result** result);

#ifdef __cplusplus
}
#endif
#endif
