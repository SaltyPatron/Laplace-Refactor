#!/usr/bin/env python3
"""Generated cache-root binding, retained-plan compatibility and fail-fast proof."""
from __future__ import annotations

import copy
import unittest
from unittest import mock

import postgresql_cluster_tests as fixture
import unicode_product_activation_tests as unicode_fixture

cluster = fixture.clusterctl
unicode = unicode_fixture.unicode


class PerfcacheConfigurationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = fixture.PostgreSQLClusterContract()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.contract = self.fixture.contract
        self.plan = self.fixture.plan()
        self.path = f"{self.plan['instance']['config_directory']}/postgresql.conf"
        self.root = self.contract['instance']['perfcache_directory']

    def config(self, plan):
        return next(entry for entry in plan['files'] if entry['path'] == self.path)

    def rehash(self, plan):
        entry = self.config(plan)
        entry['sha256'] = cluster.sha256_bytes(entry['content'].encode('utf-8'))
        fixture.rehash_plan(plan)

    def historical(self):
        plan = copy.deepcopy(self.plan)
        plan.pop('configuration_version')
        self.config(plan)['content'] = cluster.render_postgresql_conf(
            self.contract, plan['package_root'], plan['settings'],
            configuration_version=1)
        self.rehash(plan)
        return plan

    def test_new_plan_binds_exact_root_for_all_postgresql_backends(self):
        line = f"laplace.perfcache_root = {cluster.sql_literal(self.root)}\n"
        self.assertEqual(self.config(self.plan)['content'].count(line), 1)
        self.assertEqual(self.plan['configuration_version'], 2)
        cluster.validate_plan(self.plan, self.contract)

    def test_removed_root_cannot_be_hidden_by_rehashing_current_plan(self):
        plan = copy.deepcopy(self.plan)
        self.config(plan)['content'] = self.config(plan)['content'].replace(
            f"laplace.perfcache_root = {cluster.sql_literal(self.root)}\n", '')
        self.rehash(plan)
        with self.assertRaisesRegex(cluster.ClusterError, 'configuration differs'):
            cluster.validate_plan(plan, self.contract)

    def test_foreign_root_cannot_be_hidden_by_rehashing(self):
        plan = copy.deepcopy(self.plan)
        self.config(plan)['content'] = self.config(plan)['content'].replace(
            f"laplace.perfcache_root = {cluster.sql_literal(self.root)}",
            "laplace.perfcache_root = '/tmp/foreign'")
        self.rehash(plan)
        with self.assertRaisesRegex(cluster.ClusterError, 'configuration differs'):
            cluster.validate_plan(plan, self.contract)

    def test_historical_plan_retains_exact_old_bytes_and_identity(self):
        old = self.historical()
        before = copy.deepcopy(old)
        cluster.validate_plan(old, self.contract)
        self.assertEqual(old, before)
        selected = cluster.plan_with_physical_settings(
            old, self.contract, {'huge_pages': 'off'})
        self.assertEqual(cluster.configuration_version(selected), 1)
        self.assertNotIn('laplace.perfcache_root', self.config(selected)['content'])
        cluster.validate_plan(selected, self.contract)
        successor = cluster.plan_with_physical_settings(
            self.plan, self.contract, {'huge_pages': 'off'})
        self.assertEqual(cluster.configuration_version(successor), 2)
        self.assertIn('laplace.perfcache_root', self.config(successor)['content'])

    def test_version_downgrade_without_historical_bytes_is_rejected(self):
        plan = copy.deepcopy(self.plan)
        plan.pop('configuration_version')
        fixture.rehash_plan(plan)
        with self.assertRaisesRegex(cluster.ClusterError, 'configuration differs'):
            cluster.validate_plan(plan, self.contract)

    def test_unknown_or_untyped_configuration_version_is_rejected(self):
        for version in (0, 3, True, '2', None):
            with self.subTest(version=version):
                plan = copy.deepcopy(self.plan)
                plan['configuration_version'] = version
                fixture.rehash_plan(plan)
                with self.assertRaisesRegex(cluster.ClusterError, 'configuration version'):
                    cluster.validate_plan(plan, self.contract)

    def test_physical_settings_cannot_override_cache_authority(self):
        settings = dict(self.plan['settings'], **{'laplace.perfcache_root': '/tmp'})
        with self.assertRaisesRegex(cluster.ClusterError, 'path contract'):
            cluster.render_postgresql_conf(self.contract, self.plan['package_root'], settings)

    def test_configuration_quotes_exact_root(self):
        contract = copy.deepcopy(self.contract)
        contract['instance']['perfcache_directory'] = "/opt/laplace/cache's"
        rendered = cluster.render_postgresql_conf(
            contract, self.plan['package_root'], self.plan['settings'])
        self.assertIn("laplace.perfcache_root = '/opt/laplace/cache''s'\n", rendered)

    def test_preflight_rejects_missing_relative_foreign_and_sibling_roots(self):
        for value in (None, '', 'relative', '/', self.root + '-foreign'):
            with self.subTest(value=value):
                with self.assertRaisesRegex(unicode.UnicodeActivationError, 'perfcache root'):
                    unicode.validate_perfcache_root({'perfcache_root': value}, self.root)
        unicode.validate_perfcache_root({'perfcache_root': self.root}, self.root)
        self.assertIn("current_setting('laplace.perfcache_root', true)",
                      unicode.render_inspection_sql())

    def test_absent_root_stops_actual_orchestration_before_producer_directories(self):
        case = unicode_fixture.UnicodeProductActivationTests()
        case.setUp()
        validate = unicode.validate_perfcache_root

        def missing(_inspection, expected):
            return validate({'perfcache_root': None}, expected)

        with mock.patch.object(unicode, 'validate_perfcache_root', side_effect=missing), \
             mock.patch.object(unicode, 'create_work_directories') as created:
            with self.assertRaisesRegex(unicode.UnicodeActivationError, 'perfcache root'):
                case.test_success_receipt_requires_restart_and_cold_public_readback()
        created.assert_not_called()


if __name__ == '__main__':
    unittest.main(verbosity=2)
