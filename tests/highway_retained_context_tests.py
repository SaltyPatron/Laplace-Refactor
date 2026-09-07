#!/usr/bin/env python3
"""Regression coverage for exact native admission replay across package upgrades."""
from __future__ import annotations
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('highway_replay_fixtures', ROOT/'tests/highway_product_activation_tests.py')
f = importlib.util.module_from_spec(spec); spec.loader.exec_module(f)
h = f.highway


class RetainedHighwayContext(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.fixture = f.HighwayProductActivationTests(); self.fixture.setUp()
        self.contract = self.fixture.contract
        self.cluster = self.fixture.cluster
        self.unicode = self.fixture.unicode_receipt
        self.inspection = self.fixture.inspection('target-fresh')
        self.request = {
            'schema':'laplace.highway-product-activation-request/v1',
            'activation_contract_sha256':h.unicodectl.sha256_bytes(h.unicodectl.canonical_bytes(self.contract)),
            'registry_contract_sha256':'11'*32,'predecessor_registry_contract_sha256':'12'*32,
            'unicode_request_fingerprint':self.unicode['request_fingerprint'],
            'package_id':'new','operation':self.contract['operation'],
            'execution_context':self.contract['execution_context'],'expected_result':self.contract['expected_result'],
            'inspected_state':h.validate_inspection(self.inspection,self.unicode,self.contract),
            'system_identifier':'123456789'}
        self.original = dict(self.request, package_id='old', inspected_state=h.validate_inspection(
            self.fixture.inspection('fresh'),self.unicode,self.contract))
        self.directory=h.unicodectl.prefixed(self.root,self.cluster['instance']['receipt_directory'])/'highway'
        self.directory.mkdir(parents=True)
        self.folder=self.save(self.original)

    def save(self, request):
        digest=h.unicodectl.sha256_bytes(h.unicodectl.canonical_bytes(request))
        folder=self.directory/digest
        h.unicodectl.write_immutable(folder/'request.json',request)
        return folder

    def replay(self):
        return h.retained_activation_state(self.root,self.cluster,self.inspection,self.request)

    def test_replay_uses_exact_original_numeric_input_not_active_numeric_epoch(self):
        before=(self.folder/'request.json').read_bytes()
        state,digest=self.replay()
        self.assertEqual(state['numeric_epoch'],h.ZERO_256)
        self.assertEqual(state['mode'],'replay')
        self.assertEqual(state['expected_activation_sequence'],1)
        self.assertEqual(digest,self.folder.name)
        self.assertEqual(before,(self.folder/'request.json').read_bytes())
        sql=h.render_activation_sql(self.contract,self.fixture.identities,self.unicode,state)
        self.assertIn("decode('"+h.ZERO_256+"','hex')",sql)
        self.assertNotIn("decode('"+self.inspection['highway_epoch_fingerprint']+"','hex')",sql)

    def test_successor_replays_its_original_predecessor_instead_of_zero(self):
        self.inspection=self.fixture.inspection('target-successor')
        self.request['inspected_state']=h.validate_inspection(self.inspection,self.unicode,self.contract)
        original=dict(self.original,inspected_state=h.validate_inspection(
            self.fixture.inspection('predecessor'),self.unicode,self.contract))
        folder=self.save(original)
        state,digest=self.replay()
        self.assertEqual(state['numeric_epoch'],'51'*32)
        self.assertEqual(state['expected_activation_sequence'],2)
        self.assertEqual(digest,folder.name)
        sql=h.render_activation_sql(self.contract,self.fixture.identities,self.unicode,state)
        self.assertIn('WHERE activation_epoch_id=activated.registry_epoch_id',sql)

    def test_different_semantic_inputs_have_no_admissible_replay(self):
        for field in ('activation_contract_sha256','registry_contract_sha256','unicode_request_fingerprint',
                      'predecessor_registry_contract_sha256','operation','execution_context','expected_result'):
            old=self.request[field]; self.request[field]='changed'
            with self.subTest(field=field),self.assertRaisesRegex(h.HighwayActivationError,'one exact retained context'):
                self.replay()
            self.request[field]=old

    def test_other_database_cannot_reuse_an_admission(self):
        self.request['system_identifier']='987'
        with self.assertRaisesRegex(h.HighwayActivationError,'database identity differs'): self.replay()

    def test_request_tampering_and_symlinks_are_rejected(self):
        path=self.folder/'request.json'; saved=path.read_bytes()
        path.write_bytes(saved+b' ')
        with self.assertRaisesRegex(h.HighwayActivationError,'request identity differs'):self.replay()
        path.unlink(); other=self.root/'other.json';other.write_bytes(saved);path.symlink_to(other)
        with self.assertRaisesRegex(h.HighwayActivationError,'unsafe retained'):self.replay()

    def test_repeated_interrupted_requests_share_one_context_without_hiding_conflicts(self):
        self.save(dict(self.original,package_id='another-package'))
        state,_=self.replay();self.assertEqual(state['numeric_epoch'],h.ZERO_256)
        conflict=dict(self.original,inspected_state=dict(self.original['inspected_state'],mode='successor',numeric_epoch='aa'*32))
        self.save(conflict)
        with self.assertRaisesRegex(h.HighwayActivationError,'one exact retained context'): self.replay()

    def test_active_to_active_context_is_never_admitted_as_original(self):
        self.save(dict(self.original,inspected_state=dict(self.original['inspected_state'],
                      numeric_epoch=self.inspection['highway_epoch_fingerprint'])))
        with self.assertRaisesRegex(h.HighwayActivationError,'predecessor epoch is invalid'): self.replay()

    def test_legacy_request_requires_untampered_completed_receipt_bound_to_epoch_and_database(self):
        (self.folder/'request.json').unlink();self.folder.rmdir()
        original=dict(self.original);original.pop('system_identifier')
        folder=self.save(original)
        with self.assertRaisesRegex(h.HighwayActivationError,'lacks exact completed receipt'):self.replay()
        receipt={'schema':h.RECEIPT_SCHEMA,'phase':'product-activated','request_sha256':folder.name,
                 'package_id':original['package_id'],'system_identifier':'123456789',
                 'activation':{'registry_epoch_id':self.inspection['highway_epoch_id'],
                               'registry_epoch_fingerprint':self.inspection['highway_epoch_fingerprint']}}
        receipt['receipt_sha256']=h.unicodectl.document_identity(receipt,'receipt_sha256')
        h.unicodectl.write_immutable(folder/'receipt.json',receipt)
        state,_=self.replay(); self.assertEqual(state['numeric_epoch'],h.ZERO_256)
        receipt['activation']['registry_epoch_fingerprint']='ff'*32
        (folder/'receipt.json').write_bytes(h.unicodectl.canonical_bytes(receipt))
        with self.assertRaisesRegex(h.HighwayActivationError,'receipt differs'):self.replay()

    def test_controller_replays_twice_and_retains_request_before_native_execution(self):
        # Exercise the controller with the same native boundary fixture as the
        # existing lifecycle tests. Real PostgreSQL coverage is a separate test.
        fixture=self.fixture; package={'package_id':self.unicode['package_id']}
        plan={'plan_sha256':'91'*32,'instance':self.cluster['instance'],'commands':{'probe_readiness':['true']}}
        cluster_receipt={'activation_receipt_sha256':'92'*32,'system_identifier':'123456789'}
        # Replace the helper-specific fixture with an actual controller request.
        (self.folder/'request.json').unlink(); self.folder.rmdir()
        calls=[]; mode='fresh'
        def sql(_plan,_contract,program,label,_os_user,role,_timeout):
            calls.append((label,program,role))
            if label=='inspect-highway-product-state':
                return fixture.inspection('fresh' if mode=='fresh' else 'target-fresh'),{'label':label}
            if label=='admit-and-activate-highway-product-registry':
                self.assertTrue(list(self.directory.glob('*/request.json')))
                self.assertIn("decode('"+h.ZERO_256+"','hex')",program)
                self.assertNotIn("decode('"+'52'*32+"','hex')",program)
                return fixture.activation_result(1 if mode=='fresh' else 0),{'label':label}
            return fixture.readback(fixture.activation_result()),{'label':label}
        def execute():
            observations=iter([{'system_identifier':'123456789','postmaster_pid':10,'loaded_objects':[],
                               'config_files':[],'observation_sha256':'93'*32},
                              {'system_identifier':'123456789','postmaster_pid':11,'loaded_objects':[],
                               'config_files':[],'observation_sha256':'94'*32}])
            return h.execute_highway_activation(self.contract,self.cluster,fixture.unicode_contract,fixture.registry,
                fixture.previous_registry,package,plan,cluster_receipt,self.unicode,self.root,False,
                sql_runner=sql,loaded_observer=lambda *_:next(observations),
                command_runner=lambda label,*_: {'label':label},readiness_runner=lambda label,*_:{'label':label})
        with mock.patch.object(h.unicodectl,'validate_product_boundary'),\
             mock.patch.object(h,'load_unicode_identities',return_value=fixture.identities),\
             mock.patch.object(h.clusterctl,'verify_loaded'):
            first=execute(); mode='replay'
            second=execute();third=execute()
        self.assertEqual(first['mode'],'fresh');self.assertEqual(second['mode'],'replay')
        self.assertEqual(second,third)
        for result in (second,third):
            self.assertEqual(result['activation']['entity_inserted'],0)
            self.assertTrue(result['cold_application_readback_proven'])
        self.assertEqual(sum(label=='admit-and-activate-highway-product-registry' for label,_,_ in calls),3)
        self.assertEqual(calls[-1][2],'laplace_app')

if __name__=='__main__':unittest.main(verbosity=2)
