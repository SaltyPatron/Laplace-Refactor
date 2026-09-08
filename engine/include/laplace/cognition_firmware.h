#ifndef LAPLACE_COGNITION_FIRMWARE_H
#define LAPLACE_COGNITION_FIRMWARE_H

#include <stddef.h>
#include <stdint.h>
#include "laplace/cognition_prompt_admission.h"
#include "laplace/cognition_realization.h"
#include "laplace/cognition_materialization.h"
#include "laplace/cognition_discourse_frame.h"
#include "laplace/framework.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_FIRMWARE_VERSION = 1,
    LAPLACE_COGNITION_FIRMWARE_NO_BINDING = 0,
    LAPLACE_COGNITION_FIRMWARE_OBSERVATION = 1,
    LAPLACE_COGNITION_FIRMWARE_ANSWER = 2,
    LAPLACE_COGNITION_FIRMWARE_REALIZATION = 3,
    LAPLACE_COGNITION_FIRMWARE_PREVIOUS_ANSWER = 4,
    LAPLACE_COGNITION_FIRMWARE_PREVIOUS_REALIZATION = 5,
    LAPLACE_COGNITION_FIRMWARE_INTERPRET = 1,
    LAPLACE_COGNITION_FIRMWARE_EXECUTE = 2,
    LAPLACE_COGNITION_FIRMWARE_EMIT = 3,
    LAPLACE_COGNITION_FIRMWARE_STATE_HAS_REALIZATION = 1
};

typedef enum laplace_cognition_firmware_status {
    LAPLACE_COGNITION_FIRMWARE_OK = 0,
    LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID = 2,
    LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH = 3,
    LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID = 4,
    LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID = 5,
    LAPLACE_COGNITION_FIRMWARE_BINDING_ABSENT = 6,
    LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE = 7,
    LAPLACE_COGNITION_FIRMWARE_COGNITION_FAILURE = 8,
    LAPLACE_COGNITION_FIRMWARE_INCOMPLETE = 9,
    LAPLACE_COGNITION_FIRMWARE_AMBIGUOUS = 10,
    LAPLACE_COGNITION_FIRMWARE_REALIZATION_FAILURE = 11,
    LAPLACE_COGNITION_FIRMWARE_MATERIALIZATION_FAILURE = 12,
    LAPLACE_COGNITION_FIRMWARE_LIMIT = 13,
    LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE = 14,
    LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE = 15,
    LAPLACE_COGNITION_FIRMWARE_CANCELLED = 16,
    LAPLACE_COGNITION_FIRMWARE_DENIED = 17,
    LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT = 18
} laplace_cognition_firmware_status;

/* Sources are whole observations or actual earlier native results. There is no
 * constant/expected-answer operand and no topic, language or prompt classifier.
 * Step references are zero-based. Observation/NONE require step_index == 0. */
typedef struct laplace_cognition_firmware_binding {
    uint32_t source;
    uint32_t step_index;
} laplace_cognition_firmware_binding;

/* An interpreted data-flow program over the existing cognition and realization
 * owners. INTERPRET and EXECUTE differ in intent/trace, not in search semantics.
 * Only EMIT has a realization policy/encoding: it consumes a prior ANSWER act,
 * requires relation_mask == 0 and has no goal. Other steps execute native
 * cognition. Goalless queries require one unique entity across the complete
 * candidate batch; goal-bound queries bind an already-calculated entity.
 * Ambiguity never silently becomes the first row.
 * Relations and output modalities are selected by this program, never the text.
 * A goal is absent or bound from an earlier result, not supplied by a transport. */
typedef struct laplace_cognition_firmware_step {
    laplace_cognition_firmware_binding anchor;
    laplace_cognition_firmware_binding goal;
    laplace_cognition_realization_request realization;
    uint32_t kind;
    uint32_t relation_mask;
    uint32_t output_encoding;
    uint32_t reserved;
} laplace_cognition_firmware_step;

typedef struct laplace_cognition_firmware_program {
    const laplace_cognition_firmware_step* steps;
    uint32_t step_count;
    uint32_t version;
} laplace_cognition_firmware_program;

/* Request authority, evidence and resource boundaries are independent of firmware.
 * The full framework context must identify selected_program in its firmware epoch.
 * Search limits are per-step and requested_path_count must be one; forward_limits are a conserved whole-program grant.
 * There are deliberately no goal, relation-mask, topic or output-mode fields here. */
typedef struct laplace_cognition_firmware_request {
    laplace_digest256 selected_program;
    laplace_digest256 evidence_boundary;
    laplace_digest256 result_contract_fingerprint;
    /* Loaded from the host's selected predecessor, not inferred from incoming
     * checkpoint bytes. Presence is explicit; hash equality is not authorization. */
    laplace_digest256 expected_previous_checkpoint;
    uint32_t previous_checkpoint_present;
    laplace_query_search_budget search_budget;
    laplace_cognition_observation_forward_limits forward_limits;
    laplace_cognition_materialization_request materialization;
    uint64_t maximum_output_bytes;
    uint64_t maximum_checkpoint_bytes;
    uint32_t boundary_flags;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_firmware_request;

typedef struct laplace_cognition_firmware_step_receipt {
    laplace_cognition_observation_request request;
    laplace_cognition_forward_receipt cognition;
    laplace_cognition_semantic_act act;
    laplace_cognition_realization_result realization;
    laplace_cognition_realization_receipt realization_receipt;
    laplace_cognition_materialization_receipt materialization;
    laplace_digest256 prior_feedback;
    laplace_digest256 next_feedback;
    uint64_t output_offset;
    uint64_t output_bytes;
    uint32_t kind;
    uint32_t reserved;
} laplace_cognition_firmware_step_receipt;

typedef struct laplace_cognition_firmware_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 program_id;
    laplace_digest256 prompt_admission_receipt_id;
    laplace_digest256 original_observation_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 previous_checkpoint_fingerprint;
    laplace_digest256 next_checkpoint_fingerprint;
    laplace_digest256 trace_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t output_bytes;
    uint64_t checkpoint_bytes;
    uint64_t layer_count;
    uint64_t provider_call_count;
    uint64_t resource_cost;
    uint64_t io_operations;
    uint64_t database_operations;
    uint32_t completed_steps;
    uint32_t emitted_parts;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_firmware_receipt;

typedef struct laplace_cognition_firmware_error {
    uint32_t status;
    uint32_t step_index;
    uint32_t native_status;
    uint32_t native_disposition;
} laplace_cognition_firmware_error;

typedef struct laplace_cognition_firmware_result laplace_cognition_firmware_result;

LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_identify(
    const laplace_cognition_firmware_program* program, laplace_digest256* identity);

/* Pure native computation. No testimony is generated and no effect is executed.
 * Output, frame and receipt are available only if the whole program succeeds.
 * A valid predecessor checkpoint restores actual typed results, not merely a hash
 * or caller-constructed state. All later requests bind the preceding feedback,
 * and answer/realization references consume the actual values in that state.
 * A cancellation callback is sampled before each step and before publication.
 * Its state and every provider are borrowed for this call only. */
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_execute(
    const laplace_cognition_firmware_program* program,
    const laplace_cognition_firmware_request* request,
    const laplace_framework_context* context,
    laplace_cognition_prompt_admission* admission,
    const uint8_t* previous_checkpoint, size_t previous_checkpoint_bytes,
    const laplace_cognition_observation_candidate_provider_v1* providers,
    size_t provider_count,
    const laplace_cognition_realization_provider_v1* realization_provider,
    const laplace_cognition_materialization_provider_v1* materialization_provider,
    laplace_framework_cancel_requested_fn cancel_requested, void* cancel_state,
    laplace_cognition_firmware_result** result,
    laplace_cognition_firmware_error* error);

LAPLACE_API void laplace_cognition_firmware_result_destroy(
    laplace_cognition_firmware_result** result);
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_result_receipt(
    const laplace_cognition_firmware_result* result, laplace_cognition_firmware_receipt* receipt);
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_result_step(
    const laplace_cognition_firmware_result* result, size_t index,
    laplace_cognition_firmware_step_receipt* receipt);
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_result_output(
    const laplace_cognition_firmware_result* result, const uint8_t** bytes, size_t* count);
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_result_checkpoint(
    const laplace_cognition_firmware_result* result, const uint8_t** bytes, size_t* count);

/* Publish an observation/result stream through the existing framework sink
 * lifecycle. No private persistence lane or semantic testimony is introduced.
 * The caller chooses the independently admitted record type. The stream contains
 * the complete checkpoint and exact output; its source is the original prompt
 * admission and its recipe is the selected executable firmware. */
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_publish(
    const laplace_cognition_firmware_result* result,
    const laplace_framework_context* context,
    uint32_t record_type,
    laplace_framework_sink_v1* sinks, size_t sink_count,
    laplace_framework_stream_receipt* receipt);

/* Portable immutable firmware image: little-endian fixed-width fields, no
 * process addresses or ABI padding. Load verifies the independently selected
 * identity before decoding. A matching hash is not a grant of authority: execute
 * still requires the selected firmware epoch and all independent context rights. */
typedef struct laplace_cognition_firmware_image laplace_cognition_firmware_image;
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_image_create(
    const laplace_cognition_firmware_program* program, uint64_t memory_limit,
    laplace_cognition_firmware_image** image);
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_image_load(
    const uint8_t* bytes, size_t byte_count, const laplace_digest256* expected_identity,
    uint64_t memory_limit, laplace_cognition_firmware_image** image);
LAPLACE_API void laplace_cognition_firmware_image_destroy(laplace_cognition_firmware_image** image);
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_image_view(
    const laplace_cognition_firmware_image* image, laplace_cognition_firmware_program* program,
    const uint8_t** bytes, size_t* byte_count, laplace_digest256* identity);

/* A runtime supplies mechanisms, not a goal or a replacement semantic engine.
 * Canonical input and result sinks stage inert artifacts. The shared framework
 * activation provider atomically advances its declared database generation only
 * after both streams have sealed. Providers must honor that existing framework
 * contract: prepared/sealed artifacts remain abortable and never self-activate.
 * No semantic testimony is created by this observation/result publication. */
typedef struct laplace_cognition_firmware_runtime {
    const laplace_cognition_prompt_atom_provider_v1* atoms;
    const laplace_composition_presence_provider_v1* presence;
    const laplace_cognition_observation_candidate_provider_v1* cognition;
    size_t cognition_count;
    const laplace_cognition_realization_provider_v1* realization;
    const laplace_cognition_materialization_provider_v1* materialization;
    laplace_framework_sink_v1* input_sinks;
    size_t input_sink_count;
    laplace_framework_sink_v1* result_sinks;
    size_t result_sink_count;
    const laplace_framework_activation_provider_v1* activation;
    laplace_framework_cancel_requested_fn cancel_requested;
    void* cancel_state;
    uint32_t result_record_type;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_firmware_runtime;

typedef struct laplace_cognition_firmware_run_receipt {
    laplace_digest256 receipt_id;
    laplace_cognition_firmware_receipt execution;
    laplace_framework_producer_receipt input_staging;
    laplace_framework_stream_receipt result_staging;
    laplace_framework_activation_receipt activation;
    uint32_t input_stream_present;
    uint32_t version;
} laplace_cognition_firmware_run_receipt;

/* The raw-observation route: image -> native admission -> executable firmware
 * -> native act/realization/state -> canonical producer and sink staging ->
 * framework-governed activation. No result pointer or success receipt is returned
 * until the shared activation owner commits. Failure can leave inert staged
 * artifacts, never a success claim or a partial answer. The host owns activation
 * atomicity and authorizes the current database epoch independently of firmware. */
LAPLACE_API laplace_cognition_firmware_status laplace_cognition_firmware_run(
    const laplace_cognition_firmware_image* image,
    const laplace_cognition_firmware_request* request,
    const laplace_cognition_prompt_admission_input* observation,
    const uint8_t* previous_checkpoint, size_t previous_checkpoint_bytes,
    const laplace_cognition_firmware_runtime* runtime,
    laplace_cognition_firmware_result** result,
    laplace_cognition_firmware_run_receipt* receipt,
    laplace_cognition_firmware_error* error);

#ifdef __cplusplus
}
#endif
#endif
