\if :{?source_skip_unicode}
\else
\ir unicode_root_contract.sql
\endif

\set source_skip_unicode 1
BEGIN;
\ir source_admission_contract.sql
\ir source_admission_active_control_mutation.sql
ROLLBACK;
BEGIN;
\ir iso_639_source_admission_contract.sql
ROLLBACK;
BEGIN;
\ir cili_source_admission_contract.sql
\ir cili_admission_measurement_receipt.sql
ROLLBACK;
