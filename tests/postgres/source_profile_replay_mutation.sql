\ir spi_contract.sql

CREATE FUNCTION pg_temp.source_profile_replay_mutant(
    laplace.execution_context,
    laplace.source_profile_manifest[])
RETURNS laplace.source_profile_result
AS :'persistence_mutant_module',
   'laplace_pg_source_profile_validate_replay_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    profiles laplace.source_profile_manifest[];
    result laplace.source_profile_result;
BEGIN
    SELECT array_agg(ROW(profile.*)::laplace.source_profile_manifest
                     ORDER BY profile.profile_id)
    INTO STRICT profiles
    FROM laplace.source_profile profile;

    result := pg_temp.source_profile_replay_mutant(
        pg_temp.persistence_context(), profiles);
    UPDATE laplace.source_profile
    SET license_fingerprint = decode(repeat('ff', 32), 'hex')
    WHERE profile_id = result.profile_ids[1];
    PERFORM pg_temp.source_profile_replay_mutant(
        pg_temp.persistence_context(), profiles);
    RAISE EXCEPTION
        'source-profile replay mutant accepted conflicting durable manifest for receipt %',
        encode(result.source_profile_receipt_id, 'hex');
END
$mutation$;
