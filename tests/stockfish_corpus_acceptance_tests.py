#!/usr/bin/env python3
"""Deliberate counterexamples for installed full-source acceptance orchestration."""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
import pwd
import subprocess
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
        self.commit='1'*40; self.package='2'*64; self.fingerprint='a'*64
        self.manifest={'package_id':self.package,'root':'/opt/laplace/releases/'+self.package,
            'laplace':{'repository_commit':self.commit,'repository_tree':'3'*40,
                'repository_build_fingerprint':self.fingerprint}}
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
        subject.validate_package_binding(self.commit,self.package,self.manifest,self.installation,self.aggregate,
            self.fingerprint)

    def test_exact_source_package_activation_is_accepted(self): self.validate()

    def test_stale_manifest_even_with_current_activation_claim_is_rejected(self):
        self.manifest['laplace']['repository_build_fingerprint']='9'*64
        with self.assertRaisesRegex(ValueError,'stale'): self.validate()

    def test_equal_package_inputs_preserve_original_build_provenance(self):
        self.manifest['laplace']['repository_commit']='8'*40
        self.manifest['laplace']['repository_tree']='9'*40
        self.installation['package_manifest_sha256']=subject.runner.clusterctl.sha256_bytes(
            subject.runner.clusterctl.canonical_bytes(self.manifest))
        self.installation['installation_receipt_sha256']=subject.runner.document_identity(
            self.installation,'installation_receipt_sha256')
        self.aggregate['package_installation_receipt_sha256']=self.installation['installation_receipt_sha256']
        self.sign()
        before=copy.deepcopy((self.manifest,self.installation,self.aggregate))
        self.validate()
        self.assertEqual((self.manifest,self.installation,self.aggregate),before)
        self.assertEqual(self.aggregate['repository_commit'],self.commit)

    def test_missing_or_malformed_original_provenance_is_rejected(self):
        original=copy.deepcopy(self.manifest['laplace'])
        for field in ('repository_commit','repository_tree','repository_build_fingerprint'):
            for value in (None,'','not-an-identity'):
                with self.subTest(field=field,value=value):
                    self.manifest['laplace']=copy.deepcopy(original)
                    if value is None: del self.manifest['laplace'][field]
                    else: self.manifest['laplace'][field]=value
                    with self.assertRaisesRegex(ValueError,'provenance'):self.validate()

    def test_installed_observer_resolves_requested_immutable_commit_before_host_reads(self):
        sentinel=RuntimeError('expected immutable source boundary')
        with mock.patch.object(subject.runner,'require_runner'), \
             mock.patch.object(subject.repository_inputs,'repository_build_fingerprint_at_commit',
                               side_effect=sentinel) as fingerprint:
            with self.assertRaisesRegex(RuntimeError,'immutable source boundary'):
                subject.observe_activation(self.commit,Path('unused-result.json'))
        fingerprint.assert_called_once_with(subject.ROOT,self.commit)

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



class NativeReceiptEstateTests(unittest.TestCase):
    """Actual receipt files/moves and validators; no installed PostgreSQL claim."""
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(dir=SCRATCH)
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)/'receipts/postgresql/refactor'
        self.root.mkdir(parents=True)
        binding=PackageTests();binding.setUp()
        for key in ('commit','package','manifest','installation','aggregate','fingerprint'):
            setattr(self,key,copy.deepcopy(getattr(binding,key)))
        self.plan={'plan_sha256':'3'*64}
        self.cluster={'plan_sha256':self.plan['plan_sha256'],
            'activation_receipt_sha256':'4'*64,'system_identifier':'123456789'}
        self.unicode={'schema':subject.runner.unicodectl.RECEIPT_SCHEMA,'phase':'product-activated',
            'package_id':self.package,'restart_proven':True,'cold_public_readback_proven':True,
            'reverse_inversion_proven':True,'system_identifier':self.cluster['system_identifier'],
            'cluster_plan_sha256':self.plan['plan_sha256'],
            'cluster_activation_receipt_sha256':self.cluster['activation_receipt_sha256']}
        self.unicode['receipt_sha256']=subject.runner.unicodectl.document_identity(
            self.unicode,'receipt_sha256')
        self.highway={'schema':subject.runner.highwayctl.RECEIPT_SCHEMA,'phase':'product-activated',
            'package_id':self.package,'restart_proven':True,'cold_application_readback_proven':True,
            'system_identifier':self.cluster['system_identifier'],
            'cluster_plan_sha256':self.plan['plan_sha256'],
            'cluster_activation_receipt_sha256':self.cluster['activation_receipt_sha256'],
            'unicode_activation_receipt_sha256':self.unicode['receipt_sha256']}
        self.highway['receipt_sha256']=subject.runner.unicodectl.document_identity(
            self.highway,'receipt_sha256')
        self.write_generation()

    def write(self,path,document):
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(json.dumps(document,indent=3).encode()+b'\n')

    def write_generation(self):
        self.highway_name=('highway-committed-revalidation.json'
            if self.highway['schema']==subject.activation.HIGHWAY_REVALIDATION_SCHEMA
            else 'highway-product-activation.json')
        self.write(self.root/'unicode-product-activation.json',self.unicode)
        self.write(self.root/self.highway_name,self.highway)
        self.aggregate.update(subject.activation.highway_aggregate_fields(self.highway))
        self.aggregate.update({
            'cluster_activation_receipt_sha256':self.cluster['activation_receipt_sha256'],
            'unicode_activation_receipt_sha256':self.unicode['receipt_sha256'],
            'cluster_result':str(self.root/'cluster-activation'/self.package/'activation-result.json'),
            'unicode_result':str(self.root/'unicode-product-activation.json'),
            'highway_result':str(self.root/self.highway_name)})
        self.write_aggregate()

    def write_aggregate(self):
        aggregate_root=self.root/'product-activation'/self.commit/self.package
        if aggregate_root.exists():
            for path in aggregate_root.glob('*.json'):path.unlink()
        self.aggregate['result_sha256']=subject.runner.document_identity(self.aggregate,'result_sha256')
        self.aggregate_path=aggregate_root/(self.aggregate['result_sha256']+'.json')
        self.write(self.aggregate_path,self.aggregate)

    def observe(self):
        return subject.observe_native_receipts(self.root,self.commit,self.package,
            self.manifest,self.installation,self.plan,self.cluster,self.fingerprint)

    def converge(self):
        from delivery import receipt_estate_runner
        # The production owner still performs every real filesystem operation;
        # only the expected account name is mapped to this test process account.
        with mock.patch.object(receipt_estate_runner,'RUNNER_USER',pwd.getpwuid(os.geteuid()).pw_name):
            return receipt_estate_runner.converge({'instance':{'receipt_directory':str(self.root)}})

    def test_real_runner_convergence_preserves_exact_authenticated_observation(self):
        original=self.aggregate_path.read_bytes()
        before=self.observe()
        unicode_bytes=(self.root/'unicode-product-activation.json').read_bytes()
        highway_bytes=(self.root/self.highway_name).read_bytes()
        migrated=self.converge()
        after=self.observe()
        generation=self.root/'cluster-activation'/self.package
        self.assertEqual(migrated['migration_count'],2)
        self.assertFalse((self.root/'unicode-product-activation.json').exists())
        self.assertFalse((self.root/self.highway_name).exists())
        self.assertEqual((generation/'unicode-product-activation.json').read_bytes(),unicode_bytes)
        self.assertEqual((generation/self.highway_name).read_bytes(),highway_bytes)
        self.assertEqual(before[:2],after[:2])
        self.assertEqual(after[2][0][1],self.aggregate)
        self.assertEqual(after[2][0][0],self.aggregate_path)
        self.assertEqual(self.aggregate_path.read_bytes(),original)
        self.assertEqual(after[3],{'unicode':str(generation/'unicode-product-activation.json'),
            'highway':str(generation/self.highway_name)})
        self.assertEqual(self.converge()['migration_count'],0)
        self.assertEqual(self.observe(),after)

    def test_identical_dual_copies_are_accepted_but_even_whitespace_conflict_is_rejected(self):
        source=self.root/'unicode-product-activation.json'
        target=self.root/'cluster-activation'/self.package/source.name
        target.parent.mkdir(parents=True)
        target.write_bytes(source.read_bytes())
        self.assertEqual(self.observe()[3]['unicode'],str(target))
        target.write_bytes(target.read_bytes()+b' ')
        with self.assertRaisesRegex(ValueError,'bytes conflict'):self.observe()
        self.assertTrue(source.is_file());self.assertTrue(target.is_file())

    def test_unsafe_or_malformed_present_copy_cannot_fall_back_to_valid_copy(self):
        source=self.root/'unicode-product-activation.json'
        target=self.root/'cluster-activation'/self.package/source.name
        target.parent.mkdir(parents=True)
        data=source.read_bytes()
        for bad,good in ((source,target),(target,source)):
            for kind in ('symlink','malformed','wrong-package'):
                with self.subTest(path=bad,kind=kind):
                    for path in (source,target):
                        if path.exists() or path.is_symlink():path.unlink()
                    good.write_bytes(data)
                    if kind=='symlink':bad.symlink_to(good)
                    elif kind=='malformed':bad.write_bytes(b'{broken')
                    else:
                        other=copy.deepcopy(self.unicode);other['package_id']='9'*64
                        self.write(bad,other)
                    with self.assertRaises((ValueError,json.JSONDecodeError)):self.observe()
        for path in (source,target):
            if path.exists() or path.is_symlink():path.unlink()
        source.write_bytes(data)
        target.parent.rmdir()
        outside=Path(self.temporary.name)/'outside';outside.mkdir()
        (outside/source.name).write_bytes(data)
        target.parent.symlink_to(outside,target_is_directory=True)
        with self.assertRaisesRegex(ValueError,'directory is unsafe'):self.observe()

    def test_canonical_native_digest_and_cluster_links_remain_required(self):
        self.converge()
        target=self.root/'cluster-activation'/self.package/'unicode-product-activation.json'
        for kind in ('digest','cluster-link'):
            with self.subTest(kind=kind):
                value=copy.deepcopy(self.unicode)
                if kind=='digest':value['receipt_sha256']='9'*64
                else:
                    value['cluster_activation_receipt_sha256']='9'*64
                    value['receipt_sha256']=subject.runner.unicodectl.document_identity(value,'receipt_sha256')
                    self.aggregate['unicode_activation_receipt_sha256']=value['receipt_sha256']
                    self.highway['unicode_activation_receipt_sha256']=value['receipt_sha256']
                    self.highway['receipt_sha256']=subject.runner.unicodectl.document_identity(
                        self.highway,'receipt_sha256')
                    self.aggregate.update(subject.activation.highway_aggregate_fields(self.highway))
                    self.write_aggregate()
                    self.write(target.parent/self.highway_name,self.highway)
                self.write(target,value)
                with self.assertRaises((ValueError,subject.runner.RunnerActivationError)):self.observe()

    def test_authenticated_aggregate_keeps_original_paths_and_digest_after_move(self):
        self.converge()
        original=copy.deepcopy(self.aggregate)
        for field in ('cluster_result','unicode_result','highway_result'):
            with self.subTest(field=field):
                self.aggregate=copy.deepcopy(original)
                self.aggregate[field]=str(self.root/'cluster-activation'/self.package/Path(original[field]).name
                    if field!='cluster_result' else self.root/'activation-result.json')
                self.write_aggregate()
                with self.assertRaisesRegex(ValueError,'receipt paths differ'):self.observe()
        self.aggregate=copy.deepcopy(original);self.write_aggregate()
        forged=copy.deepcopy(self.aggregate);forged['result_sha256']='8'*64
        self.write(self.aggregate_path,forged)
        with self.assertRaises(ValueError):self.observe()
        self.write(self.aggregate_path,self.aggregate)
        wrong_address=self.aggregate_path.with_name('9'*64+'.json')
        self.aggregate_path.rename(wrong_address)
        with self.assertRaisesRegex(ValueError,'address differs'):self.observe()

    def test_mixed_and_fully_converged_revalidation_use_unchanged_producer_proof(self):
        import product_activation_runtime_adapter_tests as recovery_fixture
        highway=recovery_fixture.producer_revalidation(self)
        self.cluster['system_identifier']=highway['system_identifier']
        self.unicode['system_identifier']=highway['system_identifier']
        self.unicode['receipt_sha256']=subject.runner.unicodectl.document_identity(
            self.unicode,'receipt_sha256')
        links={'package_id':self.package,'cluster_plan_sha256':self.plan['plan_sha256'],
            'cluster_activation_receipt_sha256':self.cluster['activation_receipt_sha256'],
            'unicode_activation_receipt_sha256':self.unicode['receipt_sha256']}
        highway.update(links);highway['revalidation_request'].update(links)
        recovery_fixture.resign_revalidation(highway)
        (self.root/self.highway_name).unlink()
        self.aggregate.pop('highway_activation_receipt_sha256')
        self.highway=highway;self.write_generation()
        original=self.aggregate_path.read_bytes()
        before=self.observe()
        # Construct an actual partial migration explicitly. The ordinary runner
        # now converges both Unicode and committed revalidation in one call.
        moved=[]
        subject.receipt_estate.migrate_singleton_product_receipt(
            self.root/'unicode-product-activation.json', self.root/'cluster-activation',
            'unicode-product-activation.json',os.geteuid(),os.getegid(),False,moved)
        self.assertEqual(len(moved),1)
        mixed=self.observe()
        self.assertEqual(mixed[:2],before[:2])
        self.assertEqual(mixed[3]['highway'],str(self.root/self.highway_name))
        self.assertEqual(self.converge()['migration_count'],1)
        after=self.observe()
        self.assertEqual(after[:2],before[:2])
        self.assertEqual(after[3]['highway'],str(self.root/'cluster-activation'/self.package/self.highway_name))
        self.assertEqual(after[2][0][1],self.aggregate)
        self.assertEqual(self.aggregate_path.read_bytes(),original)

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
        self.assertEqual(job['needs'],['classify','dev-bat-deployment'])
        self.assertIn("needs.classify.outputs.requires_stockfish_corpus == 'true'",job['if'])
        self.assertIn("needs.dev-bat-deployment.result == 'success'",job['if'])
        self.assertIn("github.ref == 'refs/heads/main'",job['if'])
        self.assertEqual(job['with']['expected_sha'],'${{ github.sha }}')
        hosted=yaml.safe_load((ROOT/'.github/workflows/ci.yml').read_text())
        self.assertTrue(any(s.get('run')=='python3 tests/stockfish_corpus_acceptance_tests.py'
                            and 'if' not in s
                            for s in hosted['jobs']['requirements']['steps']))
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

    def test_source_selection_retains_runner_override_and_verifies_before_work(self):
        import yaml
        for path,job in (('chess-calibration.yml','calibrate'),
                         ('stockfish-corpus-acceptance.yml','installed-source-acceptance')):
            workflow=yaml.safe_load((ROOT/'.github/workflows'/path).read_text())
            environment=workflow['jobs'][job]['env']
            self.assertNotIn('LAPLACE_STOCKFISH_SOURCE',environment)
            self.assertEqual(environment['STOCKFISH_SOURCE_OVERRIDE'],'${{ vars.LAPLACE_STOCKFISH_SOURCE }}')
            commands='\n'.join(s.get('run','') for s in workflow['jobs'][job]['steps'])
            self.assertIn('if [[ -n "${STOCKFISH_SOURCE_OVERRIDE:-}" ]]; then',commands)
            self.assertIn('chess_tools.py select-source',commands)
            if job=='calibrate':
                self.assertLess(commands.index('chess_tools.py select-source'),commands.index('scripts/setup-chess.sh'))
                self.assertGreater(commands.index('chess_tools.py check-source-selection'),commands.index('scripts/setup-chess.sh'))
                self.assertIn('if [[ -n "$selected_source" ]]; then',commands)
            else:
                self.assertIn('--source-selection "$selection"',commands)
                self.assertIn("shutil.copyfile(selection, output / 'stockfish-source-selection.json')",commands)



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


    def test_real_checkout_selection_mismatch_fails_before_activation_or_admission(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            base=Path(directory);source=base/'chosen SF_19';other=base/'stale same-content checkout'
            subprocess.run(['git','init','--quiet',str(source)],check=True)
            (source/'source.cpp').write_text('int main() { return 0; }\n')
            subprocess.run(['git','-C',str(source),'add','.'],check=True)
            subprocess.run(['git','-C',str(source),'-c','user.name=Source Test',
                '-c','user.email=test@example.invalid','commit','--quiet','-m','fixture'],check=True)
            subprocess.run(['git','-C',str(source),'remote','add','origin',
                'https://github.com/official-stockfish/Stockfish.git'],check=True)
            subprocess.run(['git','clone','--quiet','--no-hardlinks',str(source),str(other)],check=True)
            prefix=base/'prefix';prefix.mkdir()
            manifest=prefix/'current.json'
            manifest.write_text(json.dumps({'tools':{'stockfish':{'source':str(source)}}}))
            with mock.patch.object(subject.chess_tools,'REQUESTED_STOCKFISH_SOURCE',source):
                selection=subject.chess_tools.select_stockfish_source(prefix)
            selection_path=base/'selection.json';subject.save(selection_path,selection)
            manifest.write_text(json.dumps({'tools':{'stockfish':{'source':str(other)}}}))
            output=base/'evidence'
            args=argparse.Namespace(output=output,chess_prefix=prefix,expected_sha='a'*40,
                timeout=30,source_selection=selection_path,stockfish_source=None)
            with mock.patch.object(subject,'observe_activation') as activation, \
                 mock.patch.object(subject,'run_command') as command:
                with self.assertRaisesRegex(subject.chess_tools.ChessToolError,'source differs from the selected checkout'):
                    subject.execute(args)
            activation.assert_not_called();command.assert_not_called()
            failed=subject.load(output/'result.json')
            self.assertEqual(failed['status'],'failed')
            self.assertEqual(failed['database_mutation_outcome'],'admission-not-submitted')
            self.assertEqual(subject.load(output/'stockfish-source-selection.json'),selection)
            self.assertEqual(subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD']),
                             subprocess.check_output(['git','-C',str(other),'rev-parse','HEAD']))


    def test_output_under_different_requested_or_explicit_checkout_is_rejected_without_writes(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            base=Path(directory);source=base/'configured';requested=base/'requested SF_19'
            subprocess.run(['git','init','--quiet',str(source)],check=True)
            (source/'source.cpp').write_text('int main() { return 0; }\n')
            subprocess.run(['git','-C',str(source),'add','.'],check=True)
            subprocess.run(['git','-C',str(source),'-c','user.name=Source Test',
                '-c','user.email=test@example.invalid','commit','--quiet','-m','fixture'],check=True)
            official='https://github.com/official-stockfish/Stockfish.git'
            subprocess.run(['git','-C',str(source),'remote','add','origin',official],check=True)
            subprocess.run(['git','clone','--quiet','--no-hardlinks',str(source),str(requested)],check=True)
            subprocess.run(['git','-C',str(requested),'remote','set-url','origin',official],check=True)
            prefix=base/'prefix';prefix.mkdir()
            (prefix/'current.json').write_text(json.dumps({'tools':{'stockfish':{'source':str(source)}}}))
            original=(requested/'source.cpp').read_bytes()
            for mode in ('requested','explicit','retained-selection'):
                output=requested/('forbidden-evidence-'+mode)
                args=argparse.Namespace(output=output,chess_prefix=prefix,source_selection=None,
                    stockfish_source=requested if mode=='explicit' else None)
                with mock.patch.object(subject.chess_tools,'REQUESTED_STOCKFISH_SOURCE',
                                       requested if mode!='explicit' else base/'absent'):
                    if mode=='retained-selection':
                        selection=subject.chess_tools.select_stockfish_source(prefix)
                        args.source_selection=base/'retained-selection.json'
                        subject.save(args.source_selection,selection)
                    with mock.patch.object(subject,'observe_activation') as activation, \
                         mock.patch.object(subject,'run_command') as command:
                        with self.subTest(mode=mode), self.assertRaisesRegex(ValueError,'outside the upstream'):
                            subject.execute(args)
                    activation.assert_not_called();command.assert_not_called()
                self.assertFalse(output.exists())
                self.assertEqual((requested/'source.cpp').read_bytes(),original)
                self.assertEqual(subprocess.check_output(
                    ['git','-C',str(requested),'status','--porcelain=v1','--untracked-files=all']),b'')

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
