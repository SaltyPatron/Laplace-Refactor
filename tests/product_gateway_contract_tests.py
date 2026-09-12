#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "tools/openai_api_compat.py"
SOURCE = ROOT / "tools/source_product.py"
WEB = ROOT / "product/web/app.js"
LIVE = ROOT / "tools/delivery/product_gateway_live_proof.py"
WORKFLOW = ROOT / ".github/workflows/product-cognition-multiturn.yml"
PACKAGE = ROOT / "contracts/product-package.json"


class ProductGatewayContractTests(unittest.TestCase):
    def test_chat_history_and_streaming_are_shipped_and_live_proven(self) -> None:
        wrapper = WRAPPER.read_text(encoding="utf-8")
        live = LIVE.read_text(encoding="utf-8")
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn('MESSAGE_ROLES = {"system", "developer", "user", "assistant"}', wrapper)
        self.assertIn('"object": "chat.completion.chunk"', wrapper)
        self.assertIn('self.wfile.write(b"data: [DONE]', wrapper)
        self.assertIn('"message_count": len(chat_messages)', live)
        self.assertIn('"stream": True', live)
        self.assertIn('.openai_chat.roles == ["system","user","assistant","user"]', workflow)
        self.assertIn('.openai_chat.streaming.done == true', workflow)

    def test_source_ui_cannot_submit_caller_server_paths(self) -> None:
        wrapper = WRAPPER.read_text(encoding="utf-8")
        web = WEB.read_text(encoding="utf-8")
        self.assertIn('"caller_supplied_server_path": False', wrapper)
        self.assertIn('/api/v1/source-catalog', web)
        self.assertIn('/api/v1/sources/preflight', web)
        self.assertIn('/api/v1/source-jobs/', web)
        self.assertNotIn('source_root:', web)
        self.assertNotIn('unicode_root:', web)

    def test_source_job_is_persisted_before_worker_launch(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        write = source.index("atomic_write_json(job_path, job)")
        launch = source.index("subprocess.Popen(", write)
        self.assertLess(write, launch)
        self.assertIn('job_id = sha256_bytes((expected_plan_id + "\\0" + key).encode("utf-8"))', source)
        self.assertIn('plan = preflight(source_id, metadata, estate)', source)
        self.assertIn('if plan["plan_id"] != expected_plan_id:', source)

    def test_package_requires_new_product_owners(self) -> None:
        package = PACKAGE.read_text(encoding="utf-8")
        for required in (
            '"bin/laplace-openai-api-core"',
            '"bin/laplace-source-product"',
            '"bin/laplace-source-job-worker"',
            '"share/laplace/source-contracts/selected-source-boundary.json"',
        ):
            self.assertIn(required, package)

    def test_activated_proof_requires_durable_source_identity(self) -> None:
        live = LIVE.read_text(encoding="utf-8")
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn('idempotent_retry_same_job', live)
        self.assertIn('"result_sha256": result_sha', live)
        self.assertIn('.source_ingestion.idempotent_retry_same_job == true', workflow)
        self.assertIn('.source_ingestion.state == "succeeded"', workflow)
        self.assertIn('.source_ingestion.result_sha256 | test("^[0-9a-f]{64}$")', workflow)


if __name__ == "__main__":
    unittest.main()
