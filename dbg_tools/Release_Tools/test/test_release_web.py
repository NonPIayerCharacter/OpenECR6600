"""本机 HTTP 服务边界与任务生命周期；模拟生成器，不查询企业 Jira。"""
import http.client
import json
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import unittest
from unittest import mock

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import release_notes_web as web


class WebTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ecw-release-web-")
        self.root = Path(self.temp.name)
        shutil.copytree(TOOLS / "web", self.root / "web")
        shutil.copy2(TOOLS / "release-project.json", self.root / "release-project.json")
        self.entered, self.release = threading.Event(), threading.Event()
        self.calls = []
        self.loader = object()
        self.service = web.ReleaseService(self.root, self.root / "runtime", self.root / "config.json",
                                          self.loader, generator=self.generate)
        self.server = web.create_server(self.service, 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.service.close()
        self.temp.cleanup()

    def generate(self, options, tools, *, jira_loader, progress):
        self.calls.append((options, tools, jira_loader))
        progress("查询 JIRA")
        self.entered.set()
        if not self.release.wait(10):
            raise RuntimeError("test gate timed out")
        batch = tools / "release" / str(len(self.calls))
        batch.mkdir(parents=True)
        (batch / "Release_Notes_TEST.md").write_text("# 测试\n\n- 【重要】变化.\n", encoding="utf-8")
        (batch / "CHANGELOG.generated.md").write_text("# 全部工程记录\n", encoding="utf-8")
        (batch / "JIRA_Status_Review_TEST.md").write_text("# JIRA 状态核对\n", encoding="utf-8")
        (batch / "private.txt").write_text("not downloadable", encoding="utf-8")
        web.core.write_json(batch / web.core.MANIFEST, {"artifacts": {name: "hash" for name in
                            ("Release_Notes_TEST.md", "CHANGELOG.generated.md", "JIRA_Status_Review_TEST.md", "private.txt")}})
        return {"batch": str(batch), "status": "draft", "importantEntryCount": 1}

    def request(self, method, path, value=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        defaults = {"X-Release-Token": self.service.token, "Content-Type": "application/json"}
        defaults.update(headers or {})
        try:
            body = json.dumps(value).encode() if not isinstance(value, bytes) and value is not None else value
            conn.request(method, path, body, defaults)
            response = conn.getresponse()
            return response.status, response.read(), dict(response.getheaders())
        finally:
            conn.close()

    def start(self):
        status, body, _ = self.request("POST", "/api/jobs", {"from": "BASE", "to": "TARGET"})
        self.assertEqual(status, 202)
        self.assertTrue(self.entered.wait(3))
        return json.loads(body)["id"]

    def finish(self):
        self.release.set()
        self.service.worker.join(5)
        self.assertFalse(self.service.worker.is_alive())

    def test_single_job_progress_download_and_refresh(self):
        job_id = self.start()
        status, body, _ = self.request("GET", "/api/jobs/" + job_id)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["stage"], "查询 JIRA")
        self.assertEqual(self.request("POST", "/api/jobs", {"from": "A", "to": "B"})[0], 409)
        self.finish()
        job = json.loads(self.request("GET", "/api/state")[1])["latest"]
        self.assertEqual(job["status"], "done")
        self.assertEqual(job["id"], job_id)
        self.assertEqual(job["files"], ["CHANGELOG.generated.md", "JIRA_Status_Review_TEST.md", "Release_Notes_TEST.md", web.core.MANIFEST])
        status, body, headers = self.request("GET", f"/api/jobs/{job_id}/files/Release_Notes_TEST.md")
        self.assertEqual(status, 200)
        self.assertIn("【重要】", body.decode())
        self.assertIn("attachment", headers["Content-Disposition"])
        self.assertEqual(headers["X-Content-Type-Options"], "nosniff")
        options, tools, loader = self.calls[0]
        self.assertEqual((options.from_ref, options.to_ref), ("BASE", "TARGET"))
        self.assertFalse(options.no_jira)
        self.assertEqual(options.jira_status_scope, "all")
        self.assertIs(loader, self.loader)
        self.assertNotIn("jira_loader", vars(options))

    def test_host_origin_and_token_guards(self):
        for headers in ({"Host": "evil.example"}, {"Origin": "https://evil.example"},
                        {"Origin": "null"}, {"X-Release-Token": "wrong"}, {"X-Release-Token": "é"}):
            with self.subTest(headers=headers):
                self.assertEqual(self.request("POST", "/api/jobs", {"from": "A", "to": "B"}, headers)[0], 403)
                self.assertEqual(self.request("GET", "/api/state", headers=headers)[0], 403)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.request("GET", "/", headers={"Host": "evil.example"})[0], 403)

    def test_scope_roundtrip_and_status_report_download(self):
        for scope in ("wrong", None, [], 1):
            self.assertEqual(self.request("POST", "/api/jobs", {"from": "A", "to": "B", "statusScope": scope})[0], 400)
        status, body, _ = self.request("POST", "/api/jobs", {"from": "A", "to": "B", "statusScope": "accepted"})
        self.assertEqual(status, 202)
        self.assertTrue(self.entered.wait(3))
        self.finish()
        job_id = json.loads(body)["id"]
        self.assertEqual(self.calls[0][0].jira_status_scope, "accepted")
        self.assertEqual(json.loads(self.request("GET", "/api/state")[1])["latest"]["statusScope"], "accepted")
        for name in ("JIRA_Status_Review_TEST.md", "CHANGELOG.generated.md"):
            self.assertEqual(self.request("GET", f"/api/jobs/{job_id}/files/{name}")[0], 200)

    def test_static_page_and_unknown_paths(self):
        status, body, headers = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(self.service.token, body.decode())
        self.assertNotIn("__SESSION_TOKEN__", body.decode())
        self.assertIn("Release Notes · ECW SDK", body.decode())
        self.assertIn("默认适用芯片：ECW6630、ECW6700", body.decode())
        self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])
        for path in ("/app.js", "/style.css"):
            self.assertEqual(self.request("GET", path)[0], 200)
        for path in ("/../jira_client.py", "/config.json", "/?token=x", "/api/jobs/missing"):
            self.assertEqual(self.request("GET", path)[0], 404)

    def test_custom_project_page_escaping_and_config_drift(self):
        path = self.root / "project-custom.json"
        project = web.core.release_project.load(TOOLS / "examples/release-project.ecr.json")
        project.update(webTitle='ECR <script>bad()</script>', fromPlaceholder='" onfocus="bad()',
                       toPlaceholder="__SESSION_TOKEN__", acceptedJiraStatuses=["已完成"])
        web.core.write_json(path, project)
        self.service.close()
        self.service = web.ReleaseService(self.root, self.root / "runtime", self.root / "config.json",
                                          self.loader, generator=self.generate, project_config=path)
        self.server.service = self.service
        text = self.request("GET", "/")[1].decode()
        self.assertIn("ECR &lt;script&gt;bad()&lt;/script&gt;", text)
        self.assertNotIn("<script>bad()", text)
        self.assertIn('placeholder="&quot; onfocus=&quot;bad()"', text)
        self.assertIn('placeholder="__SESSION_TOKEN__"', text)
        self.assertIn("默认适用芯片：ECR6600", text)
        self.assertIn("指定状态：已完成", text)
        self.assertNotIn("ECW6700SDK", text)
        self.start()
        self.finish()
        options = self.calls[0][0]
        self.assertEqual(options.project_config, str(path))
        self.assertEqual(options.project_config_sha256, web.core.file_hash(path))
        project["acceptedJiraStatuses"] = ["已关闭"]
        web.core.write_json(path, project)
        code, body, _ = self.request("POST", "/api/jobs", {"from": "A", "to": "B"})
        self.assertEqual(code, 400)
        self.assertIn("项目配置已变更", json.loads(body)["error"])

    def test_invalid_requests_and_literal_refs(self):
        for value in ({}, {"from": "A", "to": "B", "command": "bad"}, {"from": "", "to": "B"},
                      {"from": 42, "to": "B"}, {"from": "A\nB", "to": "C"}, b"{bad", b"x" * 4097):
            self.assertEqual(self.request("POST", "/api/jobs", value)[0], 400)
        literal = "$(bad); --no-jira & <script>alert(1)</script>"
        status, _, _ = self.request("POST", "/api/jobs", {"from": literal, "to": "--jira-config"})
        self.assertEqual(status, 202)
        self.assertTrue(self.entered.wait(3))
        self.assertEqual(self.calls[0][0].from_ref, literal)
        self.assertEqual(self.calls[0][0].to_ref, "--jira-config")
        self.assertFalse(self.calls[0][0].no_jira)

    def test_download_membership_and_traversal(self):
        job_id = self.start()
        self.finish()
        for name in ("private.txt", "..%2Fconfig.json", "%2e%2e%5cconfig.json", "missing.md"):
            self.assertEqual(self.request("GET", f"/api/jobs/{job_id}/files/{name}")[0], 404)
        # 即使内存任务指向外部目录，也不得成为任意文件下载接口。
        with self.service.lock:
            self.service.jobs[job_id]["batch"] = str(self.root)
        self.assertEqual(self.request("GET", f"/api/jobs/{job_id}/files/Release_Notes_TEST.md")[0], 404)

    def test_failure_diagnostics_and_unknown_exception_redaction(self):
        def fail(*args, **kwargs):
            batch = self.root / "release/failed"
            batch.mkdir(parents=True)
            web.core.write_json(batch / web.core.MANIFEST, {"status": "failed", "artifacts": {"coverage-review.md": "x"}})
            (batch / "coverage-review.md").write_text("JIRA forbidden", encoding="utf-8")
            error = web.core.ReleaseError("JIRA 查询不完整")
            error.batch = batch
            raise error
        self.service.generator = fail
        job = self.service.start({"from": "A", "to": "B"})
        self.finish()
        result = self.service.get(job["id"])
        self.assertEqual(result["status"], "failed")
        self.assertIn("coverage-review.md", result["files"])
        self.assertIsNone(result["result"])
        self.service.generator = mock.Mock(side_effect=RuntimeError("secret-response-must-not-leak"))
        job = self.service.start({"from": "A", "to": "B"})
        self.finish()
        result = self.service.get(job["id"])
        self.assertEqual(result["status"], "failed")
        self.assertNotIn("secret-response", json.dumps(result))

    def test_cached_credentials_config_drift_and_shutdown(self):
        path = self.root / "config.json"
        path.write_text("{}", encoding="utf-8")
        auth = object()
        with mock.patch.object(web.core.jira_client, "load_jira_config", return_value=({"caFile": None}, auth)) as load:
            loader = web.cached_jira_loader(path, prompt=True)
        config, actual = loader(path)
        self.assertIs(actual, auth)
        config["caFile"] = "mutated"
        self.assertIsNone(loader(path)[0]["caFile"])
        load.assert_called_once_with(path, prompt=True)
        path.write_text('{"changed":true}', encoding="utf-8")
        with self.assertRaisesRegex(web.core.jira_client.JiraError, "config_changed"):
            loader(path)
        path.unlink()
        with self.assertRaisesRegex(web.core.jira_client.JiraError, "config_changed"):
            loader(path)
        self.service.close()
        with self.assertRaises(web.BusyError):
            self.service.start({"from": "A", "to": "B"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
