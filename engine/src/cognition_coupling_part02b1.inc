    blake3_hasher hasher{};
    Start(&hasher, ResponseSetDomain);
    for (const auto& response : owned->responses) {
        HashDigest(&hasher, response.response_fingerprint);
    }
    Finish(&hasher, &receipt->response_set_fingerprint);

    Start(&hasher, ReceiptDomain);
    HashDigest(&hasher, receipt->provider_fingerprint);
    HashDigest(&hasher, receipt->seed_set_fingerprint);
    HashDigest(&hasher, receipt->response_set_fingerprint);
    HashDigest(&hasher, request->world_id);
    HashDigest(&hasher, request->time_fingerprint);
    HashDigest(&hasher, request->context_fingerprint);
    HashDigest(&hasher, request->evidence_boundary);
    HashDigest(&hasher, request->evidence_epoch);
    HashDigest(&hasher, request->authority_id);
    HashU64(&hasher, receipt->rows_examined);
    HashU64(&hasher, receipt->index_plan_count);
    HashU64(&hasher, receipt->crossing_count);
    HashU64(&hasher, receipt->io_operations);
    HashU64(&hasher, receipt->database_operations);
    HashU64(&hasher, receipt->provider_call_count);
    HashU64(&hasher, receipt->generated_frontier_state_count);
    HashU64(&hasher, receipt->witness_count);
    HashU64(&hasher, receipt->response_count);
    HashU32(&hasher, receipt->disposition);
    HashU32(&hasher, receipt->version);
    Finish(&hasher, &receipt->receipt_id);

    *result = owned.release();
    return LAPLACE_COGNITION_COUPLING_OK;
}
