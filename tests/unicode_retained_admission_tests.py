#!/usr/bin/env python3
"""A deployment can reuse an admission only after its native identity is re-proved."""
from __future__ import annotations
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('retained_unicode_tests',ROOT/'tools/postgresql/unicodectl.py')
u=importlib.util.module_from_spec(spec);sys.modules[spec.name]=u;spec.loader.exec_module(u)

class RetainedUnicodeAdmission(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.contract=u.load_json(ROOT/'contracts/unicode-product-activation.json')
        self.identity={'schema':u.IDENTITY_SCHEMA}
        for i,(name,settings) in enumerate(self.contract['identity_provider']['fields'].items(),1):
            self.identity[name]=f'{i:02x}'*settings['bytes']
        self.folder=self.root/self.contract['receipt']['directory_name']/self.identity['request_fingerprint']
        self.folder.mkdir(parents=True)
        self.original={key:key for key in ('schema','activation_contract_sha256','cluster_contract_sha256',
            'cluster_system_identifier','source_contract_sha256','source_evidence_sha256','source_root',
            'unicode_postgresql_contract_sha256','operation','execution_context','expected_result')}
        self.original.update(package_id='old',orchestrator_sha256='old-code',cluster_plan_sha256='old-plan')
        self.current=dict(self.original,package_id='new',orchestrator_sha256='new-code',cluster_plan_sha256='new-plan')
        u.write_immutable(self.folder/'request.json',self.original)
        u.write_immutable(self.folder/'identities.json',self.identity)
        self.inspection={name:self.identity[name] for name in ('activation_epoch_id','activation_epoch_fingerprint')}
        self.calls=[]
        def native(executable,path,contract):
            self.calls.append((executable,path,contract))
            self.assertEqual(path.read_bytes(),u.canonical_bytes(self.original))
            return copy.deepcopy(self.identity),{'exit_code':0,'label':'native-old-request'}
        self.native=native

    def execute(self):
        return u.retained_root_identities(self.root,self.inspection,self.current,self.contract,Path('/verified/native-identify'),self.native)

    def stage_original_request(self):
        content=u.canonical_bytes(self.original)
        staging=self.root/'.unicode-activation';staging.mkdir(parents=True,exist_ok=True)
        path=staging/f'request-{u.sha256_bytes(content)}.json';path.write_bytes(content)
        return path

    def test_new_deployment_reuses_original_epoch_without_rewriting_admission(self):
        before={p.name:p.read_bytes() for p in self.folder.iterdir()}
        identity,request,command=self.execute()
        self.assertEqual(identity,self.identity);self.assertEqual(request,self.original)
        self.assertEqual(command['label'],'native-old-request');self.assertEqual(len(self.calls),1)
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.folder.iterdir()})

    def test_package_only_cluster_contract_churn_is_deployment_not_unicode_semantics(self):
        self.current['cluster_contract_sha256']='new-package-contract'
        identity,request,command=self.execute()
        self.assertEqual(identity,self.identity);self.assertEqual(request,self.original)
        self.assertEqual(command['label'],'native-old-request');self.assertEqual(len(self.calls),1)

    def test_changed_source_database_or_semantics_cannot_reuse_old_admission(self):
        for key in ('cluster_system_identifier','source_evidence_sha256','source_contract_sha256',
                    'source_root','operation','execution_context','expected_result','activation_contract_sha256'):
            original=self.current[key];self.current[key]='changed'
            with self.subTest(key=key),self.assertRaisesRegex(u.UnicodeActivationError,'admission differs: '+key):
                self.execute()
            self.current[key]=original
        self.assertEqual(self.calls,[])

    def test_native_fingerprint_mismatch_cannot_reuse_admission(self):
        self.native=lambda *_args:(dict(self.identity,source_epoch='ff'*32),{})
        with self.assertRaisesRegex(u.UnicodeActivationError,'native identity verification'):
            self.execute()

    def test_missing_evidence_or_wrong_active_epoch_has_no_match(self):
        self.inspection['activation_epoch_id']='ff'*16
        with self.assertRaisesRegex(u.UnicodeActivationError,'one exact retained admission'):
            self.execute()
        self.assertEqual(self.calls,[])

    def test_missing_canonical_leaf_recovers_only_from_exact_staged_request(self):
        staged=self.stage_original_request();shutil.rmtree(self.folder)
        identity,request,command=self.execute()
        self.assertEqual(identity,self.identity);self.assertEqual(request,self.original)
        self.assertEqual(command['label'],'native-old-request')
        self.assertEqual(self.calls,[(Path('/verified/native-identify'),staged,self.contract)])
        self.assertEqual((self.folder/'request.json').read_bytes(),u.canonical_bytes(self.original))
        self.assertEqual((self.folder/'identities.json').read_bytes(),u.canonical_bytes(self.identity))

    def test_staged_request_filename_must_bind_exact_canonical_bytes(self):
        shutil.rmtree(self.folder)
        staging=self.root/'.unicode-activation';staging.mkdir(parents=True)
        (staging/f"request-{'ff'*32}.json").write_bytes(u.canonical_bytes(self.original))
        with self.assertRaisesRegex(u.UnicodeActivationError,'staged Unicode request digest differs'):
            self.execute()
        self.assertEqual(self.calls,[])

    def test_unrelated_staged_request_cannot_become_committed_authority(self):
        shutil.rmtree(self.folder)
        unrelated=dict(self.original,source_evidence_sha256='other-source')
        content=u.canonical_bytes(unrelated)
        staging=self.root/'.unicode-activation';staging.mkdir(parents=True)
        (staging/f'request-{u.sha256_bytes(content)}.json').write_bytes(content)
        with self.assertRaisesRegex(u.UnicodeActivationError,'one exact retained admission'):
            self.execute()
        self.assertEqual(self.calls,[])

    def test_symlink_or_noncanonical_request_cannot_become_authority(self):
        request=self.folder/'request.json'
        request.write_text(json.dumps(self.original,indent=2))
        with self.assertRaisesRegex(u.UnicodeActivationError,'not canonical'):
            self.execute()
        request.unlink();request.symlink_to(self.folder/'identities.json')
        with self.assertRaisesRegex(u.UnicodeActivationError,'unsafe retained'):
            self.execute()
        self.assertEqual(self.calls,[])

    def test_evidence_directory_identity_cannot_be_relabelled(self):
        self.folder.rename(self.folder.with_name('ee'*32))
        with self.assertRaisesRegex(u.UnicodeActivationError,'directory identity differs'):
            self.execute()

    def test_candidate_admission_is_not_published_before_database_inspection(self):
        source=(ROOT/'tools/postgresql/unicodectl.py').read_text(encoding='utf-8')
        inspection=source.index('"inspect-unicode-product-state"')
        publication=source.index('write_immutable(evidence_directory / "request.json"')
        self.assertLess(inspection,publication)

    def test_source_scoped_counts_do_not_confuse_later_highway_content_with_corruption(self):
        sql=u.render_inspection_sql()
        for name in ('entity','physicality'):
            self.assertIn(f'FROM laplace.{name} AS owned',sql)
            self.assertIn(f'witness.{name}_id=owned.{name}_id',sql)
        self.assertIn('root.root_receipt=witness.source_fingerprint',sql)
        self.assertIn('WHERE witness.attestation_kind=3',sql)
        self.assertIn('WHERE NOT EXISTS (SELECT 1 FROM laplace.unicode_root_generation)',sql)

if __name__=='__main__':unittest.main(verbosity=2)
