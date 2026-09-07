import json, os, runpy, subprocess, uuid
from pathlib import Path

r = runpy.run_path('tools/delivery/product_activation_runner.py')
c = r['clusterctl']; u = r['unicodectl']
c.require_fixture_or_root(Path('/'), False)
contract = c.load_json(Path('contracts/postgresql-cluster.json'))
activation = u.load_json(Path('contracts/unicode-product-activation.json'))
active_id = Path(contract['package']['active_link']).resolve().name
expected = 'cd6290e2bdbea789a211c2965f0e17001d39a4f61ab7e5404359513a6409eef4'
if active_id != expected:
    raise RuntimeError('Installed package changed; diagnosis must be reselected')
receipts = Path(contract['instance']['receipt_directory'])
receipt = c.load_json(receipts/'cluster-activation'/active_id/'activation-result.json')
plan = c.load_json(Path(receipt['cluster_plan_path']))
loaded = c.observe_loaded_live(plan, contract, Path('/'))
c.verify_loaded(plan, contract, loaded)
requests=[]
for p in (receipts/'unicode').glob('*/request.json'):
    q=u.load_json(p)
    if q.get('package_id') == active_id and q.get('orchestrator_sha256') == u.sha256_file(Path('tools/postgresql/unicodectl.py')):
        requests.append((p,q))
if len(requests)!=1:
    raise RuntimeError(f'Expected one exact failed request, found {len(requests)}')
request_path, request = requests[0]
identity = u.load_json(request_path.with_name('identities.json'))
u.validate_identities(identity, activation)
instance=contract['instance']
base=Path(os.environ['RUNNER_TEMP'])/'unicode-native-diagnosis'
base.mkdir(exist_ok=True)
name='unicode-contract-check-'+uuid.uuid4().hex
spool=Path(instance['temp_directory'])/name
generation=Path(instance['perfcache_directory'])/name
r['runner_work_directories']((spool,generation),contract,Path('/'))
sql=u.render_activation_sql(activation, identity, request['source_root'],str(spool),str(generation/'unicode-tier0.lpc'),str(generation/'unicode-identity-reverse.lpc'))
old="RAISE EXCEPTION 'Unicode product activation result violates its exact contract';"
new="""RAISE EXCEPTION 'Unicode product activation result violates its exact contract'
 USING DETAIL = json_build_object('build',to_jsonb(build),'active',to_jsonb(active),
 'durable_counts',json_build_object(
 'entity',(SELECT count(*) FROM laplace.entity),
 'physicality',(SELECT count(*) FROM laplace.physicality),
 'atom',(SELECT count(*) FROM laplace.attestation WHERE source_fingerprint=build.root_receipt AND attestation_kind=3),
 'ducet_position',(SELECT count(*) FROM laplace.unicode_ducet_position),
 'ducet_contraction',(SELECT count(*) FROM laplace.unicode_ducet_contraction),
 'normalization',(SELECT count(*) FROM laplace.unicode_normalization_composition),
 'generation_match',(SELECT count(*) FROM laplace.unicode_root_generation g WHERE g.root_receipt=build.root_receipt AND g.plan_manifest_fingerprint=build.plan_manifest_fingerprint AND g.postgresql_artifact_fingerprint=build.postgresql_artifact_fingerprint),
 'deposit_match',(SELECT count(*) FROM laplace.unicode_root_deposit_receipt d WHERE d.root_receipt=build.root_receipt AND d.producer_receipt=build.producer_receipt AND d.staged_stream_receipt=build.staged_stream_receipt AND d.admission_receipt=build.admission_receipt)))::text;"""
if sql.count(old)!=1 or not sql.endswith('COMMIT;\n'):
    raise RuntimeError('The selected SQL program changed')
sql=sql.replace(old,new).removesuffix('COMMIT;\n')+'ROLLBACK;\n'
(base/'program.sql').write_text(sql)
command=[f"{plan['package_root']}/pgsql-18/bin/psql",'-X','-w','-A','-t','-v','ON_ERROR_STOP=1','-h',instance['socket_directory'],'-p',str(instance['port']),'-U',instance['admin_role'],'-d',instance['database']]
result=subprocess.run(command,input=sql,text=True,capture_output=True,env=c.activation_environment(),cwd='/',timeout=1200)
(base/'stdout.txt').write_text(result.stdout);(base/'stderr.txt').write_text(result.stderr)
print(result.stderr)
inspection, _=r['runner_sql'](plan,contract,u.render_inspection_sql(),'verify-diagnostic-rollback',instance['os_user'],instance['admin_role'],120)
if u.validate_inspection(inspection,activation,identity) != 'fresh':
    raise RuntimeError('Diagnostic rollback did not retain empty product state')
(base/'result.json').write_text(json.dumps({'native_exit':result.returncode,'installed_package_id':active_id,'system_identifier':loaded['system_identifier'],'committed':False,'rollback_verified':True,'inspection':inspection,'spool_directory':str(spool),'generation_directory':str(generation)},indent=2))
print(json.dumps({'native_exit':result.returncode,'rollback_verified':True}))
