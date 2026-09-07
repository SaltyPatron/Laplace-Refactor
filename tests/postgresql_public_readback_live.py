#!/usr/bin/env python3
"""Rollback-only proof against the selected persistent PostgreSQL instance.

Requires the runner-owned installed product. No fixture or skip fallback exists.
Function metadata is modified only in transactions that end in ROLLBACK; source
admission, artifacts, table privileges and database contents are never changed.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('live_readback_runner',ROOT/'tools/delivery/product_activation_runner.py')
r=importlib.util.module_from_spec(spec);sys.modules[spec.name]=r;spec.loader.exec_module(r)
u=r.unicodectl


def prove(bindings: Path, output: Path) -> None:
    r.require_runner()
    cluster=u.load_json(ROOT/'contracts/postgresql-cluster.json')
    contract=u.load_json(ROOT/'contracts/unicode-product-activation.json')
    source=u.load_json(ROOT/'contracts/unicode-source.json')
    pg=u.load_json(ROOT/'contracts/unicode-postgresql.json')
    active=Path(cluster['package']['active_link']).resolve(strict=True)
    package_id=active.name
    if u.HEX_256.fullmatch(package_id) is None:raise RuntimeError('invalid active package')
    receipt_root=Path(cluster['instance']['receipt_directory'])
    completed=u.load_json(receipt_root/'cluster-activation'/package_id/'activation-complete.json')
    plan=u.load_json(Path(completed['cluster_plan_path']))
    r.clusterctl.validate_plan(plan,cluster)
    before=r.clusterctl.observe_loaded_live(plan,cluster,Path('/'))
    r.clusterctl.verify_loaded(plan,cluster,before)
    instance=cluster['instance']
    def query(sql,label):
        return r.runner_sql(plan,cluster,sql,label,'laplace-runner',instance['admin_role'],300)
    inspection,_=query(u.render_inspection_sql(),'inspect-before-readback-proof')
    source_root=Path('/vault/Data/UCD/Public/UCD/latest')
    source_evidence=u.verify_source_bundle(source,source_root)
    request={'schema':u.REQUEST_SCHEMA,'activation_contract_sha256':u.sha256_bytes(u.canonical_bytes(contract)),
        'cluster_contract_sha256':u.sha256_bytes(u.canonical_bytes(cluster)),
        'cluster_system_identifier':before['system_identifier'],
        'source_contract_sha256':u.sha256_bytes(u.canonical_bytes(source)),
        'source_evidence_sha256':source_evidence['source_evidence_sha256'],'source_root':str(source_root),
        'unicode_postgresql_contract_sha256':u.sha256_bytes(u.canonical_bytes(pg)),
        'operation':contract['operation'],'execution_context':contract['execution_context'],
        'expected_result':contract['expected_result']}
    identities,original,identity_command=u.retained_root_identities(receipt_root,inspection,request,
        contract,active/'bin/laplace_unicode_activation_identify',u.run_identity_provider)
    u.validate_inspection(inspection,contract,identities)
    names=['unicode_tier0_resolve_batch','unicode_identity_reverse_resolve_batch','highway_registry_resolve_active']
    catalog="""SELECT pg_catalog.json_build_object('functions',(
        SELECT pg_catalog.json_agg(pg_catalog.json_build_object('oid',p.oid,'owner',p.proowner,
            'acl',p.proacl,'definer',p.prosecdef,'config',p.proconfig) ORDER BY p.oid)
        FROM pg_catalog.pg_proc p JOIN pg_catalog.pg_namespace n ON n.oid=p.pronamespace
        WHERE n.nspname='laplace' AND p.proname IN ('unicode_tier0_resolve_batch',
            'unicode_identity_reverse_resolve_batch','highway_registry_resolve_active')))::text;"""
    metadata_before,_=query(catalog,'readback-metadata-before')
    program=bindings.read_text()
    readback=u.render_readback_sql(contract,identities)
    checks="""DO $proof$
    DECLARE accepted boolean := false;
    BEGIN
      IF current_user <> 'laplace_app' THEN RAISE EXCEPTION 'caller authority was not restored'; END IF;
      IF pg_catalog.has_table_privilege(current_user,'laplace.perfcache_active_control','SELECT,INSERT,UPDATE,DELETE')
         OR pg_catalog.has_table_privilege(current_user,'laplace.perfcache_generation','SELECT,INSERT,UPDATE,DELETE')
         OR pg_catalog.has_table_privilege(current_user,'laplace.highway_registry_active_control','SELECT,INSERT,UPDATE,DELETE')
      THEN RAISE EXCEPTION 'application received private table privileges'; END IF;
      BEGIN
        PERFORM * FROM laplace.perfcache_active_control;
        accepted := true;
      EXCEPTION WHEN insufficient_privilege THEN NULL;
      END;
      IF accepted THEN RAISE EXCEPTION 'direct private table access unexpectedly succeeded'; END IF;
      accepted := false;
      BEGIN
        PERFORM laplace.unicode_tier0_resolve_batch(decode(repeat('00',16),'hex'),decode(repeat('00',32),'hex'),ARRAY[65]);
        accepted := true;
      EXCEPTION WHEN object_not_in_prerequisite_state THEN NULL;
      END;
      IF accepted THEN RAISE EXCEPTION 'invalid epoch unexpectedly succeeded'; END IF;
    END $proof$;
    """
    # Reapplying is intentional: it proves idempotent metadata reconciliation.
    sql="BEGIN; SET LOCAL statement_timeout='180s'; SET LOCAL lock_timeout='30s';\n"+program+program
    sql+="\nSET LOCAL ROLE laplace_app; SET LOCAL search_path TO pg_temp, public;\n"+readback+checks+"\nROLLBACK;\n"
    result,command=query(sql,'cold-app-readback-rollback-proof')
    u.validate_readback(result,contract,identities)
    # Deliberately restore INVOKER: the same positive read must expose the original
    # defect rather than being masked by an administrator connection or warm pin.
    mutant="BEGIN;\n"+program.replace('SECURITY DEFINER','SECURITY INVOKER')
    mutant+="\nSET LOCAL ROLE laplace_app;\n"+readback+"\nROLLBACK;\n"
    try:
        query(mutant,'readback-invoker-negative-control')
    except r.RunnerActivationError as error:
        if 'permission denied for table perfcache_active_control' not in str(error):raise
        mutation=str(error)
    else:raise RuntimeError('INVOKER defect was not detected')
    metadata_after,_=query(catalog,'readback-metadata-after')
    after,_=query(u.render_inspection_sql(),'inspect-after-readback-proof')
    loaded_after=r.clusterctl.observe_loaded_live(plan,cluster,Path('/'))
    if (metadata_before!=metadata_after or inspection!=after
            or before['system_identifier']!=loaded_after['system_identifier']
            or before['postmaster_pid']!=loaded_after['postmaster_pid']):
        raise RuntimeError('rollback readback proof changed persistent state')
    proof={'schema':'laplace.public-readback-live-proof/v1','package_id':package_id,
        'system_identifier':before['system_identifier'],'postmaster_pid':before['postmaster_pid'],
        'activation_epoch_id':identities['activation_epoch_id'],
        'readback':result,'metadata_rollback_verified':True,'catalog_unchanged':True,
        'caller_role':'laplace_app','private_table_access_rejected':True,
        'invalid_epoch_rejected':True,'invoker_mutation_detected':mutation,
        'script_sha256':u.sha256_file(bindings),'command_receipt':command,'identity_command':identity_command}
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,sort_keys=True))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bindings',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();prove(args.bindings,args.output)
