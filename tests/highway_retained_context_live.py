#!/usr/bin/env python3
"""Execute exact Highway replay twice on the selected product, then roll back.

The deliberate active-to-active mutation must reproduce the original native
request failure. No fixture, skip, data reset or silent fallback is permitted.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('highway_replay_live_runner', ROOT/'tools/delivery/product_activation_runner.py')
r = importlib.util.module_from_spec(spec); sys.modules[spec.name]=r; spec.loader.exec_module(r)
h = r.highwayctl
u = r.unicodectl


def prove(output: Path) -> None:
    r.require_runner()
    cluster=u.load_json(ROOT/'contracts/postgresql-cluster.json')
    contract=u.load_json(ROOT/'contracts/highway-product-activation.json')
    unicode_contract=u.load_json(ROOT/'contracts/unicode-product-activation.json')
    registry=u.load_json(ROOT/'contracts/highway.json')
    predecessor=u.load_json(ROOT/'contracts/history/highway-v1.json')
    h.validate_contract(contract,cluster,unicode_contract,registry,predecessor)
    package=Path(cluster['package']['active_link']).resolve(strict=True).name
    if u.HEX_256.fullmatch(package) is None:raise RuntimeError('invalid selected package')
    receipts=Path(cluster['instance']['receipt_directory'])
    completed=u.load_json(receipts/'cluster-activation'/package/'activation-complete.json')
    plan=u.load_json(Path(completed['cluster_plan_path']))
    r.clusterctl.validate_plan(plan,cluster)
    loaded=r.clusterctl.observe_loaded_live(plan,cluster,Path('/'))
    r.clusterctl.verify_loaded(plan,cluster,loaded)
    unicode_receipt=u.load_json(receipts/'unicode-product-activation.json')
    identities=h.load_unicode_identities(cluster,unicode_contract,unicode_receipt,Path('/'))
    h.validate_unicode_receipt(unicode_receipt,{'package_id':package},identities)
    def query(sql,label,role='laplace_admin'):
        return r.runner_sql(plan,cluster,sql,label,'laplace-runner',role,300)
    before,_=query(h.render_inspection_sql(),'inspect-highway-before-replay-proof')
    state=h.validate_inspection(before,unicode_receipt,contract)
    if state['mode']!='replay':raise RuntimeError('proof requires an already-active registry')
    request={'schema':'laplace.highway-product-activation-request/v1',
        'activation_contract_sha256':u.sha256_bytes(u.canonical_bytes(contract)),
        'registry_contract_sha256':u.sha256_bytes(u.canonical_bytes(registry)),
        'predecessor_registry_contract_sha256':u.sha256_bytes(u.canonical_bytes(predecessor)),
        'unicode_request_fingerprint':unicode_receipt['request_fingerprint'],
        'operation':contract['operation'],'execution_context':contract['execution_context'],
        'expected_result':contract['expected_result'],'inspected_state':state,
        'system_identifier':loaded['system_identifier']}
    replay,admission=h.retained_activation_state(Path('/'),cluster,before,request)
    results=[];commands=[]
    for index in range(2):
        sql=h.render_activation_sql(contract,identities,unicode_receipt,replay)
        if sql.count('COMMIT;')!=1:raise RuntimeError('unexpected transaction shape')
        activation,command=query(sql.replace('COMMIT;','ROLLBACK;'),f'exact-highway-replay-{index}')
        h.validate_activation_result(activation,contract,unicode_receipt,state)
        results.append(activation);commands.append(command)
    if results[0]!=results[1]:raise RuntimeError('exact repeated native replay was not deterministic')
    readback,command=query(h.render_readback_sql(contract,identities,unicode_receipt,results[0]),
                          'cold-highway-readback-after-replay','laplace_app')
    h.validate_readback(readback,contract,unicode_receipt,results[0]);commands.append(command)
    mutant=h.render_activation_sql(contract,identities,unicode_receipt,state).replace('COMMIT;','ROLLBACK;')
    try:
        query(mutant,'active-to-active-highway-negative-control')
    except r.RunnerActivationError as error:
        detail=str(error)
        if 'numeric epoch activation failed' not in detail or 'status=12' not in detail:raise
    else:raise RuntimeError('active-to-active defect was not detected')
    after,_=query(h.render_inspection_sql(),'inspect-highway-after-replay-proof')
    loaded_after=r.clusterctl.observe_loaded_live(plan,cluster,Path('/'))
    if (after!=before or loaded_after['system_identifier']!=loaded['system_identifier']
            or loaded_after['postmaster_pid']!=loaded['postmaster_pid']):
        raise RuntimeError('rollback proof changed selected registry or database identity')
    result={'schema':'laplace.highway-retained-context-live-proof/v1','package_id':package,
        'system_identifier':loaded['system_identifier'],'postmaster_pid':loaded['postmaster_pid'],
        'admission_request_sha256':admission,'original_numeric_epoch':replay['numeric_epoch'],
        'active_numeric_epoch':state['numeric_epoch'],'exact_native_replays':2,
        'activation':results[0],'application_readback':readback,'catalog_unchanged':True,
        'active_to_active_mutation_detected':detail,'command_receipts':commands}
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    prove(parser.parse_args().output)
