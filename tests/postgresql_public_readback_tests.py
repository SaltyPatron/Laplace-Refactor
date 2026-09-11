#!/usr/bin/env python3
"""Test the generated native read boundary and installed reconciliation owner."""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('readback_runner_tests', ROOT/'tools/delivery/product_activation_runner.py')
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)


def render(destination: Path) -> str:
    args = [ROOT/'contracts/isa.json', ROOT/'contracts/persistence.json',
            ROOT/'contracts/composition.json', ROOT/'contracts/unicode-postgresql.json',
            ROOT/'contracts/unicode-root-stream.json', ROOT/'cmake/postgresql_bindings.h.in',
            ROOT/'integrations/postgresql/extension/laplace--version.sql.in',
            ROOT/'integrations/postgresql/extension/laplace.control.in',
            destination/'bindings.h', destination/'laplace--1.0.0.sql', destination/'laplace.control']
    program = destination/'render.cmake'
    program.write_text('set(PROJECT_VERSION "1.0.0")\n'
        + f'include("{ROOT}/cmake/LaplacePostgreSQLBindings.cmake")\n'
        + 'laplace_configure_postgresql_bindings(\n'
        + '\n'.join(f'"{arg}"' for arg in args) + ')\n')
    subprocess.run(['cmake','-P',str(program)],check=True,capture_output=True,text=True)
    return (destination/'laplace-public-readback.sql').read_text()


class PublicReadbackBindings(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sql = render(self.root)
        self.relative = 'pgsql-18/share/extension/laplace-public-readback.sql'
        self.path = self.root/self.relative
        self.path.parent.mkdir(parents=True)
        self.path.write_text(self.sql)
        self.plan = {'package_root':str(self.root), 'postgresql_major':18}
        self.package = {'package_id':'ab'*32, 'files':[{'path':self.relative,'kind':'file',
                        'sha256':runner.clusterctl.sha256_file(self.path)}]}
        self.contract = {'instance':{'admin_role':'laplace_admin'}}

    def test_fresh_and_existing_installations_use_identical_generated_program(self):
        self.assertNotIn('@LAPLACE_', self.sql)
        self.assertTrue((self.root/'laplace--1.0.0.sql').read_text().endswith(self.sql))
        required = json.loads((ROOT/'contracts/product-package.json').read_text())['package']['required_files']
        self.assertIn(self.relative, required)
        self.assertIn('laplace-public-readback.sql', (ROOT/'integrations/postgresql/extension/CMakeLists.txt').read_text())
        for name in ('unicode_tier0_resolve_batch','unicode_identity_reverse_resolve_batch','highway_registry_resolve_active'):
            self.assertIn('laplace.'+name+'(',self.sql)
        for condition in ('SECURITY DEFINER', 'SET search_path TO pg_catalog, pg_temp',
                          'FROM PUBLIC','p.proowner = e.extowner', 'n.nspowner = e.extowner',
                          "p.probin = 'laplace_pg'", 'p.prosrc = target.symbol', "d.deptype = 'e'",
                          "p.provolatile = 's'", 'NOT p.proleakproof'):
            self.assertIn(condition,self.sql)
        self.assertNotIn('GRANT ',self.sql)
        self.assertNotIn('ALTER ROLE',self.sql)

    def test_existing_installation_applies_verified_program_in_one_transaction(self):
        outcome={'schema':'laplace.public-readback-bindings/v1','functions':3,'owner':'laplace_admin'}
        with mock.patch.object(runner,'runner_sql', return_value=(outcome,{'exit_code':0})) as execute:
            receipt=runner.reconcile_public_readback(self.plan,self.contract,self.package)
        args=execute.call_args.args
        self.assertTrue(args[2].startswith('BEGIN;'))
        self.assertTrue(args[2].endswith('COMMIT;\n'))
        self.assertIn(self.sql,args[2])
        self.assertEqual(args[4:6],('laplace-runner','laplace_admin'))
        self.assertEqual(receipt['script_sha256'],self.package['files'][0]['sha256'])
        self.assertEqual(receipt['receipt_sha256'],runner.document_identity(receipt,'receipt_sha256'))

    def test_missing_tampered_symlinked_or_duplicate_script_never_executes(self):
        with mock.patch.object(runner,'runner_sql') as execute:
            self.path.write_text(self.sql+'-- unauthorized bytes\n')
            with self.assertRaisesRegex(runner.RunnerActivationError,'binding bytes differ'):
                runner.reconcile_public_readback(self.plan,self.contract,self.package)
            self.path.unlink()
            with self.assertRaises(runner.RunnerActivationError):
                runner.reconcile_public_readback(self.plan,self.contract,self.package)
            self.path.symlink_to(self.root/'laplace-public-readback.sql')
            with self.assertRaises(runner.RunnerActivationError):
                runner.reconcile_public_readback(self.plan,self.contract,self.package)
            self.path.unlink();self.path.write_text(self.sql)
            self.package['files'] *= 2
            with self.assertRaises(runner.RunnerActivationError):
                runner.reconcile_public_readback(self.plan,self.contract,self.package)
            execute.assert_not_called()

    def test_database_error_or_wrong_owner_cannot_publish_success(self):
        with mock.patch.object(runner,'runner_sql',side_effect=runner.RunnerActivationError('SQL failure')):
            with self.assertRaisesRegex(runner.RunnerActivationError,'SQL failure'):
                runner.reconcile_public_readback(self.plan,self.contract,self.package)
        with mock.patch.object(runner,'runner_sql',return_value=({'schema':'laplace.public-readback-bindings/v1',
                            'functions':3,'owner':'laplace_app'},{'exit_code':0})):
            with self.assertRaisesRegex(runner.RunnerActivationError,'result differs'):
                runner.reconcile_public_readback(self.plan,self.contract,self.package)

    def test_materialization_route_is_compiled_and_reconciled(self):
        cmake = (ROOT/'integrations/postgresql/extension/CMakeLists.txt').read_text()
        route = (ROOT/'integrations/postgresql/extension/cognition_materialization_route.sql').read_text()
        wrapper = (ROOT/'integrations/postgresql/extension/src/cognition_materialization_route_pg.c').read_text()
        self.assertIn('src/cognition_materialization_route_pg.c', cmake)
        self.assertIn('cognition_materialization_route.sql', cmake)
        self.assertIn('${cognition_materialization_route}\\n${public_readback_reconcile_bindings}', cmake)
        self.assertIn('file(APPEND "${product_cognition_upgrade_sql}"', cmake)
        self.assertIn('laplace.cognition_realization_materialize_utf8(', route)
        self.assertIn("'laplace_pg_cognition_realization_materialize_utf8'", route)
        self.assertIn('ALTER EXTENSION laplace ADD FUNCTION', route)
        self.assertIn('REVOKE ALL ON FUNCTION', route)
        self.assertNotIn('GRANT ', route)
        self.assertIn('laplace_pg_materialization_provider_create', wrapper)
        self.assertIn('laplace_cognition_realization_materialize_utf8(', wrapper)
        self.assertIn('laplace_pg_materialization_provider_take_error', wrapper)
        self.assertNotIn('SELECT physicality_id', wrapper)
        self.assertNotIn('laplace_pg_perfcache_pin_active', wrapper)

    def test_migration_is_before_unicode_and_highway_activation(self):
        import inspect
        program=inspect.getsource(runner.execute)
        self.assertLess(program.index('reconcile_public_readback('), program.index('unicodectl.execute_unicode_activation('))
        self.assertLess(program.index('unicodectl.execute_unicode_activation('), program.index('highwayctl.execute_highway_activation('))


if __name__=='__main__': unittest.main(verbosity=2)
