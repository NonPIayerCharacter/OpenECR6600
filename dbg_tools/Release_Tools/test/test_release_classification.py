"""分类策略与快照契约；所有 Jira fields 为模拟数据，不联网。"""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import release_classification as c
import generate_release_notes as core


def report(types, *, failed=(), category_field=None, config_path=None):
    config = json.loads((config_path or TOOLS / "examples/jira-config.example.json").read_text(encoding="utf-8"))
    config = {k: v for k, v in config.items() if k not in ("auth", "caFile")}
    config.update(releaseCategoryField=category_field, releaseCategoryMap={"1": "features"} if category_field else {})
    results = {}
    for index, (key, type_id) in enumerate(types.items()):
        project = key.rsplit("-", 1)[0]
        result = {"requestedKey": key, "canonicalKey": key, "issueId": str(123 + index), "status": "fetched", "reasonCode": None,
                  "httpStatus": 200, "attempts": 1, "fetchedAt": "2026-09-28T00:00:00Z", "warnings": [], "truncatedFields": [],
                  "fields": {"projectKey": project, "summary": "Simulated issue", "issueType": {"id": type_id, "name": "Simulated type"},
                             "descriptionText": "", "components": [], "fixVersions": [], "labels": [], "status": None,
                             "resolution": None, "updated": None, "releaseCategory": None}}
        if (type_id not in config["projects"].get(project, {}).get("issueTypeMap", {}) and
                type_id not in config["projects"].get(project, {}).get("fallbackIssueTypeMap", {})):
            result["warnings"].append("unmapped_issue_type")
        if key in failed:
            result.update(canonicalKey=None, issueId=None, status="failed", reasonCode="forbidden", fields=None, httpStatus=403, fetchedAt=None)
        results[key] = result
    fetched = len(results) - len(failed)
    return {"mode": "live", "status": "complete" if not failed else "partial" if fetched else "unavailable",
            "normalizedSchemaVersion": 1, "configSnapshot": config, "results": results,
            "counts": {"keys": len(results), "fetched": fetched, "failed": len(failed), "skipped": 0, "attempts": len(results)},
            "startedAt": "2026-09-28T00:00:00Z", "finishedAt": "2026-09-28T00:00:01Z",
            "snapshotDigest": None, "sourceManifestSha256": None}


class ClassificationTests(unittest.TestCase):
    def test_important_summary_mapping_union_and_scope(self):
        raw = [{"hash": "a", "subject": "[ECW6700-1] add option"},
               {"hash": "b", "subject": "[ECW6700-1] fix option"},
               {"hash": "c", "subject": "[ECW6700-2][ECW6700-1] adapt board"},
               {"hash": "d", "subject": "[ECW6700-2] ordinary"},
               {"hash": "e", "subject": "no issue"}]
        jira = report({"ECW6700-1": "11101", "ECW6700-2": "11101", "ECW6700-3": "11101"})
        jira["results"]["ECW6700-1"]["fields"]["summary"] = "功能【重要】调整"
        jira["results"]["ECW6700-2"]["fields"]["summary"] = "重要事项【不重要】"
        jira["results"]["ECW6700-3"]["fields"]["summary"] = "【重要】范围外"
        entries = [{"id": name, "commits": [sha], "issues": ["ECW6700-3"]}
                   for name, sha in (("RN1", "a"), ("RN2", "a"), ("RN3", "b"), ("RN4", "c"), ("RN5", "d"), ("RN6", "e"))]
        selected, audit = core.select_important({"entries": entries}, raw, jira, ["RN5", "RN1", "RN5"])
        self.assertEqual(selected, ["RN5", "RN1", "RN2", "RN3", "RN4"])
        self.assertEqual(audit["entries"]["RN1"], {"sources": ["manual", "jira-summary"], "jiraKeys": ["ECW6700-1"]})
        self.assertEqual(audit["entries"]["RN5"], {"sources": ["manual"], "jiraKeys": []})
        self.assertNotIn("RN6", audit["entries"])
        self.assertEqual(core.select_important({"entries": entries}, raw, c.disabled_jira(), [])[0], [])

    def test_default_jira_mode_and_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            default = root / "examples/jira-config.example.json"
            options = core.parser().parse_args(["--jira-prompt"])
            actual = core.generation_options(options, root)
            self.assertEqual(actual.jira_config, str(default))
            self.assertIsNone(options.jira_config)
            (root / "jira-config.local.json").write_text("{}", encoding="utf-8")
            self.assertEqual(core.generation_options(options, root).jira_config, str(root / "jira-config.local.json"))
            for flag, value in (("--jira-config", "custom.json"), ("--jira-snapshot", "saved.json"), ("--no-jira", None)):
                options = core.parser().parse_args([flag, value] if value else [flag])
                actual = core.generation_options(options, root)
                self.assertEqual(vars(options), vars(actual))
            options.jira_prompt = True
            with self.assertRaises(core.ReleaseError):
                core.generation_options(options, root)

    def test_incomplete_jira_and_truncated_summary_block_customer_output(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            core.check_jira_complete(report({}), stage)
            core.check_jira_complete(c.disabled_jira(), stage)
            jira = report({"ECW6700-1": "11101"})
            jira["results"]["ECW6700-1"]["truncatedFields"] = ["summary"]
            with self.assertRaises(core.ReleaseError):
                core.check_jira_complete(jira, stage)
            self.assertIn("summary_truncated", (stage / "coverage-review.md").read_text(encoding="utf-8"))
            with self.assertRaises(core.ReleaseError):
                core.check_jira_complete(report({"ECW6700-1": "11101"}, failed=("ECW6700-1",)), stage)

    def classify(self, subject, jira=None, body=""):
        return c.classify_commit({"hash": "a" * 40, "subject": subject}, {**c.empty_evidence(), "body": body, "requested": bool(body)}, jira)

    def test_metadata_defaults_and_manual_values(self):
        content = {"releaseDate": "  ", "scope": "", "entries": []}
        with mock.patch.object(core, "datetime") as clock:
            clock.now.return_value.astimezone.return_value.date.return_value.isoformat.return_value = "2030-01-02"
            actual = core.apply_metadata_defaults(content)
        self.assertEqual(actual["releaseDate"], "2030-01-02")
        self.assertEqual(actual["scope"], "ECW6630、ECW6700")
        self.assertEqual(content["releaseDate"], "  ")
        manual = {**content, "releaseDate": "2026-11-01", "scope": "ECW6700 / customer board"}
        self.assertEqual(core.apply_metadata_defaults(manual), manual)
        self.assertEqual(core.apply_metadata_defaults({})["scope"], "ECW6630、ECW6700")

    def test_review_counts_and_actionable_report(self):
        raw = [{"hash": "a" * 40, "subject": "[ECW6700-1] I2C 功能未合入"},
               {"hash": "b" * 40, "subject": "[ECW6700-2] 重构流程并新增配置目标"},
               {"hash": "c" * 40, "subject": "[ECW6700-3] fix crash"}]
        jira = report({"ECW6700-1": "11101", "ECW6700-2": "10604", "ECW6700-3": "11101"})
        decisions = {r["hash"]: c.classify_commit(r, jira=jira) for r in raw}
        identity = {"previous": {"commit": "0" * 40, "tag": "BASE", "type": "tag"},
                    "target": {"commit": "c" * 40, "tag": "TARGET", "type": "tag"}}
        content = core.apply_metadata_defaults(core.automatic_content(raw, identity, decisions))
        audit = {"AUTO_" + sha: decision for sha, decision in decisions.items()}
        coverage, stats = c.customer_coverage(raw, content, audit, automatic=True)
        self.assertEqual(stats["jiraFallbackEntryCount"], 2)
        self.assertEqual(stats["reviewRequiredEntryCount"], 2)
        self.assertEqual(stats["categoryCounts"]["unclassified"], 0)
        for github in (False, True):
            text, _ = core.render(content, [], identity, "TARGET", github=github, statistics=stats)
            self.assertIn("JIRA 兜底 2 条，需人工复核 2 条", text)
            self.assertIn("发布日期：" + content["releaseDate"], text)
            self.assertIn("ECW6630、ECW6700", text)
        review = core.coverage_markdown(raw, {"missing": [], "decisions": []}, content, [], identity,
                                       audit, jira, coverage, stats)
        self.assertLess(review.index("## 优先复核"), review.index("## 提交逐条覆盖"))
        self.assertIn("问题描述待核实并改写为具体变更", review)
        self.assertIn("复合变更，需确认是否拆项", review)
        manual = c.manual_decisions(content["entries"], raw, decisions)
        manual_coverage, manual_stats = c.customer_coverage(raw, content, manual)
        self.assertEqual(manual_stats["jiraFallbackEntryCount"], 0)
        review = core.coverage_markdown(raw, {"missing": [], "decisions": []}, content, [], identity,
                                       manual, jira, manual_coverage, manual_stats)
        self.assertIn("自动候选曾按 JIRA 兜底，最终以人工分类为准", review)

    def test_manual_example_covers_p21_p22_and_splits_independent_changes(self):
        content = core.read_json(TOOLS / "examples/p21-p22-content.json")
        records = core.read_json(TOOLS / "test/fixtures/classification-sample.json")["records"]
        hashes = {h for e in content["entries"] for h in e["commits"]}
        raw = [{"hash": x["hash"], "subject": x["subject"]} for x in records if x["hash"] in hashes]
        self.assertEqual(len(raw), 16)
        identity = {"previous": {"commit": content["previousSha"]}, "target": {"commit": content["targetSha"]}}
        core.validate_entries(content, raw, identity, full=True)
        decisions = {r["hash"]: c.classify_commit(r) for r in raw}
        audit = c.manual_decisions(content["entries"], raw, decisions)
        coverage, stats = c.customer_coverage(raw, content, audit)
        self.assertEqual(coverage["status"], "complete")
        self.assertEqual(stats["customerEntryCount"], 15)
        self.assertEqual(stats["unaccountedCommitCount"], 0)
        for key in ("ECW6700-1407", "ECW6700-1432", "ECW6630-2434", "ECW6700-1470"):
            self.assertTrue(any(e["issues"] == [key] for e in content["entries"]))

    def test_sample_51_has_explicit_expectations_and_no_omissions(self):
        fixture = core.read_json(TOOLS / "test/fixtures/classification-sample.json")["records"]
        types = {k: v for x in fixture for k, v in x["jiraTypes"].items()}
        jira = report(types)
        self.assertEqual(len(fixture), 51)
        self.assertEqual(len(types), 41)
        identity = {"previous": {"commit": "a" * 40}, "target": {"commit": "b" * 40}}
        raw = [{k: x[k] for k in ("hash", "subject")} for x in fixture]
        for mode, source in (("Local", None), ("Jira", jira)):
            decisions = {x["hash"]: c.classify_commit(x, jira=source) for x in raw}
            for x in fixture:
                with self.subTest(mode=mode, subject=x["subject"]):
                    self.assertEqual(decisions[x["hash"]]["category"], x["expected" + mode])
            content = core.automatic_content(raw, identity, decisions)
            audit = {"AUTO_" + h: d for h, d in decisions.items()}
            coverage, stats = c.customer_coverage(raw, content, audit, automatic=True)
            self.assertEqual(coverage["status"], "complete")
            self.assertEqual(stats["customerEntryCount"], 51)
            self.assertEqual(stats["issuePrefixEntryCounts"], {"ECW6700": 47, "ECW6630": 2, "ECW6280": 2})
            self.assertEqual(sum(stats["categoryCounts"].values()), 51)
            self.assertEqual(len(set(e["id"] for e in content["entries"])), 51)

    def test_same_issue_can_have_different_commit_types(self):
        jira = report({"ECW6700-1": "10604"})
        self.assertEqual(self.classify("[ECW6700-1] 新增分区", jira)["category"], "features")
        self.assertEqual(self.classify("[ECW6700-1] update build script", jira)["category"], "adaptations")
        self.assertEqual(self.classify("[ECW6700-1] fix compiler error", jira)["category"], "fixes")
        self.assertEqual(self.classify("[ECW6700-1] opt throughput", jira)["category"], "improvements")

    def test_defect_purpose_and_maintenance_objects(self):
        cases = [("增加队列长度，避免分片丢包", "fixes"), ("add api-guide for lowpower", "adaptations"),
                 ("add new throughput configuration target", "features"), ("add pinmux cfg for uart", "features"),
                 ("修复编译错误", "fixes"), ("新增功能并修复已有问题", "unclassified"),
                 ("增加队列避免丢包；新增串口命令", "unclassified"),
                 ("增加队列避免丢包，新增串口命令", "unclassified"),
                 ("未新增命令", "unclassified"), ("AT+SYSMSG? 默认值确认", "unclassified")]
        for text, expected in cases:
            with self.subTest(text=text):
                self.assertEqual(self.classify(text)["category"], expected)

    def test_functional_objects_do_not_imply_build_maintenance(self):
        cases = {"新增源码实现UART驱动": "features", "新增目录遍历API": "features",
                 "add pinmux cfg for uart": "features", "add macro to support new protocol": "features",
                 "add api implementation": "features", "update build script for new target": "adaptations",
                 "add api-guide for lowpower": "adaptations", "update board macro": "adaptations",
                 "删除旧驱动源码": "adaptations", "move ble amt cmd to component/cmd": "adaptations"}
        for project in ("ECR6600", "ECR6600A", "ECW6700", "ECW6630", "ECW6280"):
            key = project + "-1"
            config = TOOLS / "examples" / ("jira-config.ecr.example.json" if project.startswith("ECR") else "jira-config.example.json")
            jira = report({key: "10604"}, config_path=config)
            for text, category in cases.items():
                with self.subTest(project=project, text=text):
                    decision = self.classify("[" + key + "] " + text, jira)
                    self.assertEqual((decision["category"], decision["decisionSource"]), (category, "git-rule"))
                    self.assertNotIn("unmapped_issue_type", decision["reasonCodes"])
                    self.assertNotIn("partial_issue_evidence", decision["reasonCodes"])
            for text in ("adapt customer api", "update source directory", "move data to buffer"):
                decision = self.classify("[" + key + "] " + text, jira)
                self.assertEqual(decision["category"], "features")
                self.assertIn("jira_category_fallback", decision["reasonCodes"])
                self.assertEqual(self.classify(text)["category"], "unclassified")
            self.assertEqual(self.classify("[" + key + "] add api implementation; update build script", jira)["category"], "features")

    def test_report_explains_same_issue_categories_in_readable_text(self):
        jira = report({"ECR6600A-1": "10604"}, config_path=TOOLS / "examples/jira-config.ecr.example.json")
        raw = [{"hash": "a" * 40, "subject": "[ECR6600A-1] add pinmux cfg"},
               {"hash": "b" * 40, "subject": "[ECR6600A-1] update build script"},
               {"hash": "c" * 40, "subject": "[ECR6600A-1] adapt customer api"}]
        identity = {"previous": {"commit": "0" * 40}, "target": {"commit": "c" * 40}}
        decisions = {row["hash"]: c.classify_commit(row, jira=jira) for row in raw}
        content = core.automatic_content(raw, identity, decisions)
        audit = {"AUTO_" + sha: decision for sha, decision in decisions.items()}
        text = core.coverage_markdown(raw, {"missing": [], "decisions": []}, content, [], identity, audit, jira)
        for expected in ("新增功能 / 提交描述", "构建、测试与产品适配变化 / 提交描述", "新增功能 / JIRA 类型兜底",
                         "G03：明确脚本、文档或配套维护", "G04：明确新增功能或配置", "同一 JIRA 的不同提交可以进入不同类别"):
            self.assertIn(expected, text)

    def test_problem_sync_fallback_retains_review_and_merge_block(self):
        jira = report({"ECW6700-1": "11101"})
        for text in ("I2C 初始化失败", "功能未合入", "lmactest should use noRom", "sync qzt code"):
            with self.subTest(text=text):
                decision = self.classify("[ECW6700-1] " + text, jira)
                self.assertEqual(decision["category"], "fixes")
                self.assertIn("jira_category_fallback", decision["reasonCodes"])
                self.assertTrue(decision["needsReview"])
        for text in ("Merge branch fix", 'Revert "fix: crash"'):
            with self.subTest(text=text):
                self.assertEqual(self.classify("[ECW6700-1] " + text, jira)["category"], "unclassified")

    def test_jira_type_id_and_conflict(self):
        jira = report({"ECW6700-1": "11101"})
        jira["results"]["ECW6700-1"]["fields"]["issueType"]["name"] = "任意重命名"
        self.assertEqual(self.classify("[ECW6700-1] adjust lifecycle", jira)["decisionSource"], "jira-type")
        self.assertTrue(c.needs_git_evidence(self.classify("[ECW6700-1] adjust lifecycle", jira)))
        self.assertFalse(c.needs_git_evidence(self.classify("[ECW6700-1] fix crash", jira)))
        decision = self.classify("[ECW6700-1] 新增命令", jira)
        self.assertEqual(decision["category"], "fixes")
        self.assertIn("jira_git_conflict", decision["reasonCodes"])
        self.assertIn("jira_category_fallback", decision["reasonCodes"])

    def test_customer_issue_type_is_fix_for_configured_projects(self):
        for project in ("ECW6700", "ECW6630", "ECW6280"):
            key = project + "-1"
            jira = report({key: "10603"})
            jira["results"][key]["fields"]["issueType"]["name"] = "客户问题"
            with self.subTest(project=project):
                for subject in ("adjust connection", "优化连接流程", "连接无法建立"):
                    decision = self.classify("[" + key + "] " + subject, jira)
                    self.assertEqual(decision["category"], "fixes")
                    self.assertEqual(decision["decisionSource"], "jira-type")
                self.assertNotIn("unmapped_issue_type", self.classify("[" + key + "] adjust connection", jira)["reasonCodes"])
                jira["results"][key]["fields"]["issueType"]["name"] = "任意名称"
                self.assertEqual(self.classify("[" + key + "] adjust connection", jira)["category"], "fixes")
        # 客户送样单等其他类型不能因名称包含“客户”而被判为缺陷。
        other = report({"ECW6700-1": "13003"})
        other["results"]["ECW6700-1"]["fields"]["issueType"]["name"] = "客户送样单"
        self.assertEqual(self.classify("[ECW6700-1] update", other)["category"], "unclassified")

    def test_jira_field_overrides_type_missing_and_unknown_block(self):
        jira = report({"ECW6700-1": "11101"}, category_field="customfield_123")
        row = jira["results"]["ECW6700-1"]
        row["fields"]["releaseCategory"] = {"id": "1", "value": "New feature"}
        self.assertEqual(self.classify("[ECW6700-1] 新增命令", jira)["decisionSource"], "jira-field")
        row["fields"]["releaseCategory"]["id"] = "404"
        self.assertEqual(self.classify("[ECW6700-1] 修复崩溃", jira)["category"], "unclassified")
        row["fields"]["releaseCategory"] = None
        for warning in ("category_field_missing", "invalid_category_field", "canonical_project_not_configured"):
            row["warnings"] = [warning]
            self.assertEqual(self.classify("[ECW6700-1] 修复崩溃", jira)["category"], "unclassified")

    def test_verified_project_types_and_requirement_fallback(self):
        for project in ("ECW6700", "ECW6630", "ECW6280"):
            key = project + "-1"
            for type_id, category in (("11100", "fixes"), ("11101", "fixes"), ("10100", "improvements")):
                with self.subTest(project=project, type_id=type_id):
                    decision = self.classify("[" + key + "] adjust lifecycle", report({key: type_id}))
                    self.assertEqual(decision["category"], category)
                    self.assertEqual(decision["decisionSource"], "jira-type")
                    self.assertNotIn("unmapped_issue_type", decision["reasonCodes"])
            requirements = ("10604", "10512", "10509") if project == "ECW6630" else ("10604",)
            for type_id in requirements:
                with self.subTest(project=project, type_id=type_id):
                    jira = report({key: type_id})
                    fallback = self.classify("[" + key + "] sync code", jira)
                    self.assertEqual(fallback["category"], "features")
                    self.assertIn("jira_category_fallback", fallback["reasonCodes"])
                    self.assertTrue(fallback["needsReview"])
                    explicit = self.classify("[" + key + "] fix crash", jira)
                    self.assertEqual((explicit["category"], explicit["decisionSource"]), ("fixes", "git-rule"))

    def test_ambiguous_types_are_not_forced_into_release_categories(self):
        for project, type_ids in (("ECW6700", ("10503", "10512", "10509")),
                                 ("ECW6630", ("10006", "11501")),
                                 ("ECW6280", ("10503", "10512", "10509"))):
            key = project + "-1"
            for type_id in type_ids:
                with self.subTest(project=project, type_id=type_id):
                    self.assertEqual(self.classify("[" + key + "] adjust lifecycle", report({key: type_id}))["category"], "unclassified")

    def test_project_task_fallback_preserves_explicit_and_blocked_categories(self):
        for project in ("ECW6700", "ECW6630", "ECW6280"):
            key, other = project + "-36", project + "-37"
            for type_id in ("10007", "10304"):
                with self.subTest(project=project, type_id=type_id):
                    jira = report({key: type_id})
                    for subject in ("add ad9613 PWDN always ok", "add MCP4811 for CFO"):
                        commit = {"hash": "a" * 40, "subject": "[" + key + "] " + subject}
                        decision = c.classify_commit(commit, jira=jira)
                        self.assertEqual(decision["category"], "features")
                        self.assertIn("jira_category_fallback", decision["reasonCodes"])
                        self.assertTrue(decision["needsReview"])
                        entry = {"id": "manual", "category": "adaptations", "issues": [key], "commits": [commit["hash"]]}
                        manual = c.manual_decisions([entry], [commit], {commit["hash"]: decision})["manual"]
                        self.assertEqual((manual["category"], manual["decisionSource"]), ("adaptations", "manual"))
                    for subject, category in (("fix crash", "fixes"), ("optimize throughput", "improvements"), ("update build script", "adaptations")):
                        decision = self.classify("[" + key + "] " + subject, jira)
                        self.assertEqual((decision["category"], decision["decisionSource"]), (category, "git-rule"))
                    for subject in ('Merge "[' + key + '] add MCP4811 for CFO" into develop', 'Revert "[' + key + '] add MCP4811 for CFO"'):
                        decision = self.classify(subject, jira)
                        self.assertEqual(decision["category"], "unclassified")
                        self.assertIn("requires_net_effect_review", decision["reasonCodes"])
                    conflict = self.classify("[" + key + "][" + other + "] sync code", report({key: type_id, other: "11101"}))
                    self.assertEqual(conflict["category"], "unclassified")
                    self.assertIn("multiple_issue_categories", conflict["reasonCodes"])

    def test_ecr_live_type_ids_cover_requirement_improvement_and_defects(self):
        config_path = TOOLS / "examples/jira-config.ecr.example.json"
        # ID/名称来自项目元数据；事项与提交为模拟数据，不依赖事项单号。
        types = {"10604": ("需求", "features"), "10100": ("改进", "improvements"),
                 "10102": ("缺陷", "fixes"), "10603": ("客户问题", "fixes"),
                 "10007": ("任务", "features"), "10304": ("子任务", "features")}
        for project in ("ECR6600", "ECR6600A"):
            cases = {**types, **({"11100": ("IC缺陷", "fixes")} if project == "ECR6600A" else {})}
            for type_id, (name, category) in cases.items():
                with self.subTest(project=project, type_id=type_id):
                    key = project + "-1"
                    jira = report({key: type_id}, config_path=config_path)
                    jira["results"][key]["fields"]["issueType"]["name"] = name
                    decision = self.classify("[" + key + "] sync platform code", jira)
                    self.assertEqual((decision["category"], decision["decisionSource"]), (category, "jira-type"))
                    self.assertNotIn("unmapped_issue_type", decision["reasonCodes"])

    def test_ecr_empty_maps_reproduce_requirement_and_improvement_gap(self):
        for project in ("ECR6600", "ECR6600A"):
            for type_id, expected in (("10604", "features"), ("10100", "improvements")):
                with self.subTest(project=project, type_id=type_id):
                    key = project + "-1"
                    jira = report({key: type_id}, config_path=TOOLS / "examples/jira-config.ecr.example.json")
                    empty = copy.deepcopy(jira)
                    empty["configSnapshot"]["projects"][project] = {"issueTypeMap": {}, "fallbackIssueTypeMap": {}}
                    text = "[" + key + "] sync platform code"
                    before = self.classify(text, empty)
                    self.assertEqual(before["category"], "unclassified")
                    self.assertIn("unmapped_issue_type", before["reasonCodes"])
                    self.assertEqual(self.classify(text, jira)["category"], expected)

    def test_ecr_fallback_preserves_explicit_manual_and_conflict_boundaries(self):
        config_path = TOOLS / "examples/jira-config.ecr.example.json"
        for project in ("ECR6600", "ECR6600A"):
            key = project + "-1"
            for type_id in ("10604", "10007", "10304"):
                jira = report({key: type_id}, config_path=config_path)
                for text, expected in (("fix crash", "fixes"), ("optimize throughput", "improvements"),
                                       ("update build script", "adaptations")):
                    with self.subTest(project=project, type_id=type_id, text=text):
                        decision = self.classify("[" + key + "] " + text, jira)
                        self.assertEqual((decision["category"], decision["decisionSource"]), (expected, "git-rule"))
                for action in ("Merge", "Revert"):
                    decision = self.classify(action + ' "[' + key + '] add feature"', jira)
                    self.assertEqual(decision["category"], "unclassified")
                    self.assertIn("requires_net_effect_review", decision["reasonCodes"])
                row = {"hash": "a" * 40, "subject": "[" + key + "] sync platform code"}
                entry = {"id": "manual", "category": "adaptations", "issues": [key], "commits": [row["hash"]]}
                manual = c.manual_decisions([entry], [row], {row["hash"]: c.classify_commit(row, jira=jira)})["manual"]
                self.assertEqual((manual["category"], manual["decisionSource"]), ("adaptations", "manual"))
        jira = report({"ECR6600-1": "10604", "ECR6600A-2": "10100"}, config_path=config_path)
        conflict = self.classify("[ECR6600-1][ECR6600A-2] sync platform code", jira)
        self.assertEqual(conflict["category"], "unclassified")
        self.assertIn("multiple_issue_categories", conflict["reasonCodes"])
        for project, type_id in (("ECR6600", "10006"), ("ECR6600A", "99999")):
            key = project + "-1"
            jira = report({key: type_id}, config_path=config_path)
            self.assertEqual(self.classify("[" + key + "] sync platform code", jira)["category"], "unclassified")

    def test_multiple_issue_conflicts_and_partial_evidence(self):
        jira = report({"ECW6700-1": "11101", "ECW6280-2": "10100"})
        text = "[ECW6700-1][ECW6280-2] adjust lifecycle"
        self.assertIn("multiple_issue_categories", self.classify(text, jira)["reasonCodes"])
        partial = report({"ECW6700-1": "11101", "ECW6280-2": "10100"}, failed=("ECW6280-2",))
        self.assertEqual(self.classify(text, partial)["category"], "unclassified")
        fixed = self.classify(text.replace("adjust", "fix"), partial)
        self.assertEqual(fixed["decisionSource"], "git-rule")
        self.assertTrue(fixed["needsReview"])
        self.assertIn("partial_issue_evidence", fixed["reasonCodes"])

    def test_body_evidence_does_not_use_paths_or_jira_description(self):
        jira = report({"ECW6700-1": "10007"})
        jira["results"]["ECW6700-1"]["fields"]["descriptionText"] = "fix crash"
        self.assertEqual(self.classify("[ECW6700-1] update", jira)["category"], "features")
        self.assertEqual(self.classify("[ECW6700-1] update", jira, body="fix crash")["category"], "fixes")
        self.assertEqual(c.classify_commit({"subject": "update"}, {**c.empty_evidence(), "paths": ["docs/fix.md"]})["category"], "unclassified")

    def test_seven_reported_entries_fallback_without_rewriting_text(self):
        fixture = core.read_json(TOOLS / "test/fixtures/classification-sample.json")["records"]
        expected = {"1460": "fixes", "342": "fixes", "868": "fixes", "1407": "improvements",
                    "1470": "fixes", "1432": "fixes", "1475": "features"}
        rows = [x for x in fixture if any("ECW6700-" + key in x["jiraTypes"] for key in expected) and
                ("ECW6700-1475" not in x["jiraTypes"] or "RAM" in x["subject"])]
        self.assertEqual(len(rows), 7)
        jira = report({k: v for x in rows for k, v in x["jiraTypes"].items()})
        for row in rows:
            key = next(iter(row["jiraTypes"]))
            decision = c.classify_commit(row, jira=jira)
            with self.subTest(key=key):
                self.assertEqual(decision["category"], expected[key.rsplit("-", 1)[1]])
                self.assertEqual(decision["decisionSource"], "jira-type")
                self.assertIn("jira_category_fallback", decision["reasonCodes"])
                self.assertTrue(decision["needsReview"])
                identity = {"previous": {"commit": "a" * 40}, "target": {"commit": row["hash"]}}
                before = core.automatic_content([row], identity)["entries"][0]
                after = core.automatic_content([row], identity, {row["hash"]: decision})["entries"][0]
                for field in ("id", "text", "issues", "commits", "validation"):
                    self.assertEqual(before[field], after[field])

    def test_fallback_requires_all_keys_mapped_and_consistent(self):
        jira = report({"ECW6700-1": "10604", "ECW6700-2": "11101"})
        text = "[ECW6700-1][ECW6700-2] sync code"
        decision = self.classify(text, jira)
        self.assertEqual(decision["category"], "unclassified")
        self.assertIn("multiple_issue_categories", decision["reasonCodes"])
        self.assertEqual({x["category"] for x in decision["jiraCandidates"]}, {"features", "fixes"})
        for source in (report({"ECW6700-1": "10604", "ECW6700-2": "11101"}, failed=("ECW6700-2",)),
                       report({"ECW6700-1": "10604", "ECW6700-2": "10503"}), None):
            self.assertEqual(self.classify(text, source)["category"], "unclassified")
        same = report({"ECW6700-1": "10604", "ECW6700-2": "10604"})
        self.assertEqual(self.classify(text, same)["category"], "features")

    def test_fallback_config_optional_project_scoped_and_manual_priority(self):
        jira = report({"ECW6700-1": "10604"})
        row = {"hash": "a" * 40, "subject": "[ECW6700-1] 重构流程并新增配置目标"}
        decision = c.classify_commit(row, jira=jira)
        self.assertEqual(decision["category"], "features")
        entry = {"id": "manual", "category": "adaptations", "issues": ["ECW6700-1"], "commits": [row["hash"]]}
        manual = c.manual_decisions([entry], [row], {row["hash"]: decision})["manual"]
        self.assertEqual((manual["category"], manual["decisionSource"]), ("adaptations", "manual"))
        del jira["configSnapshot"]["projects"]["ECW6700"]["fallbackIssueTypeMap"]
        self.assertEqual(c.classify_commit(row, jira=jira)["category"], "unclassified")
        other = report({"ECW6280-1": "10604"})
        del other["configSnapshot"]["projects"]["ECW6280"]["fallbackIssueTypeMap"]
        self.assertEqual(self.classify("[ECW6280-1] sync code", other)["category"], "unclassified")

    def test_manual_mapping_split_merge_exclusion_and_render_counters(self):
        raw = [{"hash": str(i) * 40, "subject": "[ECW6700-1] fix crash"} for i in range(1, 4)]
        decisions = {x["hash"]: c.classify_commit(x) for x in raw}
        entries = [{"id": "A", "category": "fixes", "issues": ["ECW6700-1", "ECW6280-2"], "commits": [raw[0]["hash"], raw[1]["hash"]]},
                   {"id": "B", "category": "features", "issues": ["ECW6700-1"], "commits": [raw[1]["hash"]]}]
        content = {"entries": entries, "customerExclusions": [{"hash": raw[2]["hash"], "reason": "outside product", "reviewer": "fixture"}]}
        audit = c.manual_decisions(entries, raw, decisions)
        rendered = {"releaseNotes": ["A", "B"], "githubReleaseBody": ["B", "A"]}
        coverage, stats = c.customer_coverage(raw, content, audit, ["A"], rendered)
        self.assertEqual(coverage["status"], "complete")
        self.assertEqual(stats["representedCommitCount"], 2)
        self.assertEqual(stats["customerEntryCount"], 2)
        self.assertEqual(stats["excludedCommitCount"], 1)
        self.assertEqual(stats["issuePrefixEntryCounts"], {"ECW6700": 2, "ECW6280": 1})
        self.assertEqual(audit["B"]["decisionSource"], "manual")
        for bad in (["A"], ["A", "A", "B"], ["A", "B", "UNKNOWN"]):
            with self.assertRaisesRegex(c.ClassificationError, "渲染"):
                c.customer_coverage(raw, content, audit, rendered={**rendered, "releaseNotes": bad})
        for exclusions in ([content["customerExclusions"][0]] * 2, [{"hash": raw[0]["hash"], "reason": "x", "reviewer": "x"}], [{"hash": "z" * 40, "reason": "x", "reviewer": "x"}]):
            with self.assertRaises(c.ClassificationError):
                c.customer_coverage(raw, {**content, "customerExclusions": exclusions}, audit)
        with self.assertRaisesRegex(c.ClassificationError, "自动客户覆盖"):
            c.customer_coverage(raw, {"entries": entries}, audit, automatic=True)

    def snapshot(self):
        identity = {"previous": {"commit": "a" * 40}, "target": {"commit": "b" * 40}}
        raw = [{"hash": "b" * 40, "subject": "[ECW6700-1] fix crash"}]
        source = {"generate_release_notes.py": "c" * 64, "jira_client.py": "d" * 64, "release_classification.py": "e" * 64}
        data = {"rawCommits": raw, "issueKeys": ["ECW6700-1"], "ruleVersion": c.RULE_VERSION, "sourceHashes": source,
                "gitEvidence": {"b" * 40: c.empty_evidence()}, "historyEvidence": {"ruleVersion": core.release_history.RULE_VERSION}}
        jira = report({"ECW6700-1": "11101"})
        jira["snapshotDigest"] = c.snapshot_digest(identity, data, jira)
        return {"schemaVersion": 3, "status": "draft", "identity": identity, "classification": data, "jira": jira}

    def test_snapshot_replay_is_offline_and_recomputes_decisions(self):
        saved = self.snapshot()
        saved["customerEntries"] = [{"category": "wrong"}]
        data = saved["classification"]
        with mock.patch.dict("os.environ", {}, clear=True), mock.patch.object(core.jira_client, "load_jira_config", side_effect=AssertionError("online loader")):
            jira, evidence = c.replay_snapshot(saved, saved["identity"], data["rawCommits"], data["sourceHashes"], "source hash")
        self.assertEqual(jira["mode"], "snapshot")
        self.assertEqual(jira["sourceManifestSha256"], "source hash")
        self.assertEqual(c.classify_commit(data["rawCommits"][0], evidence["b" * 40], jira)["category"], "fixes")

    def test_snapshot_optional_assignee_validation(self):
        saved = self.snapshot()
        jira = saved["jira"]
        c.validate_snapshot_report(jira, ["ECW6700-1"])
        for owner in (None, {"name": "E0001", "displayName": "研发甲"}):
            jira["results"]["ECW6700-1"]["fields"]["assignee"] = owner
            c.validate_snapshot_report(jira, ["ECW6700-1"])
        for owner in ("E0001", {"name": 1, "displayName": "研发甲"}, {"name": "dev", "displayName": ""},
                      {"name": "dev", "displayName": "研发甲", "emailAddress": "private@example.invalid"}):
            jira["results"]["ECW6700-1"]["fields"]["assignee"] = owner
            with self.subTest(owner=owner), self.assertRaisesRegex(c.ClassificationError, "处理人"):
                c.validate_snapshot_report(jira, ["ECW6700-1"])

    def test_snapshot_mismatch_and_corruption(self):
        original = self.snapshot()
        changes = [("schemaVersion", 2), ("status", "failed"), ("jira.mode", "disabled"),
                   ("classification.ruleVersion", "changed"), ("classification.sourceHashes", {}),
                   ("classification.rawCommits", []), ("classification.issueKeys", []),
                   ("classification.gitEvidence", {}), ("classification.historyEvidence", {}), ("jira.snapshotDigest", "bad"),
                   ("jira.results", {}), ("jira.configSnapshot.auth", {}),
                   ("jira.configSnapshot.projects.ECW6700.fallbackIssueTypeMap", {"10604": "unknown"}),
                   ("jira.configSnapshot.projects.ECW6700.fallbackIssueTypeMap", {"Requirement": "features"}),
                   ("jira.results.ECW6700-1.fields.issueType", {"id": 1, "name": "bug"})]
        for path, value in changes:
            saved = copy.deepcopy(original)
            obj = saved
            parts = path.split(".")
            for key in parts[:-1]:
                obj = obj[key]
            obj[parts[-1]] = value
            with self.subTest(path=path), self.assertRaises(c.ClassificationError):
                c.replay_snapshot(saved, original["identity"], original["classification"]["rawCommits"], original["classification"]["sourceHashes"], "hash")

    def test_snapshot_partial_failures_preserved(self):
        saved = self.snapshot()
        saved["jira"] = report({"ECW6700-1": "11101"}, failed=("ECW6700-1",))
        saved["jira"]["snapshotDigest"] = c.snapshot_digest(saved["identity"], saved["classification"], saved["jira"])
        jira, _ = c.replay_snapshot(saved, saved["identity"], saved["classification"]["rawCommits"], saved["classification"]["sourceHashes"], "hash")
        self.assertEqual(jira["status"], "unavailable")
        self.assertEqual(jira["results"]["ECW6700-1"]["reasonCode"], "forbidden")


if __name__ == "__main__":
    unittest.main(verbosity=2)
