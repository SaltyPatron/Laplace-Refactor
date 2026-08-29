extern "C" laplace_cognition_operator_status laplace_cognition_operator_create(
    const laplace_cognition_operator_program* const program,
    const laplace_cognition_operator_field* const fields,
    const size_t field_count,
    const laplace_cognition_operator_constraint* const constraints,
    const size_t constraint_count,
    laplace_cognition_operator** const operator_value,
    laplace_cognition_operator_receipt* const receipt) {
    if (operator_value != nullptr) *operator_value = nullptr;
    if (receipt != nullptr) {
        *receipt = laplace_cognition_operator_receipt{};
        receipt->version = LAPLACE_COGNITION_OPERATOR_VERSION;
    }
    if (program == nullptr || fields == nullptr || field_count == 0U ||
        constraints == nullptr || constraint_count == 0U ||
        operator_value == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT;
    }
    if (!ProgramValid(*program)) {
        receipt->status = LAPLACE_COGNITION_OPERATOR_PROGRAM_INVALID;
        return LAPLACE_COGNITION_OPERATOR_PROGRAM_INVALID;
    }
    try {
        std::unique_ptr<laplace_cognition_operator> value(
            new laplace_cognition_operator{});
        value->program = *program;
        value->eligible_families.assign(
            program->eligible_relation_families,
            program->eligible_relation_families + program->eligible_relation_family_count);
        std::sort(value->eligible_families.begin(), value->eligible_families.end());
        value->program.eligible_relation_families = value->eligible_families.data();
        value->fields.assign(fields, fields + field_count);

        std::set<DigestKey> field_ids;
        for (const auto& field : value->fields) {
            if (!FieldValid(field)) {
                receipt->status = LAPLACE_COGNITION_OPERATOR_FIELD_INVALID;
                return LAPLACE_COGNITION_OPERATOR_FIELD_INVALID;
            }
            if (!field_ids.insert(Key(field.field_id)).second) {
                receipt->status = LAPLACE_COGNITION_OPERATOR_FIELD_INVALID;
                return LAPLACE_COGNITION_OPERATOR_FIELD_INVALID;
            }
        }

        std::set<DigestKey> constraint_ids;
        std::map<EvidenceKey, laplace_cognition_operator_constraint> dependent_roots;
        std::vector<laplace_cognition_operator_constraint> selected;
        selected.reserve(constraint_count);
        std::set<DigestKey> planes;
        for (std::size_t index = 0U; index < constraint_count; ++index) {
            const auto& constraint = constraints[index];
            if (!ConstraintValid(constraint, field_count)) {
                receipt->status = LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID;
                return LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID;
            }
            if (!constraint_ids.insert(Key(constraint.constraint_id)).second) {
                receipt->status = LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID;
                return LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID;
            }
            if (!ConstraintEligible(*program, constraint)) {
                continue;
            }
            bool keep = true;
            if (constraint.source_class !=
                LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY) {
                const auto evidence_key = MakeEvidenceKey(constraint);
                const auto found = dependent_roots.find(evidence_key);
                if (found == dependent_roots.end()) {
                    dependent_roots.emplace(evidence_key, constraint);
                } else if (SameEvidenceSemantic(found->second, constraint)) {
#if defined(LAPLACE_TEST_COGNITION_OPERATOR_AMPLIFY_DEPENDENT_EVIDENCE)
                    keep = true;
#else
                    keep = false;
                    ++value->receipt.deduplicated_dependent_count;
#endif
                } else {
                    receipt->status = LAPLACE_COGNITION_OPERATOR_EVIDENCE_CONFLICT;
                    return LAPLACE_COGNITION_OPERATOR_EVIDENCE_CONFLICT;
                }
            }
            if (!keep) continue;
            selected.push_back(constraint);
            planes.insert(Key(constraint.plane_id));
            switch (constraint.source_class) {
                case LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY:
                    ++value->receipt.physicality_constraint_count;
                    break;
                case LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY:
                    ++value->receipt.testimony_constraint_count;
                    break;
                case LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED:
                    ++value->receipt.derived_constraint_count;
                    break;
                default:
                    break;
            }
        }
        if (selected.empty()) {
            receipt->status = LAPLACE_COGNITION_OPERATOR_EMPTY;
            return LAPLACE_COGNITION_OPERATOR_EMPTY;
        }
        std::sort(selected.begin(), selected.end(), ConstraintLess);
        value->constraints = std::move(selected);
        HashProgram(value->program, &value->receipt.program_fingerprint);
        value->receipt.field_count = field_count;
        value->receipt.input_constraint_count = constraint_count;
        value->receipt.selected_constraint_count = value->constraints.size();
        value->receipt.relation_plane_count = static_cast<std::uint32_t>(planes.size());
        value->receipt.version = LAPLACE_COGNITION_OPERATOR_VERSION;
        value->receipt.status = LAPLACE_COGNITION_OPERATOR_OK;
        HashOperatorSets(value.get());
        *receipt = value->receipt;
        *operator_value = value.release();
        return LAPLACE_COGNITION_OPERATOR_OK;
    } catch (const std::bad_alloc&) {
        receipt->status = LAPLACE_COGNITION_OPERATOR_MEMORY_FAILURE;
        return LAPLACE_COGNITION_OPERATOR_MEMORY_FAILURE;
    }
}

extern "C" void laplace_cognition_operator_destroy(
    laplace_cognition_operator** const operator_value) {
    if (operator_value != nullptr) {
        delete *operator_value;
        *operator_value = nullptr;
    }
}

extern "C" size_t laplace_cognition_operator_field_count(
    const laplace_cognition_operator* const operator_value) {
    return operator_value == nullptr ? 0U : operator_value->fields.size();
}

extern "C" size_t laplace_cognition_operator_constraint_count(
    const laplace_cognition_operator* const operator_value) {
    return operator_value == nullptr ? 0U : operator_value->constraints.size();
}

extern "C" laplace_cognition_operator_status laplace_cognition_operator_energy(
    const laplace_cognition_operator* const operator_value,
    const double* const input,
    const size_t input_count,
    double* const energy) {
    if (energy != nullptr) *energy = 0.0;
    if (operator_value == nullptr || input == nullptr || energy == nullptr) {
        return LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT;
    }
    if (input_count != operator_value->fields.size()) {
        return LAPLACE_COGNITION_OPERATOR_RANGE;
    }
    for (std::size_t index = 0U; index < input_count; ++index) {
        if (!std::isfinite(input[index])) {
            return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        }
    }
    std::vector<double> scratch(input_count, 0.0);
    return ApplyInternal(*operator_value, input, scratch.data(), energy)
        ? LAPLACE_COGNITION_OPERATOR_OK
        : LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
}

extern "C" laplace_cognition_operator_status laplace_cognition_operator_apply(
    const laplace_cognition_operator* const operator_value,
    const double* const input,
    const size_t input_count,
    double* const output,
    const size_t output_count,
    laplace_cognition_operator_application_receipt* const receipt) {
    if (receipt != nullptr) *receipt = laplace_cognition_operator_application_receipt{};
    if (operator_value == nullptr || input == nullptr || output == nullptr ||
        receipt == nullptr) {
        return LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT;
    }
    if (input_count != operator_value->fields.size() || output_count != input_count) {
        receipt->status = LAPLACE_COGNITION_OPERATOR_RANGE;
        return LAPLACE_COGNITION_OPERATOR_RANGE;
    }
    for (std::size_t index = 0U; index < input_count; ++index) {
        if (!std::isfinite(input[index])) {
            receipt->status = LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
            return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        }
    }
    double energy = 0.0;
    if (!ApplyInternal(*operator_value, input, output, &energy)) {
        receipt->status = LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
    }
    blake3_hasher input_hasher{};
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_COGNITION_OPERATOR_APPLICATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_APPLICATION_DOMAIN) - 1U);
    HashDigest(&input_hasher, operator_value->receipt.operator_id);
    for (std::size_t index = 0U; index < input_count; ++index) {
        HashF64(&input_hasher, input[index]);
    }
    Finish(&input_hasher, &receipt->input_fingerprint);

    blake3_hasher output_hasher{};
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_COGNITION_OPERATOR_APPLICATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_APPLICATION_DOMAIN) - 1U);
    HashDigest(&output_hasher, operator_value->receipt.operator_id);
    for (std::size_t index = 0U; index < output_count; ++index) {
        HashF64(&output_hasher, output[index]);
    }
    HashF64(&output_hasher, energy);
    Finish(&output_hasher, &receipt->output_fingerprint);
    receipt->operator_id = operator_value->receipt.operator_id;
    receipt->field_count = input_count;
    receipt->constraint_count = operator_value->constraints.size();
    receipt->energy = energy;
    receipt->status = LAPLACE_COGNITION_OPERATOR_OK;

    blake3_hasher receipt_hasher{};
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_COGNITION_OPERATOR_APPLICATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_APPLICATION_DOMAIN) - 1U);
    HashDigest(&receipt_hasher, receipt->operator_id);
    HashDigest(&receipt_hasher, receipt->input_fingerprint);
    HashDigest(&receipt_hasher, receipt->output_fingerprint);
    HashU64(&receipt_hasher, receipt->field_count);
    HashU64(&receipt_hasher, receipt->constraint_count);
    HashF64(&receipt_hasher, receipt->energy);
    Finish(&receipt_hasher, &receipt->receipt_id);
    return LAPLACE_COGNITION_OPERATOR_OK;
}
