"""JIRA 失败原因展示及 ECR 双项目查询；使用模拟传输，不访问真实服务。"""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import generate_release_notes as core
from test_jira_client import Reply, Transport, issue


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="release-diagnostics-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.keys = ["ECR6600-1", "ECR6600A-2"]

    def fetch(self, filename):
        path = TOOLS / "examples" / filename
        config, auth = core.jira_client.load_jira_config(path, environ={
            "ECW_JIRA_USER": "test-user", "ECW_JIRA_PASSWORD": "secret-must-not-escape"})
        transport = Transport([Reply(issue(key)) for key in self.keys])
        with mock.patch.object(core.jira_client.request, "build_opener", return_value=transport):
            report = core.jira_client.fetch_issues(self.keys, config, auth)
        return path, report, transport

    def test_ecw_default_reproduces_missing_ecr_projects(self):
        path, report, transport = self.fetch("jira-config.example.json")
        self.assertEqual(transport.calls, [])
        self.assertEqual(report["status"], "unavailable")
        with self.assertRaises(core.ReleaseError) as failure:
            core.check_jira_complete(report, self.root, path)
        text = str(failure.exception)
        for value in ("project_not_configured: 2", "缺少项目：ECR6600、ECR6600A", str(path.resolve()), "--project-config"):
            self.assertIn(value, text)
        review = (self.root / "coverage-review.md").read_text(encoding="utf-8")
        self.assertIn("| ECR6600A-2 | project_not_configured |  | 0 |", review)
        self.assertNotIn("secret-must-not-escape", text + review)

    def test_ecr_example_queries_both_projects_and_flags_unknown_type(self):
        _, report, transport = self.fetch("jira-config.ecr.example.json")
        self.assertEqual(len(transport.calls), 2)
        self.assertEqual(report["status"], "complete")
        self.assertEqual(set(report["configSnapshot"]["projects"]), {"ECR6600", "ECR6600A"})
        for row in report["results"].values():
            # 传输样本故意使用未配置的类型 1；不能因补齐已知类型而猜测未知类型。
            self.assertEqual(row["status"], "fetched")
            self.assertIn("unmapped_issue_type", row["warnings"])
        core.check_jira_complete(report, self.root)
        self.assertFalse((self.root / "coverage-review.md").exists())

    def test_distinct_failures_keep_reason_http_attempts_and_remedy(self):
        path, original, _ = self.fetch("jira-config.ecr.example.json")
        for reason, code, hint in (("forbidden", 403, "浏览权限"), ("not_found_or_not_visible", 404, "同一账号"),
                                   ("auth_failed", 401, "验证码"), ("budget_exhausted", None, "totalBudgetSeconds"),
                                   ("timeout", None, "timeoutSeconds"), ("tls_error", None, "caFile"),
                                   ("network_error", None, "DNS"), ("rate_limited", 429, "限流")):
            with self.subTest(reason=reason):
                report = copy.deepcopy(original)
                report["status"] = "partial"
                report["results"][self.keys[1]].update(status="failed", reasonCode=reason, httpStatus=code,
                                                     attempts=2, fields=None)
                report["counts"].update(fetched=1, failed=1, attempts=3)
                with self.assertRaises(core.ReleaseError) as failure:
                    core.check_jira_complete(report, self.root, path)
                self.assertIn(reason + ": 1", str(failure.exception))
                self.assertIn(hint, str(failure.exception))
                review = (self.root / "coverage-review.md").read_text(encoding="utf-8")
                self.assertIn(f"| ECR6600A-2 | {reason} | {code or ''} | 2 |", review)
                self.assertNotIn("secret-must-not-escape", str(failure.exception) + review)

    def test_truncated_summary_is_not_misreported_as_project_failure(self):
        _, report, _ = self.fetch("jira-config.ecr.example.json")
        report["results"][self.keys[0]]["truncatedFields"] = ["summary"]
        with self.assertRaises(core.ReleaseError) as failure:
            core.check_jira_complete(report, self.root)
        self.assertIn("summary_truncated: 1", str(failure.exception))
        self.assertIn("JIRA 快照", str(failure.exception))
        self.assertNotIn("缺少项目", str(failure.exception))

    def test_error_is_bounded_but_report_lists_every_failed_issue(self):
        path, report, _ = self.fetch("jira-config.example.json")
        row = report["results"][self.keys[0]]
        report["results"] = {f"ECR6600-{i}": {**row, "requestedKey": f"ECR6600-{i}"} for i in range(1, 101)}
        report["counts"].update(keys=100, skipped=100)
        with self.assertRaises(core.ReleaseError) as failure:
            core.check_jira_complete(report, self.root, path)
        self.assertIn("project_not_configured: 100", str(failure.exception))
        self.assertNotIn("ECR6600-100", str(failure.exception))
        self.assertIn("| ECR6600-100 |", (self.root / "coverage-review.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
