#!/usr/bin/env python3
"""Teach canonical product activation to recover only proven dangling product pointers."""

from __future__ import annotations

from pathlib import Path

RECONCILE = Path("tools/delivery/product_activation_reconcile.py")
TESTS = Path("tests/product_cluster_upgrade_tests.py")


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> int:
    source = RECONCILE.read_text(encoding="utf-8")
    if "def recover_dangling_product_pointers(" not in source:
        marker = "\ndef _round_allocation(size: int, unit: int) -> int:\n"
        helper = r'''

def recover_dangling_product_pointers(
    contract: dict[str, Any], root: Path = Path("/")
) -> dict[str, Any] | None:
    """Remove only canonical product links whose immutable release is provably absent.

    A symlink is product state, not evidence that its target exists.  If both active
    pointers name one canonical content-addressed release under the declared release
    root and that target is absent, preserve a receipt and clear the stale pointers so
    normal fresh activation can publish a verified successor.  Existing targets,
    foreign paths, mismatched package ids, and non-symlink collisions fail closed.
    """
    release_root = runner.clusterctl.prefixed(
        root, contract["package"]["release_root"]
    )
    release_root.mkdir(parents=True, exist_ok=True, mode=0o2755)
    release_root_resolved = release_root.resolve()
    logical_links = (
        contract["package"]["active_link"],
        runner.clusterctl.RUNTIME_LINK,
    )
    observations: list[dict[str, Any]] = []
    package_ids: set[str] = set()
    for logical in logical_links:
        link = runner.clusterctl.prefixed(root, logical)
        if not link.exists() and not link.is_symlink():
            continue
        if not link.is_symlink():
            raise runner.RunnerActivationError(
                f"product pointer recovery refuses non-symlink state: {link}"
            )
        raw = os.readlink(link)
        target = (link.parent / raw).resolve(strict=False)
        try:
            relative = target.relative_to(release_root_resolved)
        except ValueError as error:
            raise runner.RunnerActivationError(
                f"product pointer recovery refuses release-root escape: {link} -> {raw}"
            ) from error
        if (
            len(relative.parts) != 1
            or release_capacity.PACKAGE_ID.fullmatch(relative.name) is None
        ):
            raise runner.RunnerActivationError(
                f"product pointer recovery refuses noncanonical release: {link} -> {raw}"
            )
        if target.exists() or target.is_symlink():
            return None
        package_ids.add(relative.name)
        observations.append(
            {
                "logical_path": logical,
                "physical_path": str(link),
                "link_target": raw,
                "resolved_target": str(target),
                "target_absent": True,
            }
        )

    if not observations:
        return None
    if len(package_ids) != 1:
        raise runner.RunnerActivationError(
            "dangling product pointers disagree on predecessor package identity"
        )
    package_id = next(iter(package_ids))
    for observation in observations:
        link = Path(observation["physical_path"])
        if not link.is_symlink() or os.readlink(link) != observation["link_target"]:
            raise runner.RunnerActivationError(
                "dangling product pointer changed during recovery"
            )
    for observation in observations:
        Path(observation["physical_path"]).unlink()

    receipt: dict[str, Any] = {
        "schema": "laplace.dangling-product-pointer-recovery/v1",
        "phase": "cleared-proven-dangling-pointers",
        "package_id": package_id,
        "release_root": str(release_root_resolved),
        "pointers": observations,
    }
    receipt["receipt_sha256"] = runner.document_identity(receipt, "receipt_sha256")
    receipt_path = (
        Path(contract["instance"]["receipt_directory"])
        / "pointer-recovery"
        / package_id
        / "recovery.json"
    )
    runner.write_json(receipt_path, receipt)
    receipt["receipt_path"] = str(receipt_path)
    return receipt
'''
        replace_once(RECONCILE, marker, helper + marker)

        old = '''    contract = runner.clusterctl.load_json(contract_path)
    active = Path(contract["package"]["active_link"])
    if active.exists() or active.is_symlink():
'''
        new = '''    contract = runner.clusterctl.load_json(contract_path)
    recover_dangling_product_pointers(contract)
    active = Path(contract["package"]["active_link"])
    if active.exists() or active.is_symlink():
'''
        replace_once(RECONCILE, old, new)

    tests = TESTS.read_text(encoding="utf-8")
    if "test_dangling_canonical_product_pointers_are_receipted_and_cleared" not in tests:
        anchor = '''    def test_reconciler_routes_active_and_fresh_state_to_distinct_lifecycles(self) -> None:
'''
        test_code = r'''    def test_dangling_canonical_product_pointers_are_receipted_and_cleared(self) -> None:
        release_root = self.root / "releases"
        release_root.mkdir()
        runtime = self.root / "runtime-link"
        contract = {
            "package": {
                "active_link": str(self.active),
                "release_root": str(release_root),
            },
            "instance": {"receipt_directory": str(self.root / "receipts")},
        }
        self.active.symlink_to(f"releases/{self.old_id}")
        runtime.symlink_to(f"releases/{self.old_id}")
        with mock.patch.object(RECONCILE.runner.clusterctl, "RUNTIME_LINK", str(runtime)):
            receipt = RECONCILE.recover_dangling_product_pointers(contract, self.root)
        self.assertIsNotNone(receipt)
        self.assertEqual(receipt["package_id"], self.old_id)
        self.assertFalse(self.active.exists())
        self.assertFalse(self.active.is_symlink())
        self.assertFalse(runtime.exists())
        self.assertFalse(runtime.is_symlink())
        persisted = Path(receipt["receipt_path"])
        self.assertTrue(persisted.is_file())

    def test_dangling_pointer_recovery_refuses_foreign_or_live_targets(self) -> None:
        release_root = self.root / "releases"
        release_root.mkdir()
        runtime = self.root / "runtime-link"
        contract = {
            "package": {
                "active_link": str(self.active),
                "release_root": str(release_root),
            },
            "instance": {"receipt_directory": str(self.root / "receipts")},
        }
        self.active.symlink_to("../foreign")
        with mock.patch.object(RECONCILE.runner.clusterctl, "RUNTIME_LINK", str(runtime)):
            with self.assertRaisesRegex(
                RECONCILE.runner.RunnerActivationError, "release-root escape"
            ):
                RECONCILE.recover_dangling_product_pointers(contract, self.root)
        self.active.unlink()
        live = release_root / self.old_id
        live.mkdir()
        self.active.symlink_to(f"releases/{self.old_id}")
        with mock.patch.object(RECONCILE.runner.clusterctl, "RUNTIME_LINK", str(runtime)):
            self.assertIsNone(RECONCILE.recover_dangling_product_pointers(contract, self.root))
        self.assertTrue(self.active.is_symlink())

'''
        replace_once(TESTS, anchor, test_code + anchor)

    print("enabled fail-closed receipted dangling product-pointer recovery")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
