"""Jira 客户端离线契约测试：所有 HTTP 响应和等待均模拟，不使用真实账户。"""
import base64
import contextlib
import copy
from email.message import Message
from email.utils import formatdate
import io
import json
import os
from pathlib import Path
import ssl
import sys
import tempfile
import unittest
from unittest import mock
from urllib import error, parse, request, response

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import jira_client as client

CONFIG = {
    "schemaVersion": 1, "baseUrl": "https://jira.example.invalid/jira", "deployment": "server",
    "auth": {"mode": "basic-password", "userEnv": "TEST_JIRA_USER", "passwordEnv": "TEST_JIRA_SECRET"},
    "projects": {"ECW6700": {"issueTypeMap": {"1": "fixes"}},
                 "ECW6280": {"issueTypeMap": {"2": "features"}}},
}
ENV = {"TEST_JIRA_USER": "test-user", "TEST_JIRA_SECRET": "test-secret"}


def issue(key="ECW6700-1434", **fields):
    return {"id": "42", "key": key, "fields": {
        "project": {"key": key.rsplit("-", 1)[0]}, "summary": "修复重复通知",
        "issuetype": {"id": "1", "name": "故障"}, "description": "first\nsecond",
        "components": [{"id": "9", "name": "AT"}], "labels": ["release"],
        "fixVersions": [{"id": "10", "name": "P22"}], "status": {"id": "3", "name": "Done"},
        "resolution": None, "updated": "2026-09-28T00:00:00.000+0800", "assignee": None, **fields}}


class Reply(io.BytesIO):
    def __init__(self, data=None, *, raw=None, content_type="application/json;charset=UTF-8", status=200):
        super().__init__(raw if raw is not None else json.dumps(data, ensure_ascii=False).encode("utf-8"))
        self.status = status
        self.headers = Message()
        self.headers["Content-Type"] = content_type


def http_error(status, retry=None, captcha=False):
    headers = Message()
    if retry is not None:
        headers["Retry-After"] = retry
    if captcha:
        headers["X-Seraph-LoginReason"] = "AUTHENTICATION_DENIED"
    return error.HTTPError("https://jira.example.invalid/private", status, "SECRET_SERVER_ERROR",
                           headers, io.BytesIO(b"SECRET_ERROR_BODY"))


class Clock:
    def __init__(self):
        self.elapsed, self.sleeps = 0.0, []

    def monotonic(self):
        return self.elapsed

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.elapsed += seconds


class Transport:
    def __init__(self, replies):
        self.replies, self.calls = iter(replies), []

    def open(self, req, timeout):
        self.calls.append((req, timeout))
        value = next(self.replies)
        if callable(value):
            value = value()
        if isinstance(value, BaseException):
            raise value
        return value


class JiraClientTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="jira-client-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / "配置.json"

    def load(self, data=None, environ=None, **kwargs):
        self.path.write_text(json.dumps(CONFIG if data is None else data, ensure_ascii=False), encoding="utf-8-sig")
        return client.load_jira_config(self.path, environ=ENV if environ is None else environ, **kwargs)

    def fetch(self, replies, keys=("ECW6700-1434",), data=None, clock=None):
        config, auth = self.load(data)
        transport, clock = Transport(replies), clock or Clock()
        with mock.patch.object(client.request, "build_opener", return_value=transport), \
                mock.patch.object(client.time, "monotonic", side_effect=clock.monotonic), \
                mock.patch.object(client.time, "sleep", side_effect=clock.sleep):
            report = client.fetch_issues(keys, config, auth)
        return report, transport, clock

    def test_config_defaults_bom_context_path_and_auth_repr(self):
        config, auth = self.load()
        self.assertEqual((config["timeoutSeconds"], config["totalBudgetSeconds"], config["maxAttempts"]), (10, 120, 2))
        self.assertNotIn(ENV["TEST_JIRA_SECRET"], repr(auth))
        self.assertFalse(hasattr(auth, "__dict__"))

    def test_invalid_config_matrix(self):
        changes = [ {"schemaVersion": True}, {"schemaVersion": 2}, {"unknown": 1}, {"projects": {}},
                    {"timeoutSeconds": True}, {"timeoutSeconds": 0}, {"timeoutSeconds": 61},
                    {"totalBudgetSeconds": 9}, {"totalBudgetSeconds": 601}, {"maxAttempts": 0}, {"maxAttempts": 4},
                    {"deployment": "unknown"}, {"auth": {"mode": "basic-token"}}, {"caFile": False},
                    {"releaseCategoryField": "summary"}, {"releaseCategoryMap": {"1": "fixes"}},
                    {"projects": {"bad-key": {"issueTypeMap": {}}}},
                    {"projects": {"ECW6700": {"issueTypeMap": {"Bug": "fixes"}}}},
                    {"projects": {"ECW6700": {"issueTypeMap": {"1": "Bug"}}}} ]
        for delta in changes:
            with self.subTest(delta=delta), self.assertRaises(client.JiraError):
                self.load({**copy.deepcopy(CONFIG), **delta})

    def test_unsafe_url_matrix(self):
        urls = ["http://jira.example.invalid", "https://user:secret@jira.example.invalid", "https://@jira.example.invalid",
                "https://jira.example.invalid?a=1", "https://jira.example.invalid#part", "https://jira.example.invalid?",
                "https://jira.example.invalid/../jira", "https://jira.example.invalid/%2e%2e/jira",
                "https://jira.example.invalid\r\nX: 1", "https://jira.example.invalid:99999", "https://"]
        for url in urls:
            with self.subTest(url=url), self.assertRaises(client.JiraError):
                self.load({**CONFIG, "baseUrl": url})

    def test_optional_fallback_type_mapping_is_validated_and_archived(self):
        config = copy.deepcopy(CONFIG)
        project = config["projects"]["ECW6700"]
        project["fallbackIssueTypeMap"] = {"10604": "features"}
        report, _, _ = self.fetch([Reply(issue(issuetype={"id": "10604", "name": "需求"}))], data=config)
        self.assertEqual(report["configSnapshot"]["projects"]["ECW6700"]["fallbackIssueTypeMap"], {"10604": "features"})
        self.assertNotIn("unmapped_issue_type", report["results"]["ECW6700-1434"]["warnings"])
        for value in (None, [], {"需求": "features"}, {"10604": "unclassified"}):
            project["fallbackIssueTypeMap"] = value
            with self.subTest(value=value), self.assertRaises(client.JiraError):
                self.load(config)
        old, _ = self.load()
        self.assertNotIn("fallbackIssueTypeMap", old["projects"]["ECW6700"])

    def test_duplicate_json_keys_and_wrong_top_level(self):
        for raw in ('{"schemaVersion":1,"schemaVersion":1}', "[]", "null", "{"):
            self.path.write_text(raw, encoding="utf-8")
            with self.subTest(raw=raw), self.assertRaises(client.JiraError):
                client.load_jira_config(self.path, environ=ENV)

    def test_credentials_missing_invalid_and_no_values_in_errors(self):
        for env in ({}, {**ENV, "TEST_JIRA_USER": ""}, {**ENV, "TEST_JIRA_SECRET": "\nSECRET"},
                    {**ENV, "TEST_JIRA_USER": "user:SECRET"}):
            with self.subTest(env_keys=list(env)), self.assertRaises(client.JiraError) as caught:
                self.load(environ=env)
            self.assertNotIn("SECRET", str(caught.exception))

    def test_prompt_reads_secret_without_recording_it(self):
        with mock.patch("builtins.input", return_value="interactive-user"), \
                mock.patch.object(client.sys.stdin, "isatty", return_value=True), \
                mock.patch.object(client.getpass, "getpass", return_value="interactive-password") as prompt:
            _, auth = self.load(environ={}, prompt=True)
        self.assertEqual(prompt.call_count, 1)
        self.assertNotIn("interactive-password", repr(auth))
        with mock.patch.object(client.sys.stdin, "isatty", return_value=False), self.assertRaises(client.JiraError):
            self.load(environ={"TEST_JIRA_USER": "test-user"}, prompt=True)

    def test_ca_path_resolution_validation_and_default_tls(self):
        certificate = self.root / "enterprise.pem"
        certificate.write_text("not a certificate", encoding="ascii")
        with self.assertRaisesRegex(client.JiraError, "invalid_ca"):
            self.load({**CONFIG, "caFile": "enterprise.pem"})
        with mock.patch.object(client.ssl, "create_default_context") as create:
            config, _ = self.load({**CONFIG, "caFile": "enterprise.pem"})
            self.assertEqual(config["caFile"], str(certificate.resolve()))
            create.return_value.load_verify_locations.assert_called_once_with(cafile=str(certificate.resolve()))
        context = client._ssl_context(None)
        self.assertTrue(context.check_hostname)
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)

    def test_server_basic_exact_fields_readonly_and_secret_redaction(self):
        reply = Reply(issue())
        report, transport, _ = self.fetch([reply])
        req, timeout = transport.calls[0]
        self.assertEqual(req.get_method(), "GET")
        self.assertEqual(parse.urlsplit(req.full_url).path, "/jira/rest/api/2/issue/ECW6700-1434")
        query = parse.parse_qs(parse.urlsplit(req.full_url).query)
        self.assertEqual(set(query["fields"][0].split(",")), set(client.FIELDS))
        self.assertEqual(query["updateHistory"], ["false"])
        encoded = base64.b64encode(b"test-user:test-secret").decode()
        self.assertEqual(req.get_header("Authorization"), "Basic " + encoded)
        serialized = json.dumps(report)
        for value in ("test-secret", encoded, "Authorization", "passwordEnv", "caFile"):
            self.assertNotIn(value, serialized)
        self.assertTrue(reply.closed)
        self.assertEqual(timeout, 10)
        self.assertEqual(report["status"], "complete")

    def test_cloud_token_and_data_center_bearer(self):
        for deployment, mode in (("cloud", "basic-token"), ("data-center", "bearer")):
            config = copy.deepcopy(CONFIG)
            config["deployment"] = deployment
            config["auth"] = {"mode": mode, "tokenEnv": "TEST_JIRA_SECRET"}
            if mode == "basic-token":
                config["auth"]["userEnv"] = "TEST_JIRA_USER"
            report, transport, _ = self.fetch([Reply(issue())], data=config)
            req = transport.calls[0][0]
            self.assertIn("/api/3/" if deployment == "cloud" else "/api/2/", req.full_url)
            self.assertTrue(req.get_header("Authorization").startswith("Basic " if mode == "basic-token" else "Bearer "))
            self.assertEqual(report["status"], "complete")
        config = {**CONFIG, "auth": {"mode": "bearer", "tokenEnv": "TEST_JIRA_SECRET"}}
        for secret in ("has space", "中文token"):
            with self.subTest(secret_kind="invalid bearer"), self.assertRaisesRegex(client.JiraError, "credentials_invalid"):
                self.load(config, environ={**ENV, "TEST_JIRA_SECRET": secret})

    def test_dedup_sorted_whitelist_and_alias_not_entry_dedup(self):
        keys = ["ECW6700-2", "ECW6280-1", "OTHER-1", "ECW6700-1", "ECW6700-1"]
        report, transport, _ = self.fetch([Reply(issue("ECW6280-1")), Reply(issue("ECW6700-99")),
                                           Reply(issue("ECW6700-99"))], keys)
        self.assertEqual(list(report["results"]), sorted(set(keys)))
        self.assertEqual(len(transport.calls), 3)
        self.assertEqual(report["results"]["OTHER-1"]["reasonCode"], "project_not_configured")
        self.assertEqual(report["results"]["ECW6700-1"]["canonicalKey"], "ECW6700-99")
        self.assertEqual(report["results"]["ECW6700-2"]["canonicalKey"], "ECW6700-99")

    def test_empty_keys_and_invalid_keys_do_not_query(self):
        report, transport, _ = self.fetch([], [])
        self.assertEqual(report["status"], "complete")
        self.assertEqual(transport.calls, [])
        config, auth = self.load()
        for keys in ("ECW6700-1", ["ecw6700-1"], ["ECW6700-1/../../other"], [12]):
            with self.subTest(keys=keys), self.assertRaises(client.JiraError):
                client.fetch_issues(keys, config, auth)

    def test_51_references_make_41_queries_across_three_projects(self):
        keys = ([f"ECW6700-{i}" for i in range(1, 38)] + ["ECW6630-1", "ECW6630-2", "ECW6280-1", "ECW6280-2"] +
                [f"ECW6700-{i}" for i in range(1, 11)])
        config = copy.deepcopy(CONFIG)
        config["projects"]["ECW6630"] = {"issueTypeMap": {}}
        report, transport, _ = self.fetch([Reply(issue(k)) for k in sorted(set(keys))], keys, config)
        self.assertEqual(len(keys), 51)
        self.assertEqual(len(transport.calls), 41)
        self.assertEqual(report["counts"]["fetched"], 41)

    def test_config_relative_to_callers_directory(self):
        self.load()
        previous = Path.cwd()
        try:
            os.chdir(self.root)
            config, _ = client.load_jira_config(self.path.name, environ=ENV)
        finally:
            os.chdir(previous)
        self.assertEqual(config["baseUrl"], CONFIG["baseUrl"])

    def test_canonical_project_and_required_field_validation(self):
        moved = issue("NEWPROJECT-7")
        report, _, _ = self.fetch([Reply(moved)])
        self.assertIn("canonical_project_not_configured", report["results"]["ECW6700-1434"]["warnings"])
        bad = [None, {}, issue(summary=None), issue(issuetype={"name": "Bug"}), issue(project={"key": "OTHER"})]
        bad.append({**issue(), "id": None})
        for payload in bad:
            with self.subTest(payload=payload):
                report, _, _ = self.fetch([Reply(payload)])
                result = report["results"]["ECW6700-1434"]
                self.assertEqual(result["reasonCode"], "invalid_response")
                self.assertIsNone(result["fields"])

    def test_issue_type_ids_not_names_and_no_classification(self):
        report, _, _ = self.fetch([Reply(issue(issuetype={"id": "99", "name": "Bug"}))])
        result = report["results"]["ECW6700-1434"]
        self.assertEqual(result["fields"]["issueType"], {"id": "99", "name": "Bug"})
        self.assertIn("unmapped_issue_type", result["warnings"])
        self.assertNotIn("category", result)
        self.assertNotIn("customerApproved", report)

    def test_custom_category_null_missing_invalid_and_unknown(self):
        config = {**copy.deepcopy(CONFIG), "releaseCategoryField": "customfield_123", "releaseCategoryMap": {"8": "fixes"}}
        cases = [({}, "category_field_missing"), ({"customfield_123": None}, None),
                 ({"customfield_123": [{"id": "8", "value": "Fix"}]}, "invalid_category_field"),
                 ({"customfield_123": {"id": "9", "value": "Other"}}, "unmapped_release_category"),
                 ({"customfield_123": {"id": "8", "value": "Fix"}}, None)]
        for fields, warning in cases:
            with self.subTest(warning=warning):
                report, transport, _ = self.fetch([Reply(issue(**fields))], data=config)
                warnings = report["results"]["ECW6700-1434"]["warnings"]
                self.assertEqual(warnings, [warning] if warning else [])
                self.assertIn("customfield_123", transport.calls[0][0].full_url)

    def test_adf_plaintext_lists_breaks_unknown_and_no_media_fetch(self):
        adf = {"type": "doc", "version": 1, "content": [
            {"type": "paragraph", "content": [{"type": "text", "text": "first"}, {"type": "hardBreak"},
                                                  {"type": "text", "text": "second"}]},
            {"type": "bulletList", "content": [{"type": "listItem", "content": [
                {"type": "paragraph", "content": [{"type": "text", "text": "item"}]}]}]},
            {"type": "media", "attrs": {"url": "https://untrusted.invalid/image"}}]}
        report, transport, _ = self.fetch([Reply(issue(description=adf))])
        result = report["results"]["ECW6700-1434"]
        self.assertIn("first\nsecond", result["fields"]["descriptionText"])
        self.assertIn("item", result["fields"]["descriptionText"])
        self.assertIn("unsupported_adf_node", result["warnings"])
        self.assertEqual(len(transport.calls), 1)
        self.assertNotIn("untrusted.invalid", json.dumps(result))

    def test_utf8_limits_and_adf_depth_are_bounded(self):
        report, _, _ = self.fetch([Reply(issue(summary="汉" * 3000, description="汉" * 20000))])
        result = report["results"]["ECW6700-1434"]
        self.assertEqual(result["truncatedFields"], ["description", "summary"])
        self.assertLessEqual(len(result["fields"]["summary"].encode()), client.MAX_SUMMARY_BYTES)
        self.assertLessEqual(len(result["fields"]["descriptionText"].encode()), client.MAX_DESCRIPTION_BYTES)
        adf = {"type": "doc", "content": [{"type": "text", "text": "x"}] * (client.MAX_ADF_NODES + 1)}
        report, _, _ = self.fetch([Reply(issue(description=adf))])
        self.assertIn("adf_node_limit", report["results"]["ECW6700-1434"]["warnings"])

    def test_malformed_adf_children_are_flagged(self):
        adf = {"type": "doc", "content": ["not an ADF node", {"type": "text", "text": 123},
                                          {"type": "paragraph", "content": "invalid"}]}
        report, _, _ = self.fetch([Reply(issue(description=adf))])
        result = report["results"]["ECW6700-1434"]
        self.assertEqual(result["fields"]["descriptionText"], "")
        self.assertEqual(result["warnings"], ["invalid_adf_content", "invalid_adf_node", "invalid_adf_text"])

    def test_optional_fields_missing_and_invalid(self):
        payload = issue(components="bad", labels=[1], status=[], updated=1)
        del payload["fields"]["description"]
        del payload["fields"]["fixVersions"]
        report, _, _ = self.fetch([Reply(payload)])
        result = report["results"]["ECW6700-1434"]
        self.assertEqual(result["status"], "fetched")
        for warning in ("invalid_components", "invalid_labels", "invalid_status", "invalid_updated",
                        "missing_description", "missing_fixVersions"):
            self.assertIn(warning, result["warnings"])

    def test_assignee_names_null_missing_and_invalid(self):
        self.assertIn("assignee", client.FIELDS)
        cases = [({"name": "E0001", "displayName": "研发甲", "emailAddress": "private@example.invalid"},
                  {"name": "E0001", "displayName": "研发甲"}, None),
                 ({"accountId": "cloud-id", "displayName": "研发乙"}, {"name": "cloud-id", "displayName": "研发乙"}, None),
                 ({"name": "dev"}, {"name": "dev", "displayName": "dev"}, None),
                 (None, None, None), ({}, None, "invalid_assignee"), ("bad", None, "invalid_assignee"),
                 ({"name": "dev", "displayName": 123}, None, "invalid_assignee")]
        for raw, expected, warning in cases:
            with self.subTest(raw=raw):
                report, _, _ = self.fetch([Reply(issue(assignee=raw))])
                result = report["results"]["ECW6700-1434"]
                self.assertEqual(result["status"], "fetched")
                self.assertEqual(result["fields"]["assignee"], expected)
                self.assertEqual(result["warnings"], [warning] if warning else [])
                self.assertNotIn("emailAddress", json.dumps(report))
        payload = issue()
        del payload["fields"]["assignee"]
        report, _, _ = self.fetch([Reply(payload)])
        self.assertIsNone(report["results"]["ECW6700-1434"]["fields"]["assignee"])
        self.assertIn("missing_assignee", report["results"]["ECW6700-1434"]["warnings"])

    def test_401_and_captcha_abort_later_authentication(self):
        for denied in (http_error(401), http_error(403, captcha=True)):
            report, transport, _ = self.fetch([denied], ["ECW6700-1", "ECW6700-2"])
            self.assertEqual(len(transport.calls), 1)
            self.assertEqual(report["results"]["ECW6700-1"]["reasonCode"], "auth_failed")
            self.assertEqual(report["results"]["ECW6700-2"]["reasonCode"], "auth_aborted")
            self.assertNotIn("SECRET", json.dumps(report))

    def test_403_404_continue_other_keys_without_retry(self):
        report, transport, _ = self.fetch([http_error(403), http_error(404), Reply(issue("ECW6700-3"))],
                                         ["ECW6700-1", "ECW6700-2", "ECW6700-3"])
        self.assertEqual(report["counts"], {"keys": 3, "fetched": 1, "failed": 2, "skipped": 0, "attempts": 3})
        self.assertEqual(report["status"], "partial")
        self.assertEqual(report["results"]["ECW6700-2"]["reasonCode"], "not_found_or_not_visible")

    def test_retryable_errors_and_max_attempts(self):
        failures = [TimeoutError("SECRET"), error.URLError(ConnectionResetError("SECRET")),
                    *[http_error(code) for code in (429, 500, 502, 503, 504)]]
        for failure in failures:
            with self.subTest(type=type(failure).__name__):
                report, transport, clock = self.fetch([failure, Reply(issue())])
                self.assertEqual(report["status"], "complete")
                self.assertEqual(len(transport.calls), 2)
                self.assertEqual(clock.sleeps, [1])
        report, transport, _ = self.fetch([TimeoutError(), TimeoutError()])
        self.assertEqual(len(transport.calls), 2)
        self.assertEqual(report["results"]["ECW6700-1434"]["reasonCode"], "timeout")

    def test_retry_after_seconds_date_invalid_and_deferred(self):
        with mock.patch.object(client.time, "time", return_value=1000000000):
            for value, delay in (("2", 2), (formatdate(1000000003, usegmt=True), 3), ("bad", 1), ("0", 0)):
                report, _, clock = self.fetch([http_error(429, value), Reply(issue())])
                self.assertEqual(clock.sleeps, [delay])
                self.assertEqual(report["status"], "complete")
        for code in (429, 503):
            report, transport, clock = self.fetch([http_error(code, "20")])
            self.assertEqual(len(transport.calls), 1)
            self.assertEqual(clock.sleeps, [])
            self.assertEqual(report["results"]["ECW6700-1434"]["reasonCode"], "rate_limited" if code == 429 else "retry_deferred")

    def test_budget_skips_unattempted_keys_and_clamps_timeout(self):
        clock = Clock()
        def slow_failure():
            clock.elapsed += 10
            return TimeoutError()
        report, transport, _ = self.fetch([slow_failure], ["ECW6700-1", "ECW6700-2"],
                                         {**CONFIG, "totalBudgetSeconds": 10, "timeoutSeconds": 60}, clock)
        self.assertEqual(len(transport.calls), 1)
        self.assertEqual(transport.calls[0][1], 10)
        self.assertEqual(report["results"]["ECW6700-2"]["reasonCode"], "budget_exhausted")
        self.assertEqual(report["results"]["ECW6700-2"]["attempts"], 0)

    def test_tls_redirect_html_invalid_json_and_response_limit(self):
        cases = [(ssl.SSLError("SECRET"), "tls_error"), (error.URLError(ssl.SSLError("SECRET")), "tls_error"),
                 (http_error(302), "redirect_rejected"), (http_error(400), "http_error"),
                 (Reply(raw=b"<html>SECRET</html>", content_type="text/html"), "non_json_response"),
                 (Reply(raw=b"not JSON SECRET"), "invalid_response"),
                 (Reply(raw=b"x" * (client.MAX_RESPONSE_BYTES + 1)), "response_too_large")]
        for value, reason in cases:
            with self.subTest(reason=reason):
                report, transport, _ = self.fetch([value])
                self.assertEqual(report["results"]["ECW6700-1434"]["reasonCode"], reason)
                self.assertEqual(len(transport.calls), 1)
                self.assertNotIn("SECRET", json.dumps(report))

    def test_response_budget_checked_during_read_and_resources_closed(self):
        clock = Clock()
        class SlowReply(Reply):
            def read(self, size=-1):
                clock.elapsed += 10
                return super().read(size)
        reply = SlowReply(issue())
        report, _, _ = self.fetch([reply], data={**CONFIG, "totalBudgetSeconds": 10}, clock=clock)
        self.assertEqual(report["results"]["ECW6700-1434"]["reasonCode"], "budget_exhausted")
        self.assertTrue(reply.closed)
        class PrivateBody(io.BytesIO):
            def read(self, *args):
                raise AssertionError("Error body must not be read")
        body = PrivateBody(b"SECRET_BODY")
        report, _, _ = self.fetch([error.HTTPError("https://jira.example.invalid", 403, "private", Message(), body)])
        self.assertEqual(report["status"], "unavailable")
        self.assertTrue(body.closed)

    def test_jira_config_snapshot_does_not_alias_input(self):
        config, auth = self.load()
        transport = Transport([Reply(issue())])
        with mock.patch.object(client.request, "build_opener", return_value=transport):
            result = client.fetch_issues(["ECW6700-1434"], config, auth)
        config["projects"]["ECW6700"]["issueTypeMap"]["1"] = "features"
        self.assertEqual(result["configSnapshot"]["projects"]["ECW6700"]["issueTypeMap"]["1"], "fixes")

    def test_real_urllib_redirect_handler_does_not_forward_auth(self):
        config, auth = self.load()
        def redirected(req):
            headers = Message()
            headers["Location"] = "http://other.invalid/steal"
            resp = response.addinfourl(io.BytesIO(b""), headers, req.full_url, 302)
            resp.msg = "Found"
            return resp
        with mock.patch.object(request.HTTPSHandler, "https_open", side_effect=redirected) as opened:
            report = client.fetch_issues(["ECW6700-1434"], config, auth)
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(report["results"]["ECW6700-1434"]["reasonCode"], "redirect_rejected")

    def test_mutated_origin_rejected_before_http(self):
        config, auth = self.load()
        config["baseUrl"] = "https://other.invalid"
        with mock.patch.object(request.OpenerDirector, "open") as opened, self.assertRaisesRegex(client.JiraError, "auth_origin_mismatch"):
            client.fetch_issues(["ECW6700-1434"], config, auth)
        opened.assert_not_called()

    def test_unknown_transport_exception_is_sanitized_and_interrupt_propagates(self):
        with self.assertRaises(client.JiraError) as caught:
            self.fetch([RuntimeError("SECRET_REQUEST_HEADERS")])
        self.assertEqual(str(caught.exception), "internal_error: Jira 查询未完成")
        with self.assertRaises(KeyboardInterrupt):
            self.fetch([KeyboardInterrupt()])

    def test_cli_exit_codes_and_no_implicit_file_output(self):
        self.load()
        for status, code in (("complete", 0), ("partial", 2), ("unavailable", 2)):
            with mock.patch.dict(client.os.environ, ENV), mock.patch.object(client, "fetch_issues", return_value={"status": status}), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                actual = client.main(["--config", str(self.path), "--issue", "ECW6700-1434"])
                self.assertEqual(actual, code)
                self.assertEqual(json.loads(output.getvalue())["status"], status)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            client.main(["--config", str(self.path), "--password", "dummy"])
        with mock.patch.dict(client.os.environ, {}, clear=True), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(client.main(["--config", str(self.path), "--issue", "ECW6700-1434"]), 1)
        self.assertEqual([p.name for p in self.root.iterdir()], [self.path.name])


if __name__ == "__main__":
    unittest.main()
