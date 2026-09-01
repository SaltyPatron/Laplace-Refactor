DO $contract$
DECLARE
    pinned_context laplace.execution_context;
BEGIN
    pinned_context := pg_temp.source_admission_context();
    BEGIN
        UPDATE laplace.perfcache_active_control
        SET active_present = false
        WHERE singleton;
        PERFORM pg_temp.admit_source(
            pg_temp.source_artifacts(), pinned_context);
        RAISE EXCEPTION 'absent active Unicode root was accepted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
    IF NOT EXISTS (
        SELECT 1 FROM laplace.perfcache_active_control
        WHERE singleton AND active_present) THEN
        RAISE EXCEPTION 'Unicode mutation subtransaction did not roll back';
    END IF;
END
$contract$;
