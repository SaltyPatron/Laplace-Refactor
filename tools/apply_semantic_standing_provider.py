#!/usr/bin/env python3
from pathlib import Path

p = Path('integrations/postgresql/extension/src/semantic_cognition_pg.c')
text = p.read_text()

def one(old, new):
    global text
    n = text.count(old)
    if n != 1:
        raise SystemExit(f'expected one match, got {n}: {old[:100]!r}')
    text = text.replace(old, new, 1)

one('''    laplace_digest256 evidence_epoch;
    laplace_digest256 provider_fingerprint;''',
'''    laplace_digest256 evidence_epoch;
    laplace_digest256 authority_id;
    laplace_digest256 provider_fingerprint;''')

one('''static void semantic_provider_identify(
    const laplace_digest256* boundary_id,
    const laplace_digest256* evidence_epoch,
    laplace_digest256* provider_fingerprint) {''',
'''static void semantic_provider_identify(
    const laplace_digest256* boundary_id,
    const laplace_digest256* evidence_epoch,
    const laplace_digest256* authority_id,
    laplace_digest256* provider_fingerprint) {''')
one('''    blake3_hasher_update(
        &hasher, evidence_epoch->bytes, sizeof(evidence_epoch->bytes));
    blake3_hasher_finalize(''',
'''    blake3_hasher_update(
        &hasher, evidence_epoch->bytes, sizeof(evidence_epoch->bytes));
    blake3_hasher_update(
        &hasher, authority_id->bytes, sizeof(authority_id->bytes));
    blake3_hasher_finalize(''')

start = text.index('    static const char query[] =', text.index('static int semantic_enumerate_impl'))
end = text.index('    Datum* source_values;', start)
query = '''    static const char query[] =
        "WITH src AS MATERIALIZED ("
        " SELECT entity_id, ordinality - 1 AS source_state_index"
        " FROM unnest($1::bytea[]) WITH ORDINALITY s(entity_id, ordinality)"
        "), edges AS MATERIALIZED ("
        " SELECT (s.source_state_index)::bigint AS source_state_index,"
        " CASE WHEN o.left_value_entity_id=s.entity_id"
        "      THEN o.right_value_entity_id ELSE o.left_value_entity_id END AS target_entity_id,"
        " o.occurrence_id AS observation_fingerprint,"
        " p.proposition_id, p.relation_id, er.root_node_id AS evidence_root_id,"
        " et.uncertainty_numerator, et.uncertainty_denominator,"
        " CASE WHEN p.flags=2 THEN 3"
        "      WHEN o.left_value_entity_id=s.entity_id THEN 1 ELSE 2 END AS direction"
        " FROM src s"
        " JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_occurrence o"
        "   ON o.boundary_id=$2"
        "  AND o.disposition=1"
        "  AND (o.left_value_entity_id=s.entity_id OR o.right_value_entity_id=s.entity_id)"
        " JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_proposition p"
        "   ON p.proposition_id=o.proposition_id"
        " JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony et"
        "   ON et.source_profile_id=o.source_profile_id"
        " JOIN " LAPLACE_PG_SCHEMA ".evidence_node en"
        "   ON en.node_id=et.evidence_node_id"
        "  AND en.proposition_id=o.row_entity_id"
        " JOIN " LAPLACE_PG_SCHEMA ".evidence_root_projection er"
        "   ON er.node_id=en.node_id"
        "  AND er.proposition_id=en.proposition_id"
        " WHERE p.flags IN (1,2)"
        "), dedup AS ("
        " SELECT DISTINCT ON (source_state_index, proposition_id, target_entity_id, evidence_root_id)"
        " source_state_index,target_entity_id,observation_fingerprint,relation_id,evidence_root_id,direction,"
        " uncertainty_numerator,uncertainty_denominator"
        " FROM edges"
        " ORDER BY source_state_index,proposition_id,target_entity_id,evidence_root_id,observation_fingerprint"
        "), standing_lanes AS MATERIALIZED ("
        " SELECT DISTINCT d.source_state_index,d.target_entity_id,d.observation_fingerprint,d.relation_id,d.direction,"
        " d.evidence_root_id,me.participant_coordinate_id"
        " FROM dedup d"
        " JOIN " LAPLACE_PG_SCHEMA ".standing_match_event me"
        "   ON me.eligible_root_id=d.evidence_root_id"
        "), current_standing AS MATERIALIZED ("
        " SELECT l.*,st.state_id,st.coordinate_id,st.arena_scope_id,st.prior_state_id,st.epoch_id,"
        " st.rating_recipe_id,st.rating,st.rating_deviation,st.volatility,st.eligible_match_count,"
        " st.period_ordinal,st.rating_recipe_version,st.flags AS standing_flags"
        " FROM standing_lanes l"
        " JOIN LATERAL ("
        "   SELECT sh.* FROM " LAPLACE_PG_SCHEMA ".standing_state_history sh"
        "   JOIN " LAPLACE_PG_SCHEMA ".standing_recipe_history rh"
        "     ON rh.recipe_id=sh.rating_recipe_id AND rh.evidence_boundary_id=$2"
        "   JOIN " LAPLACE_PG_SCHEMA ".standing_recipe_admission ra"
        "     ON ra.recipe_id=sh.rating_recipe_id AND ra.evidence_epoch=$3 AND ra.authority_fingerprint=$4"
        "   WHERE sh.coordinate_id=l.participant_coordinate_id"
        "   ORDER BY sh.period_ordinal DESC,sh.state_id"
        "   LIMIT 1"
        " ) st ON true"
        "), candidate_rows AS ("
        " SELECT source_state_index,target_entity_id,observation_fingerprint,relation_id,evidence_root_id,direction,"
        " uncertainty_numerator,uncertainty_denominator,2::integer AS source_layer,"
        " NULL::bytea AS state_id,NULL::bytea AS coordinate_id,NULL::bytea AS arena_scope_id,"
        " NULL::bytea AS prior_state_id,NULL::bytea AS epoch_id,NULL::bytea AS rating_recipe_id,"
        " NULL::double precision AS rating,NULL::double precision AS rating_deviation,"
        " NULL::double precision AS volatility,NULL::numeric AS eligible_match_count,"
        " NULL::numeric AS period_ordinal,NULL::integer AS rating_recipe_version,NULL::integer AS standing_flags"
        " FROM dedup"
        " UNION ALL"
        " SELECT source_state_index,target_entity_id,observation_fingerprint,relation_id,NULL::bytea,direction,"
        " NULL::numeric,NULL::numeric,16::integer,state_id,coordinate_id,arena_scope_id,prior_state_id,epoch_id,"
        " rating_recipe_id,rating,rating_deviation,volatility,eligible_match_count,period_ordinal,"
        " rating_recipe_version,standing_flags"
        " FROM current_standing"
        ")"
        " SELECT * FROM candidate_rows"
        " ORDER BY source_state_index,observation_fingerprint,target_entity_id,source_layer,coordinate_id NULLS FIRST";
'''
text = text[:start] + query + text[end:]

one('''    bytea* boundary;
    Oid argument_types[2];
    Datum argument_values[2];''',
'''    bytea* boundary;
    bytea* evidence_epoch;
    bytea* authority_id;
    Oid argument_types[4];
    Datum argument_values[4];''')

one('''    boundary = laplace_pg_bytes_to_bytea(
        state->boundary_id.bytes, sizeof(state->boundary_id.bytes));

    argument_types[0] = get_array_type(BYTEAOID);
    argument_types[1] = BYTEAOID;
    if (argument_types[0] == InvalidOid) {
        return 3;
    }
    argument_values[0] = PointerGetDatum(source_array);
    argument_values[1] = PointerGetDatum(boundary);

    result = SPI_execute_with_args(
        query, 2, argument_types, argument_values, NULL, true, row_limit);''',
'''    boundary = laplace_pg_bytes_to_bytea(
        state->boundary_id.bytes, sizeof(state->boundary_id.bytes));
    evidence_epoch = laplace_pg_bytes_to_bytea(
        state->evidence_epoch.bytes, sizeof(state->evidence_epoch.bytes));
    authority_id = laplace_pg_bytes_to_bytea(
        state->authority_id.bytes, sizeof(state->authority_id.bytes));

    argument_types[0] = get_array_type(BYTEAOID);
    argument_types[1] = BYTEAOID;
    argument_types[2] = BYTEAOID;
    argument_types[3] = BYTEAOID;
    if (argument_types[0] == InvalidOid) {
        return 3;
    }
    argument_values[0] = PointerGetDatum(source_array);
    argument_values[1] = PointerGetDatum(boundary);
    argument_values[2] = PointerGetDatum(evidence_epoch);
    argument_values[3] = PointerGetDatum(authority_id);

    result = SPI_execute_with_args(
        query, 4, argument_types, argument_values, NULL, true, row_limit);''')

loop_start = text.index('    for (source_index = 0u; source_index < (size_t)SPI_processed; ++source_index) {')
loop_end = text.index('\n    *candidate_count = (size_t)SPI_processed;', loop_start)
new_loop = '''    for (source_index = 0u; source_index < (size_t)SPI_processed; ++source_index) {
        HeapTuple tuple = SPI_tuptable->vals[source_index];
        TupleDesc tuple_desc = SPI_tuptable->tupdesc;
        laplace_cognition_observation_candidate* candidate = &candidates[source_index];
        bool is_null = false;
        Datum value;
        int64 state_index;
        int32 direction;
        int32 source_layer;

        memset(candidate, 0, sizeof(*candidate));
        value = SPI_getbinval(tuple, tuple_desc, 1, &is_null);
        if (is_null) return 6;
        state_index = DatumGetInt64(value);
        if (state_index < 0 || (uint64_t)state_index >= frontier_state_count) return 7;
        candidate->source_state_index = (uint64_t)state_index;

        value = SPI_getbinval(tuple, tuple_desc, 2, &is_null);
        if (is_null) return 8;
        semantic_read_id128_datum(value, &candidate->target_entity_id, "semantic target_entity_id");
        value = SPI_getbinval(tuple, tuple_desc, 3, &is_null);
        if (is_null) return 9;
        semantic_read_digest_datum(value, &candidate->observation_fingerprint, "semantic observation_fingerprint");
        value = SPI_getbinval(tuple, tuple_desc, 4, &is_null);
        if (is_null) return 10;
        semantic_read_id128_datum(value, &candidate->relation_id, "semantic relation_id");
        value = SPI_getbinval(tuple, tuple_desc, 6, &is_null);
        if (is_null) return 11;
        direction = DatumGetInt32(value);
        if (direction < 0) return 12;
        value = SPI_getbinval(tuple, tuple_desc, 9, &is_null);
        if (is_null) return 13;
        source_layer = DatumGetInt32(value);

        candidate->source_logical_ordinal = 0u;
        candidate->target_logical_ordinal = 0u;
        candidate->multiplicity = 1u;
        candidate->gap = 1u;
        candidate->relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
        candidate->source_layer = (uint32_t)source_layer;
        candidate->direction = (uint32_t)direction;
        candidate->flags = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;

        if (candidate->source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY) {
            value = SPI_getbinval(tuple, tuple_desc, 5, &is_null);
            if (is_null) return 14;
            semantic_read_digest_datum(value, &candidate->evidence_root_fingerprint, "semantic evidence_root_id");
            value = SPI_getbinval(tuple, tuple_desc, 7, &is_null);
            if (is_null) return 15;
            candidate->evidence_uncertainty_numerator = laplace_pg_uint64_from_numeric(value, "semantic uncertainty_numerator");
            value = SPI_getbinval(tuple, tuple_desc, 8, &is_null);
            if (is_null) return 16;
            candidate->evidence_uncertainty_denominator = laplace_pg_uint64_from_numeric(value, "semantic uncertainty_denominator");
            candidate->flags |= LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT;
        } else if (candidate->source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING) {
            semantic_read_digest_datum(SPI_getbinval(tuple, tuple_desc, 10, &is_null), &candidate->standing.state_id, "standing state_id");
            if (is_null) return 17;
            semantic_read_digest_datum(SPI_getbinval(tuple, tuple_desc, 11, &is_null), &candidate->standing.coordinate_id, "standing coordinate_id");
            if (is_null) return 18;
            semantic_read_digest_datum(SPI_getbinval(tuple, tuple_desc, 12, &is_null), &candidate->standing.arena_scope_id, "standing arena_scope_id");
            if (is_null) return 19;
            value = SPI_getbinval(tuple, tuple_desc, 13, &is_null);
            if (!is_null) semantic_read_digest_datum(value, &candidate->standing.prior_state_id, "standing prior_state_id");
            semantic_read_digest_datum(SPI_getbinval(tuple, tuple_desc, 14, &is_null), &candidate->standing.epoch_id, "standing epoch_id");
            if (is_null) return 20;
            semantic_read_digest_datum(SPI_getbinval(tuple, tuple_desc, 15, &is_null), &candidate->standing.rating_recipe_id, "standing rating_recipe_id");
            if (is_null) return 21;
            value = SPI_getbinval(tuple, tuple_desc, 16, &is_null); if (is_null) return 22; candidate->standing.rating = DatumGetFloat8(value);
            value = SPI_getbinval(tuple, tuple_desc, 17, &is_null); if (is_null) return 23; candidate->standing.rating_deviation = DatumGetFloat8(value);
            value = SPI_getbinval(tuple, tuple_desc, 18, &is_null); if (is_null) return 24; candidate->standing.volatility = DatumGetFloat8(value);
            value = SPI_getbinval(tuple, tuple_desc, 19, &is_null); if (is_null) return 25; candidate->standing.eligible_match_count = laplace_pg_uint64_from_numeric(value, "standing eligible_match_count");
            value = SPI_getbinval(tuple, tuple_desc, 20, &is_null); if (is_null) return 26; candidate->standing.period_ordinal = laplace_pg_uint64_from_numeric(value, "standing period_ordinal");
            value = SPI_getbinval(tuple, tuple_desc, 21, &is_null); if (is_null) return 27; candidate->standing.rating_recipe_version = (uint32_t)DatumGetInt32(value);
            value = SPI_getbinval(tuple, tuple_desc, 22, &is_null); if (is_null) return 28; candidate->standing.flags = (uint32_t)DatumGetInt32(value);
            candidate->flags |= LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT;
        } else {
            return 29;
        }
    }
'''
text = text[:loop_start] + new_loop + text[loop_end:]

one('''    state->boundary_id = request->evidence_boundary;
    state->evidence_epoch = request->evidence_epoch;''',
'''    state->boundary_id = request->evidence_boundary;
    state->evidence_epoch = request->evidence_epoch;
    state->authority_id = request->authority_id;''')
one('''    semantic_provider_identify(
        &state->boundary_id,
        &state->evidence_epoch,
        &state->provider_fingerprint);''',
'''    semantic_provider_identify(
        &state->boundary_id,
        &state->evidence_epoch,
        &state->authority_id,
        &state->provider_fingerprint);''')

p.write_text(text)
Path('tools/apply_semantic_standing_provider.py').unlink(missing_ok=True)
Path('.github/workflows/apply-semantic-standing-provider.yml').unlink(missing_ok=True)
