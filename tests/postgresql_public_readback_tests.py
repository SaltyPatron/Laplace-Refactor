#!/usr/bin/env python3
"""Test the generated native read boundary and installed reconciliation owner."""
from __future__ import annotations
import importlib.util
import json
import re
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
        world = (self.root/'world-admission-optional-evidence.sql').read_text()
        self.assertIn(world,self.sql)
        self.assertIn('world_admission_evidence_coverage',world)
        self.assertIn('evidence_lineage_receipt_id IS NULL',world)
        self.assertIn('evidence_testimony_receipt_id IS NULL',world)
        self.assertIn('profile_claim_count>0',world)


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

    def test_packaged_reconciliation_targets_the_current_extension_generation(self):
        extension = ROOT/'integrations/postgresql/extension'
        cmake = (extension/'CMakeLists.txt').read_text()
        # Execute the production packaging block, including the prefix omitted
        # by the base-binding generator used by the fresh-schema tests above.
        packaging = cmake.split('file(READ "${CMAKE_SOURCE_DIR}/contracts/isa.json"', 1)[0]
        destination = self.root/'packaged'
        destination.mkdir()
        script = destination/'package.cmake'
        script.write_text('set(PROJECT_VERSION "1.0.0")\n'
            + f'set(CMAKE_SOURCE_DIR "{ROOT}")\n'
            + f'set(CMAKE_CURRENT_SOURCE_DIR "{extension}")\n'
            + f'set(CMAKE_BINARY_DIR "{destination}")\n'
            + f'set(CMAKE_CURRENT_BINARY_DIR "{destination}")\n' + packaging)
        subprocess.run(['cmake','-P',str(script)],check=True,capture_output=True,text=True)
        program = (destination/'share/extension/laplace-public-readback.sql').read_text()
        current = re.search(r"default_version\s*=\s*'([^']+)'",
            (extension/'laplace.control.in').read_text()).group(1)
        self.assertIn(f"ALTER EXTENSION laplace UPDATE TO '{current}'", program)
        self.assertIn(f"ELSIF version <> '{current}' THEN", program)
        self.assertIn("IF version IN ('1.0.1', '1.0.2', '1.0.3') THEN", program)
        self.assertIn('unsupported product cognition predecessor version', program)
        self.assertIn('owner <> current_user', program)
        required = json.loads((ROOT/'contracts/product-package.json').read_text())['package']['required_files']
        self.assertIn(f'pgsql-18/share/extension/laplace--{current}.sql', required)
        self.assertIn(f'pgsql-18/share/extension/laplace--1.0.3--{current}.sql', required)
        fixture = (extension/'tests/observation_cognition_contract.sql.in').read_text()
        self.assertEqual(fixture.count('\\ir @CMAKE_BINARY_DIR@/integrations/postgresql/extension/share/extension/laplace-public-readback.sql'), 2)

    def test_committed_revalidation_type_and_binding_upgrade_are_exact_and_private(self):
        declaration = 'CREATE TYPE laplace.highway_registry_revalidation_result AS ('
        generated = (self.root/'laplace--1.0.0.sql').read_text()
        expected = generated[generated.index(declaration):].split(';',1)[0]+';'
        self.assertIn(expected,self.sql)
        for guard in ('member_names IS DISTINCT FROM', 'member_types IS DISTINCT FROM',
                      'a.attisdropped', "t.typowner=e.extowner", "p.prosrc='laplace_pg_highway_registry_revalidate_committed'",
                      "NOT p.prosecdef", "d.deptype='e'", 'ALTER EXTENSION laplace ADD FUNCTION',
                      'ALTER EXTENSION laplace ADD TYPE'):
            self.assertIn(guard,self.sql)
        self.assertIn('REVOKE ALL ON FUNCTION laplace.highway_registry_revalidate_committed',self.sql)
        self.assertNotIn('GRANT ',self.sql)

    def test_migration_is_before_unicode_and_highway_activation(self):
        import inspect
        program=inspect.getsource(runner.execute)
        self.assertLess(program.index('reconcile_public_readback('), program.index('unicodectl.execute_unicode_activation('))
        self.assertLess(program.index('unicodectl.execute_unicode_activation('), program.index('highwayctl.execute_highway_activation('))


if __name__=='__main__': unittest.main(verbosity=2)
