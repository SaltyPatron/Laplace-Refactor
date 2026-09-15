#!/usr/bin/env python3
"""Deliberate counterexamples for installed full-source acceptance orchestration."""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
SCRATCH=Path(os.environ.get('TMPDIR',str(ROOT/'out/acceptance-test-scratch')))
SCRATCH.mkdir(parents=True,exist_ok=True)
SPEC=importlib.util.spec_from_file_location('stockfish_corpus_acceptance',ROOT/'tools/sources/stockfish_corpus_acceptance.py')
subject=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(subject)


def fixtures():
    package='a'*64
    manifest={'schema':'laplace.verified-git-corpus/v1','file_count':2,'byte_count':5,
        'artifacts':[{'path':'src/α.cpp','sha256':'b'*64,'byte_count':3},
                     {'path':'COPYING','sha256':'c'*64,'byte_count':2}]}
    active={'package_id':package,'release':'/opt/laplace/releases/'+package}
    admission={key:'\\x'+'d'*64 for key in ('profile_id','source_fingerprint',
        'reconstruction_fingerprint','source_profile_receipt_id','root_physicality_id')}
    admission.update({'root_entity_id':'\\x'+'e'*32,'testimony_count':0,'evidence_node_count':0})
    records=[]
    for index,item in enumerate(manifest['artifacts']):
        records.append({'path':item['path'],'sha256':item['sha256'],'output_bytes':item['byte_count'],
            'artifact_index':index,'source_profile_id':admission['profile_id'],
            'root_content_id':'\\x'+str(index+1)*32,
            **{field:'\\x'+str(index+1)*64 for field in ('source_binding_id','materialization_receipt_id',
                'provider_fingerprint','readset_fingerprint','output_fingerprint')},
            'recipe_id':'\\x'+'5'*64,'structural_receipt_id':'\\x'+'6'*64,
            'resolved_nodes':3,'trajectory_carriers':1,'codepoint_count':item['byte_count'],
            'maximum_depth':2,'verified_witnesses':13,'version':1,'database_operations':5})
    result={'schema':'laplace.admit-source/v1','phase':'source-admitted-and-exactly-read-back',
        'profile':'verified-git-code','active_package_id':package,'tool_release':active['release'],
        'executable_semantics_verified':False,'verified_git_input':{'manifest':manifest},
        'admission':admission,'persisted_profile':{'claim_count':0},
        **{field:'f'*64 for field in ('geometry_epoch','perfcache_epoch','numeric_epoch',
            'native_source_fingerprint','native_reconstruction_fingerprint')},
        'readback':{'schema':'laplace.verified-git-source-readback/v1',
            'structural_witness_fingerprint':'7'*64,
            'all_artifacts_exact':True,'structural_receipt_count':1,'verified_file_count':2,
            'verified_byte_count':5,'records':records,'source_occurrence_count':9,
            'structural_witness_count':13,'database_row_counts':{'entity':100,'physicality':100,'attestation':9}}}
    return manifest,active,result


class ReadbackTests(unittest.TestCase):
    def setUp(self): self.manifest,self.active,self.first=fixtures()

    def check_bad(self, mutate, pattern):
        second=copy.deepcopy(self.first)
        mutate(second)
        with self.assertRaisesRegex(ValueError,pattern):
            subject.verify_repeat(self.first,second,self.manifest,self.active)

    def test_complete_repeat_accepts_actual_denominators(self):
        result=subject.verify_repeat(self.first,copy.deepcopy(self.first),self.manifest,self.active)
        self.assertEqual(result['verified_file_count'],2)
        self.assertEqual(result['verified_byte_count'],5)
        self.assertEqual(result['substrate_row_deltas'],{'entity':0,'physicality':0,'attestation':0})

    def test_physical_readback_work_can_change_without_canonical_change(self):
        second=copy.deepcopy(self.first)
        second['readback']['records'][0].update(database_operations=1,materialization_receipt_id='\\x'+'8'*64)
        subject.verify_repeat(self.first,second,self.manifest,self.active)

    def test_canonical_reuse_accepts_different_structural_execution_receipts_and_bindings(self):
        second=copy.deepcopy(self.first)
        for index,row in enumerate(second['readback']['records']):
            row['structural_receipt_id']='\\x'+'8'*64
            row['source_binding_id']='\\x'+str(index+3)*64
        original_first,original_second=copy.deepcopy(self.first),copy.deepcopy(second)
        result=subject.verify_repeat(self.first,second,self.manifest,self.active)
        self.assertEqual(result['substrate_row_deltas'],{'entity':0,'physicality':0,'attestation':0})
        self.assertEqual(self.first,original_first)
        self.assertEqual(second,original_second)
        for first_row,second_row in zip(self.first['readback']['records'],second['readback']['records']):
            self.assertEqual(len(first_row),20)
            self.assertEqual(len(second_row),20)
            self.assertNotEqual(first_row['structural_receipt_id'],second_row['structural_receipt_id'])
            self.assertNotEqual(first_row['source_binding_id'],second_row['source_binding_id'])

    def test_each_result_requires_a_lowercase_semantic_witness_fingerprint(self):
        for which in (0,1):
            first,second=copy.deepcopy(self.first),copy.deepcopy(self.first)
            (first,second)[which]['readback'].pop('structural_witness_fingerprint')
            with self.subTest(result=which,missing=True), self.assertRaisesRegex(ValueError,'structural witness fingerprint'):
                subject.verify_repeat(first,second,self.manifest,self.active)
            for value in (None,False,17,'','a'*63,'a'*65,'AB'*32,'\\x'+'ab'*32):
                first,second=copy.deepcopy(self.first),copy.deepcopy(self.first)
                (first,second)[which]['readback']['structural_witness_fingerprint']=value
                with self.subTest(result=which,value=value), self.assertRaisesRegex(ValueError,'structural witness fingerprint'):
                    subject.verify_repeat(first,second,self.manifest,self.active)

    def test_repeat_rejects_changed_semantic_witness_with_unchanged_or_new_execution_receipts(self):
        for changed_execution in (False,True):
            second=copy.deepcopy(self.first)
            second['readback']['structural_witness_fingerprint']='9'*64
            if changed_execution:
                for index,row in enumerate(second['readback']['records']):
                    row['structural_receipt_id']='\\x'+'8'*64
                    row['source_binding_id']='\\x'+str(index+3)*64
            with self.subTest(changed_execution=changed_execution), self.assertRaisesRegex(ValueError,'semantic structural witness fingerprint'):
                subject.verify_repeat(self.first,second,self.manifest,self.active)

    def test_every_native_metadata_field_is_required(self):
        for field in subject.READBACK_FIELDS:
            with self.subTest(field=field):
                self.check_bad(lambda r:r['readback']['records'][0].pop(field),'complete result contract')

    def test_native_identity_width_and_nonzero_are_required(self):
        for field in ('root_content_id',*subject.READBACK_DIGEST_FIELDS):
            width=64 if field=='root_content_id' else 32
            for value in ('\\x'+'1'*width,'\\x'+'0'*(32 if field=='root_content_id' else 64)):
                with self.subTest(field=field,value=value):
                    self.check_bad(lambda r:r['readback']['records'][0].__setitem__(field,value),
                        'identity|bytes, path, order or profile')

    def test_native_metadata_counters_have_exact_types_and_bounds(self):
        for field in subject.READBACK_COUNT_FIELDS:
            for value in (False,-1,1.5):
                with self.subTest(field=field,value=value):
                    self.check_bad(lambda r:r['readback']['records'][0].__setitem__(field,value),'invalid count')
        for field,value in (('version',2),('maximum_depth',257),('codepoint_count',4),('verified_witnesses',14)):
            with self.subTest(field=field):
                self.check_bad(lambda r:r['readback']['records'][0].__setitem__(field,value),'inconsistent|structural receipt')

    def test_rows_cannot_mix_structural_receipts_or_repeat_artifact_bindings(self):
        for field in ('structural_receipt_id','recipe_id'):
            with self.subTest(field=field):
                self.check_bad(lambda r:r['readback']['records'][1].__setitem__(field,'\\x'+'7'*64),'structural receipt')
        self.check_bad(lambda r:r['readback']['records'][1].__setitem__('source_binding_id',
            r['readback']['records'][0]['source_binding_id']),'artifact source binding')

    def test_repeated_bytes_require_same_native_output_identity(self):
        self.check_bad(lambda r:r['readback']['records'][0].__setitem__('output_fingerprint','\\x'+'7'*64),'source root')

    def test_missing_file_is_rejected(self):
        self.check_bad(lambda r:r['readback']['records'].pop(),'omitted or repeated')

    def test_duplicate_file_is_rejected(self):
        self.check_bad(lambda r:r['readback']['records'].__setitem__(1,r['readback']['records'][0]),'bytes, path, order')

    def test_changed_bytes_are_rejected(self):
        self.check_bad(lambda r:r['readback']['records'][0].__setitem__('sha256','0'*64),'bytes, path, order')

    def test_reordered_files_are_rejected(self):
        self.check_bad(lambda r:r['readback']['records'].reverse(),'bytes, path, order')

    def test_fractional_native_count_is_rejected(self):
        self.check_bad(lambda r:r['readback']['records'][0].__setitem__('output_bytes',3.25),'invalid count')

    def test_false_is_not_zero_count(self):
        self.check_bad(lambda r:r['readback']['database_row_counts'].__setitem__('entity',False),'invalid count')

    def test_missing_fresh_postcommit_counts_is_rejected(self):
        self.check_bad(lambda r:r['readback'].pop('database_row_counts'),'fresh postcommit')

    def test_each_global_row_amplification_is_rejected(self):
        for key in ('entity','physicality','attestation'):
            with self.subTest(key=key):
                self.check_bad(lambda r:r['readback']['database_row_counts'].__setitem__(key,999),'amplified persistent')

    def test_each_scoped_amplification_is_rejected(self):
        for key in ('source_occurrence_count','structural_witness_count'):
            with self.subTest(key=key):
                def amplify(result):
                    result['readback'][key]=999
                    if key=='structural_witness_count':
                        for row in result['readback']['records']:row['verified_witnesses']=999
                self.check_bad(amplify,'amplified persistent')

    def test_changed_canonical_file_root_is_rejected(self):
        self.check_bad(lambda r:r['readback']['records'][0].__setitem__('root_content_id','\\x'+'9'*32),'source root')

    def test_changed_product_package_is_rejected(self):
        self.check_bad(lambda r:r.__setitem__('active_package_id','9'*64),'current-package')

    def test_changed_live_epoch_is_rejected(self):
        self.check_bad(lambda r:r.__setitem__('numeric_epoch','9'*64),'context identity')

    def test_semantic_testimony_is_rejected(self):
        self.check_bad(lambda r:r['admission'].__setitem__('testimony_count',1),'invented semantic testimony')

    def test_executable_semantics_claim_is_rejected(self):
        self.check_bad(lambda r:r.__setitem__('executable_semantics_verified',True),'current-package')


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.commit='1'*40; self.package='2'*64
        self.manifest={'package_id':self.package,'root':'/opt/laplace/releases/'+self.package,
            'laplace':{'repository_commit':self.commit}}
        self.gateway={'product':{'package_release_root':'/opt/laplace/releases',
            'package_installation_schema':'laplace.product-package-installation-receipt/v1'}}
        self.installation={'schema':'laplace.product-package-installation-receipt/v1','phase':'installed',
            'package_id':self.package,'package_manifest_sha256':subject.runner.clusterctl.sha256_bytes(
                subject.runner.clusterctl.canonical_bytes(self.manifest)),
            'package_root':'/opt/laplace/releases/'+self.package,'installation_root':'/',
            'installed_release':'/opt/laplace/releases/'+self.package,
            'source_physical_root':'/build/laplace/runner/product/stage/'+'4'*64+'/root',
            'source_package_verified':True,'installed_package_verified':True,'overwrite_performed':False}
        self.installation['installation_receipt_sha256']=subject.runner.document_identity(self.installation,'installation_receipt_sha256')
        self.aggregate={'schema':subject.runner.RESULT_SCHEMA,'phase':'product-unicode-and-highway-activated',
            'package_id':self.package,'repository_commit':self.commit,
            'execution_owner':'laplace-runner','root_product_executor':False,'postgresql_lifecycle_provider':'pg_ctl',
            'package_installation_receipt_sha256':self.installation['installation_receipt_sha256']}
        self.sign()

    def sign(self): self.aggregate['result_sha256']=subject.runner.document_identity(self.aggregate,'result_sha256')

    def validate(self):
        subject.validate_package_binding(self.commit,self.package,self.manifest,self.installation,self.aggregate)

    def test_exact_source_package_activation_is_accepted(self): self.validate()

    def test_stale_manifest_even_with_current_activation_claim_is_rejected(self):
        self.manifest['laplace']['repository_commit']='9'*40
        with self.assertRaisesRegex(ValueError,'stale'): self.validate()

    def test_stale_activation_even_with_current_package_is_rejected(self):
        self.aggregate['repository_commit']='9'*40;self.sign()
        with self.assertRaisesRegex(ValueError,'stale'): self.validate()

    def test_wrong_package_is_rejected(self):
        self.aggregate['package_id']='9'*64;self.sign()
        with self.assertRaisesRegex(ValueError,'identities differ'): self.validate()

    def test_forged_terminal_receipt_digest_is_rejected(self):
        self.aggregate['result_sha256']='9'*64
        with self.assertRaisesRegex(ValueError,'receipt identity'): self.validate()

    def test_missing_successful_phase_is_rejected(self):
        self.aggregate['phase']='pending';self.sign()
        with self.assertRaisesRegex(subject.activation.ActivationGatewayError,'terminal phase'):self.validate()


    def test_old_gateway_schema_is_rejected(self):
        self.aggregate['schema']=subject.activation.RESULT_SCHEMA;self.sign()
        with self.assertRaisesRegex(ValueError,'runner terminal'):self.validate()

    def test_root_executor_is_rejected(self):
        self.aggregate['root_product_executor']=True;self.sign()
        with self.assertRaisesRegex(ValueError,'runner-owned lifecycle'):self.validate()

    def test_actual_runner_cluster_pg_ctl_without_boot_is_accepted(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            plan=Path(directory)/'plan.json';plan.write_text('{}')
            result={'schema':subject.runner.clusterctl.ACTIVATION_SCHEMA,'phase':'activated',
                'package_id':self.package,'restart_proven':True,'lifecycle_provider':'pg_ctl',
                'boot_enabled':False,'service_integration_required':False,
                'active_target':'releases/'+self.package,'runtime_target':'../releases/'+self.package,
                'cluster_plan_path':str(plan)}
            result['activation_receipt_sha256']=subject.runner.document_identity(result,'activation_receipt_sha256')
            self.assertEqual(subject.runner.validate_cluster_result(result,self.package),plan)

    def test_exact_retention_preserves_original_bytes_and_distinct_reactivations(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            p=Path(directory);source=p/'result.json';store=p/'receipts'
            data=json.dumps(self.aggregate,indent=3).encode()+b'\n'
            source.write_bytes(data)
            first=subject.retain_activation(source,self.commit,store)
            self.assertEqual(first.read_bytes(),data);self.assertEqual(source.read_bytes(),data)
            self.assertEqual(subject.retain_activation(source,self.commit,store),first)
            self.aggregate['unicode_activation_receipt_sha256']='8'*64;self.sign()
            source.write_text(json.dumps(self.aggregate))
            second=subject.retain_activation(source,self.commit,store)
            self.assertNotEqual(first,second);self.assertEqual(first.read_bytes(),data)
            self.assertEqual(len(list(first.parent.glob('*.json'))),2)

    def test_retention_rejects_conflicting_bytes_at_same_digest_address(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            p=Path(directory);source=p/'result.json';store=p/'receipts'
            source.write_text(json.dumps(self.aggregate))
            target=subject.retain_activation(source,self.commit,store)
            target.write_text('conflict')
            with self.assertRaisesRegex(subject.receipt_estate.ReceiptEstateError,'conflicts'):
                subject.retain_activation(source,self.commit,store)
            self.assertEqual(target.read_text(),'conflict')


class LoadedPlanTests(unittest.TestCase):
    def test_runtime_selection_uses_activated_build_receipt_independent_of_stockfish_location(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root=Path(directory);runtime=root/'actual-runtime';runtime.mkdir()
            stockfish=root/'operator'/'SF_19';stockfish.mkdir(parents=True)
            retained={'path':str(runtime),'tree_sha256':'1'*64,'file_count':5,
                'symlink_count':0,'directory_count':2,'total_file_bytes':999}
            active={'package_id':'2'*64,'manifest':{'provenance':{'build_input_closure':{
                'schema':'laplace.product-build-input-closure-receipt/v1','complete':True,
                'build_input_roots':{'tree-sitter':retained}}}}}
            result=subject.select_runtime_root(active,None)
            self.assertEqual(result['runtime_root'],str(runtime))
            self.assertNotEqual(Path(result['runtime_root']),stockfish.parent/'tree-sitter')
            self.assertEqual(result['activated_package_build_input'],retained)
            explicit=root/'qualified-copy';explicit.mkdir()
            result=subject.select_runtime_root(active,explicit)
            self.assertEqual(result['runtime_root'],str(explicit))
            self.assertEqual(result['selection'],'explicit-runtime-root')
            self.assertEqual(result['activated_package_build_input'],retained)
            for missing in ('complete','tree-sitter'):
                bad=copy.deepcopy(active)
                closure=bad['manifest']['provenance']['build_input_closure']
                if missing=='complete':closure['complete']=False
                else:closure['build_input_roots'].pop('tree-sitter')
                with self.subTest(missing=missing),self.assertRaisesRegex(ValueError,'activated package'):
                    subject.select_runtime_root(bad,None)
            runtime.rmdir()
            with self.assertRaisesRegex(ValueError,'runtime checkout is unavailable'):
                subject.select_runtime_root(active,None)

    def test_each_native_receipt_link_rejects_another_generation(self):
        plan={'plan_sha256':'1'*64}
        cluster={'plan_sha256':plan['plan_sha256'],'activation_receipt_sha256':'2'*64}
        unicode={'cluster_plan_sha256':plan['plan_sha256'],
            'cluster_activation_receipt_sha256':cluster['activation_receipt_sha256'],'receipt_sha256':'3'*64}
        highway={**unicode,'unicode_activation_receipt_sha256':unicode['receipt_sha256']}
        subject.validate_native_activation_links(plan,cluster,unicode,highway)
        for index,key in ((1,'plan_sha256'),(2,'cluster_plan_sha256'),(2,'cluster_activation_receipt_sha256'),
                (3,'cluster_plan_sha256'),(3,'cluster_activation_receipt_sha256'),(3,'unicode_activation_receipt_sha256')):
            values=copy.deepcopy([plan,cluster,unicode,highway]);values[index][key]='9'*64
            with self.subTest(index=index,key=key),self.assertRaisesRegex(ValueError,'activation'):
                subject.validate_native_activation_links(*values)

    def test_each_required_loaded_object_is_bound_to_verified_package(self):
        release=Path('/opt/laplace/releases')/('1'*64)
        contract={'instance':{'database':'laplace_refactor','port':55433},
            'package':{'required_loaded_objects':['pgsql-18/bin/postgres','lib/liblaplace_engine.so.2.0.0']}}
        files={key:{'sha256':str(index+2)*64} for index,key in enumerate(contract['package']['required_loaded_objects'])}
        plan={'package_root':str(release),'instance':copy.deepcopy(contract['instance']),'required_loaded_objects':[{'path':str(release/k),'sha256':v['sha256']} for k,v in files.items()]}
        manifest={'root':str(release)}
        subject.validate_loaded_plan(plan,manifest,contract,files,release)
        for change in ('omit','hash','root','instance'):
            bad=copy.deepcopy(plan)
            if change=='omit':bad['required_loaded_objects'].pop()
            elif change=='hash':bad['required_loaded_objects'][0]['sha256']='9'*64
            elif change=='root':bad['package_root']='/opt/laplace/releases/'+('9'*64)
            else:bad['instance']['database']='another_cluster'
            with self.subTest(change=change),self.assertRaisesRegex(ValueError,'loaded plan'):
                subject.validate_loaded_plan(bad,manifest,contract,files,release)

    def test_actual_process_owner_can_read_provider_without_chmod(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            library=Path(directory)/'provider.so';library.write_bytes(b'fixture provider');library.chmod(0o440)
            receipt={'library':{'path':str(library),'sha256':subject.sha(library),'byte_count':library.stat().st_size}}
            active={'loaded':{'postmaster_pid':os.getpid()}}
            self.assertTrue(subject.verify_provider_access(receipt,active)['provider_readable_and_parents_traversable'])
            library.chmod(0o640);library.write_bytes(b'changed provider')
            with self.assertRaisesRegex(ValueError,'provider bytes differ'):subject.verify_provider_access(receipt,active)


class WorkflowTests(unittest.TestCase):
    def test_acceptance_requires_successful_main_activation_and_retains_failures(self):
        import yaml
        workflow=yaml.safe_load((ROOT/'.github/workflows/product-path.yml').read_text())
        job=workflow['jobs']['deployed-stockfish-corpus']
        self.assertEqual(job['needs'],'dev-bat-deployment')
        self.assertIn("needs.dev-bat-deployment.result == 'success'",job['if'])
        self.assertIn("github.ref == 'refs/heads/main'",job['if'])
        self.assertEqual(job['with']['expected_sha'],'${{ github.sha }}')
        acceptance=yaml.safe_load((ROOT/'.github/workflows/stockfish-corpus-acceptance.yml').read_text())
        steps=acceptance['jobs']['installed-source-acceptance']['steps']
        executing=next(s for s in steps if 'stockfish_corpus_acceptance.py' in s.get('run',''))
        self.assertIn('tools/host/run-exclusive.sh',executing['shell'])
        self.assertIn('--expected-sha "$EXPECTED_SHA"',executing['run'])
        upload=next(s for s in steps if s.get('uses','').startswith('actions/upload-artifact@'))
        self.assertEqual(upload['if'],'always()')
        activation=yaml.safe_load((ROOT/'.github/workflows/product-activation.yml').read_text())
        steps=next(job['steps'] for job in activation['jobs'].values()
            if any('retain_activation' in s.get('run','') for s in job.get('steps',[])))
        retaining=next(i for i,s in enumerate(steps) if 'retain_activation' in s.get('run',''))
        producer=next(i for i,s in enumerate(steps) if 'product_activation_reconcile.py' in s.get('run',''))
        self.assertGreater(retaining,producer)
        self.assertIn('tools/host/run-exclusive.sh',steps[retaining]['shell'])
        self.assertNotIn('if',steps[retaining])


class ChildCommandTests(unittest.TestCase):
    def test_real_failed_child_retains_stdout_stderr_and_status(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            p=Path(directory);report={'commands':[]}
            command=[sys.executable,'-c',"import sys;print('partial');print('counterexample',file=sys.stderr);sys.exit(7)"]
            with self.assertRaisesRegex(ValueError,'failed; exact command'):
                subject.run_command(command,'admission',p,time.monotonic()+10,report)
            self.assertEqual(report['commands'][0]['exit_code'],7)
            self.assertEqual((p/'admission.stdout.log').read_text(),'partial\n')
            self.assertEqual((p/'admission.stderr.log').read_text(),'counterexample\n')

    def test_real_timeout_preserves_unknown_outcome(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            p=Path(directory);report={'commands':[]}
            with self.assertRaisesRegex(ValueError,'deadline exhausted'):
                subject.run_command([sys.executable,'-c','import time;time.sleep(30)'],
                    'repeat',p,time.monotonic()+0.1,report)
            self.assertEqual(report['commands'][0]['status'],'deadline-exhausted-submission-outcome-unknown')
            self.assertTrue((p/'repeat.stderr.log').exists())

    def test_output_under_upstream_is_rejected_before_any_write(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            base=Path(directory);source=base/'upstream';source.mkdir();prefix=base/'prefix';prefix.mkdir()
            (prefix/'current.json').write_text(json.dumps({'tools':{'stockfish':{'source':str(source)}}}))
            output=source/'receipt'
            with self.assertRaisesRegex(ValueError,'outside the upstream'):
                subject.execute(argparse.Namespace(output=output,chess_prefix=prefix))
            self.assertFalse(output.exists())


if __name__=='__main__':
    unittest.main()
