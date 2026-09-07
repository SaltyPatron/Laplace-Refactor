from pathlib import Path


def replace(path, old, new):
    p=Path(path); text=p.read_text()
    if text.count(old)!=1:
        raise RuntimeError(f'expected one unchanged source boundary: {path}: {old[:70]!r}')
    p.write_text(text.replace(old,new))

core='tools/postgresql/cluster_core.py'
replace(core,
 'def render_postgresql_conf(contract: dict[str, Any], package_root: str, settings: dict[str, str]) -> str:\n    instance = contract["instance"]',
 '''def configuration_version(plan: dict[str, Any]) -> int:
    # Missing version denotes the retained pre-perfcache configuration format.
    # Historical plans remain verifiable; all newly built plans select version 2.
    value = plan.get("configuration_version", 1)
    if type(value) is not int or value not in (1, 2):
        raise ClusterError("unsupported PostgreSQL configuration version")
    return value


def render_postgresql_conf(
    contract: dict[str, Any], package_root: str, settings: dict[str, str],
    *, configuration_version: int = 2,
) -> str:
    if type(configuration_version) is not int or configuration_version not in (1, 2):
        raise ClusterError("unsupported PostgreSQL configuration version")
    if "laplace.perfcache_root" in settings:
        raise ClusterError("perfcache root belongs to the cluster path contract")
    instance = contract["instance"]''')
replace(core,'    config.update(settings)\n    quoted = {',
 '''    config.update(settings)
    if configuration_version >= 2:
        config["laplace.perfcache_root"] = require_absolute_path(
            instance["perfcache_directory"], "instance.perfcache_directory")
    quoted = {
        "laplace.perfcache_root",''')
replace(core,'    plan_core = {\n        "schema": PLAN_SCHEMA,',
 '    plan_core = {\n        "schema": PLAN_SCHEMA,\n        "configuration_version": 2,')
replace(core,'    files = plan.get("files")\n',
 '    configuration_version(plan)\n    files = plan.get("files")\n')

runner='tools/postgresql/clusterctl.py'
replace(runner,'    expected_config = render_postgresql_conf(contract, plan["package_root"], settings)',
 '''    expected_config = render_postgresql_conf(
        contract, plan["package_root"], settings,
        configuration_version=configuration_version(plan))''')
replace(runner,'                      render_postgresql_conf(contract, selected["package_root"], settings),',
 '''                      render_postgresql_conf(
                          contract, selected["package_root"], settings,
                          configuration_version=configuration_version(selected)),''')

unicode='tools/postgresql/unicodectl.py'
replace(unicode,'def render_inspection_sql() -> str:\n',
 '''def validate_perfcache_root(inspection: dict[str, Any], expected_root: str) -> None:
    # Check the effective server setting, not a session-local override. The
    # generated cluster configuration owns it for every backend and after restart.
    if inspection.get("perfcache_root") != expected_root:
        raise UnicodeActivationError(
            "installed PostgreSQL perfcache root differs from the cluster contract; "
            "activate the generated cluster configuration before Unicode production")


def render_inspection_sql() -> str:
''')
replace(unicode,"    return \"\"\"SELECT json_build_object(\n  'sequence', active.sequence,",
 "    return \"\"\"SELECT json_build_object(\n  'perfcache_root', current_setting('laplace.perfcache_root', true),\n  'sequence', active.sequence,")
replace(unicode,'    mode = validate_inspection(inspection, activation_contract, identities)',
 '''    validate_perfcache_root(
        inspection, str(prefixed(root, instance["perfcache_directory"])))
    mode = validate_inspection(inspection, activation_contract, identities)''')
replace('tests/unicode_product_activation_tests.py',
 '            fresh = {\n                "sequence": 0,',
 '''            fresh = {
                "perfcache_root": str(unicode.prefixed(
                    root, self.cluster["instance"]["perfcache_directory"])),
                "sequence": 0,''')
replace('tools/tests/verify-requirements.sh',
 'python3 "$repo_root/tests/product_cluster_upgrade_tests.py"',
 '''python3 "$repo_root/tests/product_cluster_upgrade_tests.py"
python3 "$repo_root/tests/postgresql_perfcache_configuration_tests.py"''')
