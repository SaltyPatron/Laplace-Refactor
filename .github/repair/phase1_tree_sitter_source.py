#!/usr/bin/env python3
from pathlib import Path

contract_path = Path("contracts/product-package.json")
contract = contract_path.read_text(encoding="utf-8")
anchor = '    "blake3_source": "/opt/laplace/external/source-generations/f9fa2f2723d506d5a678f0bba204f67ab6a483ad09509199ce5c3f1a7ef92fd7/blake3/c",\n'
replacement = anchor + '    "tree_sitter_root": "/opt/laplace/external/source-generations/f9fa2f2723d506d5a678f0bba204f67ab6a483ad09509199ce5c3f1a7ef92fd7/tree-sitter",\n'
if contract.count(anchor) != 1:
    raise SystemExit("product contract BLAKE3 source anchor differs")
contract_path.write_text(contract.replace(anchor, replacement, 1), encoding="utf-8")

package_path = Path("tools/product/build-package.py")
package = package_path.read_text(encoding="utf-8")
anchor = '''        "blake3_root",\n        "blake3_source",\n'''
replacement = '''        "blake3_root",\n        "blake3_source",\n        "tree_sitter_root",\n'''
if package.count(anchor) != 1:
    raise SystemExit("product contract absolute-source validation anchor differs")
package = package.replace(anchor, replacement, 1)

anchor = '''    blake3_root = Path(build["blake3_root"])\n    blake3_source = Path(build["blake3_source"])\n    if not blake3_source.is_relative_to(blake3_root) or blake3_source == blake3_root:\n        raise ProductPackageError("BLAKE3 source must be contained by its repository root")\n'''
replacement = '''    blake3_root = Path(build["blake3_root"])\n    blake3_source = Path(build["blake3_source"])\n    tree_sitter_root = Path(build["tree_sitter_root"])\n    if not blake3_source.is_relative_to(blake3_root) or blake3_source == blake3_root:\n        raise ProductPackageError("BLAKE3 source must be contained by its repository root")\n    if (\n        blake3_root.name != "blake3"\n        or tree_sitter_root.name != "tree-sitter"\n        or tree_sitter_root.parent != blake3_root.parent\n    ):\n        raise ProductPackageError(\n            "Tree-sitter source root must share the locked dependency generation with BLAKE3"\n        )\n'''
if package.count(anchor) != 1:
    raise SystemExit("product source-root relationship anchor differs")
package = package.replace(anchor, replacement, 1)

anchor = '''    blake3_root = require_absolute(contract["build"]["blake3_root"], "build.blake3_root")\n    build_input_roots = {\n        "blake3": exact_tree_receipt(blake3_root),\n        **provider_roots,\n'''
replacement = '''    blake3_root = require_absolute(contract["build"]["blake3_root"], "build.blake3_root")\n    tree_sitter_root = require_absolute(\n        contract["build"]["tree_sitter_root"], "build.tree_sitter_root"\n    )\n    build_input_roots = {\n        "blake3": exact_tree_receipt(blake3_root),\n        "tree-sitter": exact_tree_receipt(tree_sitter_root),\n        **provider_roots,\n'''
if package.count(anchor) != 1:
    raise SystemExit("product build-input source-root anchor differs")
package = package.replace(anchor, replacement, 1)

anchor = '''        f"-DLAPLACE_BLAKE3_SOURCE={build['blake3_source']}",\n        f"-DLAPLACE_DEPENDENCY_LOCK={repository / 'dependencies/lock.json'}",\n'''
replacement = '''        f"-DLAPLACE_BLAKE3_SOURCE={build['blake3_source']}",\n        f"-DLAPLACE_TREE_SITTER_SOURCE={plan['build_input_roots']['tree-sitter']['path']}",\n        f"-DLAPLACE_DEPENDENCY_LOCK={repository / 'dependencies/lock.json'}",\n'''
if package.count(anchor) != 1:
    raise SystemExit("product CMake source argument anchor differs")
package = package.replace(anchor, replacement, 1)
package_path.write_text(package, encoding="utf-8")

tests_path = Path("tests/product_package_tests.py")
tests = tests_path.read_text(encoding="utf-8")
marker = '''    def test_blake3_revision_root_cannot_be_collapsed_to_source_subtree(self) -> None:\n'''
if tests.count(marker) != 1:
    raise SystemExit("product source contract test anchor differs")
addition = '''    def test_tree_sitter_source_root_cannot_be_omitted(self) -> None:\n        mutant = copy.deepcopy(self.contract)\n        mutant["build"].pop("tree_sitter_root")\n        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "build.tree_sitter_root"):\n            PACKAGE.validate_contract(mutant)\n\n    def test_tree_sitter_source_must_share_locked_dependency_generation(self) -> None:\n        mutant = copy.deepcopy(self.contract)\n        mutant["build"]["tree_sitter_root"] = "/opt/laplace/external/other/tree-sitter"\n        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "locked dependency generation"):\n            PACKAGE.validate_contract(mutant)\n\n'''
tests = tests.replace(marker, addition + marker, 1)

anchor = '''        provider = self.root / "provider"\n        for path in (repository, build, stage, toolchain, provider):\n            path.mkdir()\n'''
replacement = '''        provider = self.root / "provider"\n        tree_sitter = self.root / "tree-sitter"\n        for path in (repository, build, stage, toolchain, provider, tree_sitter):\n            path.mkdir()\n'''
if tests.count(anchor) != 1:
    raise SystemExit("sandbox source-root fixture anchor differs")
tests = tests.replace(anchor, replacement, 1)

anchor = '''            "build_input_roots": {\n                "provider": {"path": str(provider)},\n            },\n'''
replacement = '''            "build_input_roots": {\n                "provider": {"path": str(provider)},\n                "tree-sitter": {"path": str(tree_sitter)},\n            },\n'''
if tests.count(anchor) != 1:
    raise SystemExit("sandbox build-input roots anchor differs")
tests = tests.replace(anchor, replacement, 1)

anchor = '''        for path in (repository, toolchain, provider):\n            self.assertIn(["--ro-bind", str(path), str(path)], triples)\n'''
replacement = '''        for path in (repository, toolchain, provider, tree_sitter):\n            self.assertIn(["--ro-bind", str(path), str(path)], triples)\n'''
if tests.count(anchor) != 1:
    raise SystemExit("sandbox read-only source assertion anchor differs")
tests = tests.replace(anchor, replacement, 1)
tests_path.write_text(tests, encoding="utf-8")
