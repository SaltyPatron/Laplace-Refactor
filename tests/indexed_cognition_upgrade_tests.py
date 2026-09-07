#!/usr/bin/env python3
"""Verify additive schema publication, package identity, and failure boundaries."""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('indexed_upgrade_runner', ROOT/'tools/delivery/product_activation_runner.py')
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)
class IndexedCognitionUpgrade(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.relative = 'pgsql-18/share/extension/laplace--1.0.0--1.0.1.sql'
        self.path = self.root/self.relative
        self.path.parent.mkdir(parents=True)
        self.path.write_text('verified native migration\n')
        self.plan = {'package_root':str(self.root),'postgresql_major':18}
        self.package = {'package_id':'ab'*32,'files':[{'path':self.relative,'kind':'file',
                        'sha256':runner.clusterctl.sha256_file(self.path)}]}
        self.contract = {'instance':{'admin_role':'laplace_admin'}}
        self.result = {'schema':'laplace.indexed-cognition-upgrade/v1','version':'1.0.1',
                       'owner':'laplace_admin','native_bindings':2,'ready_indexes':2}
    def test_owned_native_update_is_one_transaction_and_retains_package_receipt(self):
        with mock.patch.object(runner,'runner_sql',return_value=(self.result,{'exit_code':0})) as sql:
            receipt = runner.reconcile_indexed_cognition(self.plan,self.contract,self.package)
        program = sql.call_args.args[2]
        for part in ("BEGIN;", "COMMIT;", "version = '1.0.0'", "version <> '1.0.1'",
                     "ALTER EXTENSION laplace UPDATE TO '1.0.1'", "owner <> current_user",
                     "d.deptype='e'", "p.probin='laplace_pg'", "p.prosrc=target.symbol",
                     "NOT p.prosecdef", "i.indisvalid AND i.indisready"):
            self.assertIn(part,program)
        self.assertEqual(receipt['package_id'],self.package['package_id'])
        self.assertEqual(receipt['receipt_sha256'],runner.document_identity(receipt,'receipt_sha256'))
        self.assertEqual(sql.call_count,1)
    def test_unverified_migration_never_executes_database_commands(self):
        self.path.write_text('tampered\n')
        with mock.patch.object(runner,'runner_sql') as sql:
            with self.assertRaisesRegex(runner.RunnerActivationError,'migration bytes differ'):
                runner.reconcile_indexed_cognition(self.plan,self.contract,self.package)
            sql.assert_not_called()
    def test_duplicate_or_symlinked_package_script_is_rejected(self):
        self.package['files'] *= 2
        with mock.patch.object(runner,'runner_sql') as sql:
            with self.assertRaises(runner.RunnerActivationError):
                runner.reconcile_indexed_cognition(self.plan,self.contract,self.package)
            sql.assert_not_called()
        self.package['files'] = self.package['files'][:1]
        saved=self.path.with_suffix('.saved');self.path.rename(saved);self.path.symlink_to(saved)
        with mock.patch.object(runner,'runner_sql') as sql:
            with self.assertRaises(runner.RunnerActivationError):
                runner.reconcile_indexed_cognition(self.plan,self.contract,self.package)
            sql.assert_not_called()
    def test_failed_or_wrong_version_readback_cannot_publish_success(self):
        with mock.patch.object(runner,'runner_sql',side_effect=runner.RunnerActivationError('SQL failed')):
            with self.assertRaisesRegex(runner.RunnerActivationError,'SQL failed'):
                runner.reconcile_indexed_cognition(self.plan,self.contract,self.package)
        for field,value in (('version','1.0.0'),('native_bindings',0),('ready_indexes',0),('owner','foreign')):
            with self.subTest(field=field), mock.patch.object(runner,'runner_sql',
                    return_value=(dict(self.result,**{field:value}),{})):
                with self.assertRaisesRegex(runner.RunnerActivationError,'result differs'):
                    runner.reconcile_indexed_cognition(self.plan,self.contract,self.package)
    def test_fresh_and_update_use_one_delta_without_world_reset(self):
        extension = ROOT/'integrations/postgresql/extension'
        cmake=(extension/'CMakeLists.txt').read_text()
        delta=(extension/'observation_cognition_persisted.sql.in').read_text()
        self.assertIn('${previous_schema}\\n${persisted_cognition_delta}',cmake)
        self.assertIn('WHERE physicality_type = 1;',delta)
        self.assertIn('WITH (fastupdate = off)',delta)
        for forbidden in ('TRUNCATE ','DROP TABLE ','CREATE TABLE ','GRANT ','SECURITY DEFINER'):
            self.assertNotIn(forbidden,delta)
        required=json.loads((ROOT/'contracts/product-package.json').read_text())['package']['required_files']
        self.assertIn(self.relative,required)
        source=(ROOT/'tools/delivery/product_activation_runner.py').read_text()
        self.assertLess(source.index('indexed_cognition = reconcile_indexed_cognition('),
                        source.index('public_readback = reconcile_public_readback('))
if __name__=='__main__': unittest.main(verbosity=2)
