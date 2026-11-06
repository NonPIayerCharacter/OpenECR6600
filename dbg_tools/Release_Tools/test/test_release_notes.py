"""Serial integration checks against isolated Git repositories; no SDK history changes."""
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from test_release_classification import report as jira_report

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("release_notes", TOOLS / "generate_release_notes.py")
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)
RUNTIME_ROOT = str(Path.home() / ".ecw-release-tools")


class ReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="ecw-release-tests-"))
        cls.repo = cls.root / "SDK 中文 space"
        cls.tools = cls.repo / "dbg_tools/Release_Tools"
        shutil.copytree(TOOLS, cls.tools, ignore=shutil.ignore_patterns("release", "__pycache__", "*.pyc"))
        cls.runner = core.Runner(cls.repo)
        cls.runner.git("init")
        cls.runner.git("config", "user.name", "Release Test")
        cls.runner.git("config", "user.email", "release-test@example.invalid")
        cls.runner.git("config", "commit.gpgsign", "false")
        cls.runner.git("config", "tag.gpgsign", "false")
        cls.counter = 0
        cls.base = cls.commit("baseline")
        cls.runner.git("tag", "-a", "Z_BASE", "-m", "baseline")
        cls.feature = cls.commit("Add a configuration target")
        cls.fix = cls.commit("[ECW6700-1] Fix connection close")
        cls.target = cls.commit("Merge pull request #12 from test/topic\n\nIntegrate feature")
        cls.runner.git("tag", "A_TARGET")
        cls.content = {k: "" for k in core.FIELDS}
        cls.content.update({"previousSha": cls.base, "targetSha": cls.target, "product": "ECW test", "summary": "Original summary",
                            "entries": [
                                {"id": "N", "category": "features", "text": "新增配置目标", "issues": ["ECW6700-2"], "commits": [cls.feature], "validation": "待验证"},
                                {"id": "I", "category": "improvements", "text": "优化内存布局", "issues": ["ECW6700-2"], "commits": [cls.feature], "validation": "待验证"},
                                {"id": "F", "category": "fixes", "text": "修复关闭连接问题", "issues": ["ECW6700-1"], "commits": [cls.fix], "validation": "待复测"}]})
        cls.content_path = cls.root / "人工 input.json"
        core.write_json(cls.content_path, cls.content)

    @classmethod
    def commit(cls, message):
        cls.counter += 1
        core.write_text(cls.repo / "tracked.txt", str(cls.counter))
        cls.runner.git("add", "tracked.txt")
        cls.runner.git("commit", "-m", message)
        return cls.runner.git("rev-parse", "HEAD")[0].strip()

    def options(self, **overrides):
        options = core.parser().parse_args(["--from", "Z_BASE", "--to", "A_TARGET", "--runtime-root", RUNTIME_ROOT])
        options.no_jira = not (overrides.get("jira_config") or overrides.get("jira_snapshot"))
        for key, value in overrides.items():
            setattr(options, key, value)
        return options

    def generate(self, **kwargs):
        return Path(core.generate(self.options(**kwargs), self.tools)["batch"])

    def identity(self):
        return {"previous": core.resolve_ref(self.runner, "Z_BASE"), "target": core.resolve_ref(self.runner, "A_TARGET")}

    def raw(self):
        return [{"hash": h, "subject": "test"} for h in (self.feature, self.fix, self.target)]

    def test_four_range_combinations(self):
        for a in ("Z_BASE", self.base):
            for b in ("A_TARGET", self.target):
                with self.subTest(a=a, b=b):
                    batch = self.generate(from_ref=a, to_ref=b)
                    record = core.read_json(batch / "release-data.json")[0]
                    self.assertEqual(record["diff"], self.base + ".." + self.target)
                    self.assertEqual(record["tag"], "A_TARGET" if b == "A_TARGET" else None)
                    manifest = core.read_json(batch / core.MANIFEST)
                    self.assertEqual(manifest["checks"]["representedCount"], 3)
                    self.assertEqual(len(list(batch.iterdir())), 8)
                    self.assertEqual(manifest["customerContentSource"], "git-subjects")
                    self.assertEqual(len(manifest["customerEntries"]), 3)
                    for prefix in ("Release_Notes_", "github-release-body_"):
                        text = next(batch.glob(prefix + "*.md")).read_text(encoding="utf-8")
                        self.assertIn("Add a configuration target", text)
                        self.assertIn("Fix connection close (`ECW6700-1`)", text)
                        self.assertIn("待分类变更", text)
                        self.assertNotIn("重要变更", text)

    def test_important_moves_only_selected_ids(self):
        batch = self.generate(content=str(self.content_path), important=["N", "F", "N"])
        for name in ("Release_Notes_A_TARGET.md", "github-release-body_A_TARGET.md"):
            text = (batch / name).read_text(encoding="utf-8")
            for e in self.content["entries"]:
                self.assertEqual(text.count(e["text"]), 1)
            self.assertEqual(text.count("ECW6700-2"), 2)
        text = (batch / "Release_Notes_A_TARGET.md").read_text(encoding="utf-8")
        self.assertIn("## 2. 重要变更", text)
        self.assertIn("## 3. 新增功能", text)
        self.assertIn("## 3. 新增功能\n\n\n## 4.", text)
        self.assertLess(text.index("新增配置目标"), text.index("## 3."))
        self.assertGreater(text.index("优化内存布局"), text.index("## 4."))
        manifest = core.read_json(batch / core.MANIFEST)
        self.assertEqual(manifest["importantIds"], ["N", "F"])
        self.assertEqual(len(manifest["customerEntries"]), 3)
        self.assertEqual(manifest["customerContentSource"], "manual")
        self.assertEqual(manifest["inputs"]["content"], self.content)
        for name, sha in manifest["artifacts"].items():
            self.assertEqual(core.file_hash(batch / name), sha)
        self.assertEqual(len(manifest["artifacts"]), 7)

    def test_no_important_preserves_sections_and_summary(self):
        content, important = core.merge_content(self.content, None, [], self.raw(), self.identity())
        text, _ = core.render(content, important, self.identity(), "A_TARGET")
        self.assertIn("## 2. 新增功能", text)
        self.assertNotIn("## 2. 重要变更", text)
        self.assertIn("Original summary", text)
        automatic, ids = core.merge_content(None, None, [], self.raw(), self.identity())
        self.assertFalse(ids)
        self.assertEqual(len(automatic["entries"]), 3)
        self.assertIn("3 条 Git 提交", core.render(automatic, ids, self.identity(), "A_TARGET")[0])
        self.assertNotIn("待分类变更", text)
        for github in (False, True):
            for empty in (None, "", "  ", "待填写"):
                blank = {**content, **{key: empty for key in core.FIELDS}, "entries": []}
                text, rendered = core.render(blank, [], self.identity(), "A_TARGET", github=github)
                self.assertEqual(rendered, [])
                self.assertNotIn("待填写", text)
                self.assertNotIn("待人工确认，未列出条目", text)
                self.assertIn("### 新增功能\n\n\n### 功能优化" if github else "## 2. 新增功能\n\n\n## 3. 功能优化", text)
                self.assertRegex(text, r"## Known Issues\n\s*\n## Changelog" if github else r"## 7\. 已知问题与限制\n\s*\n## 8\.")

    def test_generated_metadata_defaults_and_explicit_manual_override(self):
        before = core.datetime.now().astimezone().date().isoformat()
        result = core.generate(self.options(), self.tools)
        batch = Path(result["batch"])
        after = core.datetime.now().astimezone().date().isoformat()
        self.assertIn(result["releaseDate"], {before, after})
        self.assertEqual(result["scope"], "ECW6630、ECW6700")
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["customerMetadata"], {"releaseDate": result["releaseDate"], "scope": result["scope"]})
        self.assertEqual(result["reviewRequiredEntryCount"], data["customerStatistics"]["reviewRequiredEntryCount"])
        self.assertEqual(result["jiraFallbackEntryCount"], 0)
        for name in ("Release_Notes_A_TARGET.md", "github-release-body_A_TARGET.md"):
            text = (batch / name).read_text(encoding="utf-8")
            self.assertIn("发布日期：" + result["releaseDate"], text)
            self.assertIn("ECW6630、ECW6700", text)
        content = {**self.content, "releaseDate": "2026-11-01", "scope": "ECW6700 / test board"}
        path = self.root / "metadata.json"
        core.write_json(path, content)
        manual = self.generate(content=str(path))
        for name in ("Release_Notes_A_TARGET.md", "github-release-body_A_TARGET.md"):
            text = (manual / name).read_text(encoding="utf-8")
            self.assertIn("发布日期：2026-11-01", text)
            self.assertIn("ECW6700 / test board", text)
        self.assertEqual(core.read_json(manual / core.MANIFEST)["inputs"]["content"], content)

    def test_customer_issue_type_full_generation(self):
        config = self.tools / "examples/jira-config.example.json"
        baseline = self.runner.git("rev-parse", "HEAD")[0].strip()
        commit = self.commit("[ECW6700-1] 客户连接问题")
        jira = jira_report({"ECW6700-1": "10603"})
        jira["results"]["ECW6700-1"]["fields"]["issueType"]["name"] = "客户问题"
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            result = core.generate(self.options(from_ref=baseline, to_ref=commit, jira_config=str(config)), self.tools)
        batch = Path(result["batch"])
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["classification"]["entries"]["AUTO_" + commit]["category"], "fixes")
        self.assertEqual(len(list(batch.iterdir())), 8)
        text = next(batch.glob("Release_Notes_*.md")).read_text(encoding="utf-8")
        section = text.split("## 4. 问题修复", 1)[1].split("## 5.", 1)[0]
        self.assertIn("客户连接问题 (`ECW6700-1`)", section)
        self.assertIn("ECW6630、ECW6700", text)
        self.assertTrue(data["checks"]["customerCoverageComplete"])

    def test_automatic_candidates_conservative_and_traceable(self):
        cases = [
            ("[ECW6700-1] 新增串口配置", "features"),
            ("[ECW6700-1] 优化发送流程", "improvements"),
            ("[ECW6280-9] 修复连接问题", "fixes"),
            ("feat(wifi): add scan option", "features"),
            ("fix(at)!: correct close reporting", "fixes"),
            ("build: update sdk packaging", "adaptations"),
            ("[ECW6700-2] update configuration", "unclassified"),
            ("Merge branch 'fix/socket'", "unclassified"),
            ('Revert "feat: add scan option"', "unclassified"),
            ("新增配置，并修复旧问题", "unclassified"),
            ("重构发送流程并新增限速功能", "unclassified"),
            ("[ECW6700-3] 修复 `CIPCLOSE=5` 重复上报 `0,CLOSED`", "fixes"),
        ]
        raw = [{"hash": f"{i + 1:040x}", "subject": text} for i, (text, _) in enumerate(cases)]
        content, important = core.merge_content(None, None, [], raw, self.identity())
        self.assertFalse(important)
        self.assertEqual([e["category"] for e in content["entries"]], [c for _, c in cases])
        self.assertEqual([e["commits"][0] for e in content["entries"]], [x["hash"] for x in raw])
        self.assertEqual(content["entries"][0]["issues"], content["entries"][1]["issues"])
        self.assertNotEqual(content["entries"][0]["id"], content["entries"][1]["id"])
        self.assertEqual(core.automatic_content(raw[::-1], self.identity())["entries"][0]["id"], content["entries"][-1]["id"])
        core.validate_entries(content, raw, self.identity(), full=True)
        text, _ = core.render(content, [], self.identity(), "A_TARGET")
        self.assertIn("修复 `CIPCLOSE=5` 重复上报 `0,CLOSED` (`ECW6700-3`)", text)
        for e in content["entries"]:
            self.assertIn("未验证", e["validation"])
            self.assertNotIn(e["commits"][0], core.item_line(e))

    def test_automatic_candidates_strip_sha_and_commit_links(self):
        raw = [{"hash": self.fix, "subject": "[ECW6700-1] Fix commit `" + self.feature +
                "` and [patch](https://host/repo/commit/" + self.feature + ") ECW6700-1"}]
        content = core.automatic_content(raw, self.identity())
        entry = content["entries"][0]
        line = core.item_line(entry)
        self.assertNotIn(self.feature, line)
        self.assertNotIn("https://", line)
        self.assertEqual(line.count("ECW6700-1"), 1)
        core.validate_entries(content, raw, self.identity(), full=True)

    def test_automatic_important_ids_and_explicit_empty_manual(self):
        selected = "AUTO_" + self.fix
        batch = self.generate(important=[selected, selected])
        manifest = core.read_json(batch / core.MANIFEST)
        self.assertEqual(manifest["importantIds"], [selected])
        self.assertEqual(len(manifest["customerEntries"]), 3)
        for prefix in ("Release_Notes_", "github-release-body_"):
            text = next(batch.glob(prefix + "*.md")).read_text(encoding="utf-8")
            self.assertEqual(text.count("Fix connection close"), 1)
        empty = {**self.content, "entries": []}
        content, ids = core.merge_content(empty, None, [], self.raw(), self.identity())
        self.assertEqual(content, empty)
        self.assertNotIn("待分类变更", core.render(content, ids, self.identity(), "A_TARGET")[0])

    def test_important_file_independent_and_empty(self):
        data = {"previousSha": self.base, "targetSha": self.target, "entries": [self.content["entries"][2]]}
        p = self.root / "important.json"
        p.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8-sig")
        batch = self.generate(important_file=str(p))
        text = (batch / "Release_Notes_A_TARGET.md").read_text(encoding="utf-8")
        self.assertIn("## 1. 版本概述\n\n\n", text)
        self.assertNotIn("待填写", text)
        self.assertIn("## 2. 重要变更", text)
        self.assertEqual(len(core.read_json(batch / core.MANIFEST)["customerEntries"]), 1)
        content, ids = core.merge_content(self.content, {**data, "entries": []}, [], self.raw(), self.identity())
        self.assertFalse(ids)
        self.assertEqual(content, self.content)
        automatic, ids = core.merge_content(None, {**data, "entries": []}, [], self.raw(), self.identity())
        self.assertFalse(ids)
        self.assertEqual(len(automatic["entries"]), 3)

    def test_important_conflict_invalid_mapping_and_mutual_exclusion(self):
        data = {"previousSha": self.base, "targetSha": self.target, "entries": [copy.deepcopy(self.content["entries"][0])]}
        merged, ids = core.merge_content(self.content, data, [], self.raw(), self.identity())
        self.assertEqual(len(merged["entries"]), 3)
        self.assertEqual(ids, ["N"])
        for field, value in (("text", "不同文字"), ("commits", [self.base]), ("category", "invalid"), ("validation", "")):
            bad = copy.deepcopy(data)
            bad["entries"][0][field] = value
            with self.subTest(field=field), self.assertRaises(core.ReleaseError):
                core.merge_content(self.content, bad, [], self.raw(), self.identity())
        with self.assertRaises(core.ReleaseError):
            core.merge_content(self.content, None, ["missing"], self.raw(), self.identity())
        with self.assertRaises(core.ReleaseError):
            core.merge_content(self.content, data, ["N"], self.raw(), self.identity())

    def test_refs_explicit_short_annotated_and_branch_name(self):
        self.assertEqual(core.resolve_ref(self.runner, self.base[:9])["commit"], self.base)
        self.assertNotEqual(core.resolve_ref(self.runner, "Z_BASE")["object"], self.base)
        self.runner.git("branch", "A_TARGET", self.target)
        try:
            self.assertEqual(core.resolve_ref(self.runner, "A_TARGET")["commit"], self.target)
        finally:
            self.runner.git("branch", "-D", "A_TARGET")
        abbreviation = self.fix[:9]
        self.runner.git("tag", abbreviation, self.target)
        try:
            with self.assertRaises(core.ReleaseError):
                core.resolve_ref(self.runner, abbreviation)
            self.assertEqual(core.resolve_ref(self.runner, "tag:" + abbreviation)["commit"], self.target)
            self.assertEqual(core.resolve_ref(self.runner, "commit:" + abbreviation)["commit"], self.fix)
        finally:
            self.runner.git("tag", "-d", abbreviation)

    def test_bad_refs_and_objects(self):
        blob = self.runner.git("rev-parse", self.target + ":tracked.txt")[0].strip()
        for ref in ("HEAD", "HEAD~1", "HEAD..HEAD", "missing", "commit:" + blob, "tag:" + self.base):
            with self.subTest(ref=ref), self.assertRaises(core.ReleaseError):
                core.resolve_ref(self.runner, ref)

    def test_failed_batches_and_no_overwrite(self):
        first = self.generate()
        digest = core.inventory(first)
        second = self.generate()
        self.assertNotEqual(first, second)
        self.assertEqual(core.inventory(first), digest)
        for args in ({"from_ref": self.target, "to_ref": self.base}, {"from_ref": self.base, "to_ref": self.base}, {"to_ref": "missing"}):
            with self.assertRaises(core.ReleaseError):
                self.generate(**args)
        failed = [core.read_json(p / core.MANIFEST) for p in (self.tools / "release").iterdir()]
        self.assertGreaterEqual(sum(x["status"] == "failed" for x in failed), 3)
        for p in (self.tools / "release").iterdir():
            self.assertFalse(p.name.startswith(".pending"))
            self.assertLessEqual(len(list(p.iterdir())), 8)
            self.assertFalse((p / "generator.log").exists())

    def test_shallow_repo_rejected(self):
        marker = self.repo / ".git/shallow"
        core.write_text(marker, self.base + "\n")
        try:
            with self.assertRaisesRegex(core.ReleaseError, "浅克隆"):
                self.generate()
        finally:
            marker.unlink()

    def test_version_names_and_commit_default(self):
        for value in ("CON", "../bad/name", "", "你好", "x" * 200):
            if not value:
                with self.assertRaises(core.ReleaseError):
                    core.safe_version(value)
            else:
                name = core.safe_version(value)
                self.assertNotIn("/", name)
                self.assertNotIn("\\", name)
                self.assertLessEqual(len(name), 104)
        batch = self.generate(to_ref=self.target, version="CON")
        self.assertTrue((batch / "Release_Notes_version_CON.md").exists())
        self.assertIsNone(core.read_json(batch / "release-data.json")[0]["tag"])

    def test_coverage_omission_requires_decision(self):
        hidden = self.commit("2.0.0")
        with self.assertRaisesRegex(core.ReleaseError, "未解释遗漏"):
            self.generate(to_ref=hidden)
        p = self.root / "coverage.json"
        core.write_json(p, {"previousSha": self.base, "targetSha": hidden,
                            "decisions": [{"hash": hidden, "reason": "版本提交", "reviewer": "测试样本"}]})
        batch = self.generate(to_ref=hidden, coverage=str(p))
        self.assertEqual(core.read_json(batch / core.MANIFEST)["checks"]["reviewedOmissions"], 1)

    def test_config_package_and_template_constraints(self):
        pkg = self.repo / "package.json"
        core.write_json(pkg, {"auto-changelog": {}})
        try:
            with self.assertRaisesRegex(core.ReleaseError, "覆盖歧义"):
                self.generate()
        finally:
            pkg.unlink()
        config = self.tools / ".auto-changelog"
        original = config.read_bytes()
        try:
            data = core.read_json(config)
            data["plugins"] = []
            core.write_json(config, data)
            with self.assertRaisesRegex(core.ReleaseError, "不支持的键"):
                self.generate()
        finally:
            config.write_bytes(original)
        template = self.tools / "templates/engineering.zh.hbs"
        original = template.read_bytes()
        try:
            core.write_text(template, "only a title\n")
            with self.assertRaisesRegex(core.ReleaseError, "模板遗漏"):
                self.generate()
        finally:
            template.write_bytes(original)

    def test_input_and_tag_drift(self):
        config = self.tools / ".auto-changelog"
        original = config.read_bytes()
        write = core.write_text
        def mutate(destination, text):
            write(destination, text)
            if Path(destination).name == "coverage-review.md":
                config.write_bytes(original + b"\n")
        try:
            with mock.patch.object(core, "write_text", side_effect=mutate), self.assertRaisesRegex(core.ReleaseError, "输入发生变化"):
                self.generate()
        finally:
            config.write_bytes(original)
        def move_tag(destination, text):
            write(destination, text)
            if Path(destination).name == "coverage-review.md":
                self.runner.git("update-ref", "refs/tags/A_TARGET", self.fix)
        try:
            with mock.patch.object(core, "write_text", side_effect=move_tag), self.assertRaisesRegex(core.ReleaseError, "引用发生变化"):
                self.generate()
        finally:
            self.runner.git("update-ref", "refs/tags/A_TARGET", self.target)

    def test_source_drift_and_missing_runtime(self):
        lock = self.tools / "vendor/auto-changelog/package-lock.json"
        original = lock.read_bytes()
        try:
            lock.write_bytes(original + b"\n")
            with self.assertRaisesRegex(core.ReleaseError, "维护源码"):
                self.generate()
        finally:
            lock.write_bytes(original)
        with self.assertRaisesRegex(core.ReleaseError, "缺少运行回执"):
            self.generate(runtime_root=str(self.root / "missing-runtime"))
        with self.assertRaisesRegex(core.ReleaseError, "仓库之外"):
            self.generate(runtime_root=str(self.repo / "runtime"))

    def test_runtime_receipt_platform_and_dependency_drift(self):
        base = core.read_json(self.tools / "tool-baseline.json")
        parent = Path(RUNTIME_ROOT) / "runtime" / base["snapshotId"]
        receipt = core.read_json(parent / "runtime-manifest.json")
        altered = self.root / "receipt.json"
        for field in ("version", "platform", "arch"):
            core.write_json(altered, {**receipt, "node": {**receipt["node"], field: "invalid"}})
            with self.subTest(field=field), self.assertRaisesRegex(core.ReleaseError, "平台/版本"):
                core.verify_runtime(parent / "auto-changelog", altered, base, receipt["node"])
        core.write_json(altered, {**receipt, "dependenciesSha256": "incorrect"})
        with self.assertRaisesRegex(core.ReleaseError, "依赖发生变化"):
            core.verify_runtime(parent / "auto-changelog", altered, base, receipt["node"])

    def test_node_version_admission_and_missing_capability(self):
        # 模拟版本响应只验证门禁逻辑, 不充当 Node 26 的真实环境验证。
        base, vendor, node, info = core.baseline(self.tools, self.runner)
        runner = mock.Mock()
        for version in ("v12.22.9", "v26.10.0"):
            changed = {**info, "version": version}
            runner.run.return_value = (json.dumps(changed), 0, "")
            self.assertEqual(core.baseline(self.tools, runner)[3], changed)
        runner.run.side_effect = core.ReleaseError("Cannot find module 'fs'")
        with self.assertRaisesRegex(core.ReleaseError, "Node.js 运行能力检查失败"):
            core.baseline(self.tools, runner)

    def test_generation_without_node_prefix_support(self):
        # 在新版 Node 上也禁止 node: 加载, 防止只修预检而遗漏真实生成或旧入口。
        guard = self.root / "legacy-commonjs.cjs"
        core.write_text(guard, "const Module = require('module');\n"
                        "const load = Module._load;\n"
                        "Module._load = function(name, parent, isMain) {\n"
                        "  if (name.startsWith('node:')) throw new Error('Unsupported module: ' + name);\n"
                        "  return load.call(this, name, parent, isMain);\n"
                        "};\n")
        flags = os.environ.get("NODE_OPTIONS", "") + ' --require "' + guard.as_posix() + '"'
        with mock.patch.dict(os.environ, {"NODE_OPTIONS": flags.strip(), "ECW_JIRA_USER": "", "ECW_JIRA_PASSWORD": ""}):
            batch = self.generate()
            self.assertEqual(len(list(batch.iterdir())), 8)
            _, status, stderr = self.runner.run(["node", self.tools / "src/release-tools.js", "generate",
                                      "--previous-tag", "Z_BASE", "--target-tag", "A_TARGET",
                                      "--work-root", RUNTIME_ROOT], allowed=(1,))
            self.assertEqual(status, 1)
            self.assertIn("credentials_missing", stderr)
            self.assertNotIn("Unsupported module", stderr)

    def test_prepare_records_actual_versions_and_reuses_without_npm(self):
        base, vendor, node, info = core.baseline(self.tools, self.runner)
        info = {**info, "version": "v26.10.0"}
        options = self.options(runtime_root=str(self.root / "version-runtime"))
        parent, runtime, receipt_file = core.runtime_paths(options.runtime_root, self.repo, base)
        npm = self.root / "fake-npm/npm.cmd"
        cli = npm.parent / "node_modules/npm/bin/npm-cli.js"
        cli.parent.mkdir(parents=True)
        core.write_text(cli, "// mocked npm entry\n")

        def install(args, **kwargs):
            if args[-1] == "--version":
                version = base["version"] if Path(args[-2]).name == "index.js" else "12.1.0"
                return version + "\n", 0, ""
            # 不联网安装: 用隔离依赖样本验证 prepare -> 回执 -> 复用的完整契约。
            self.assertIn("ci", args)
            dependency = Path(kwargs["cwd"]) / "node_modules/fixture"
            dependency.mkdir(parents=True)
            core.write_text(dependency / "index.js", "module.exports = {}\n")
            return "", 0, ""

        with mock.patch.object(core, "baseline", return_value=(base, vendor, node, info)), \
                mock.patch.object(core.shutil, "which", return_value=str(npm)), \
                mock.patch.object(core.Runner, "run", side_effect=install):
            self.assertFalse(core.prepare(options, self.tools)["reused"])
        receipt = core.read_json(receipt_file)
        self.assertEqual(receipt["npmVersion"], "12.1.0")
        self.assertEqual(receipt["node"], info)
        with mock.patch.object(core, "baseline", return_value=(base, vendor, node, info)), \
                mock.patch.object(core.shutil, "which", side_effect=AssertionError("npm must not be queried")), \
                mock.patch.object(core.Runner, "run", side_effect=AssertionError("npm must not be executed")):
            self.assertTrue(core.prepare(options, self.tools)["reused"])
        for invalid in (None, "", 12, "invalid"):
            core.write_json(receipt_file, {**receipt, "npmVersion": invalid})
            with self.subTest(version=invalid), self.assertRaisesRegex(core.ReleaseError, "有效 npm 版本"):
                core.verify_runtime(runtime, receipt_file, base, info)

    def test_prepare_failure_does_not_write_receipt(self):
        base, vendor, node, info = core.baseline(self.tools, self.runner)
        npm = self.root / "failed-npm/npm.cmd"
        cli = npm.parent / "node_modules/npm/bin/npm-cli.js"
        cli.parent.mkdir(parents=True)
        core.write_text(cli, "// mocked npm entry\n")
        for label, responses in (("invalid-version", [("invalid\n", 0, "")]),
                                 ("failed-ci", [("12.1.0\n", 0, ""), core.ReleaseError("npm ci failed")])):
            options = self.options(runtime_root=str(self.root / label))
            parent, runtime, receipt = core.runtime_paths(options.runtime_root, self.repo, base)
            with self.subTest(label=label), \
                    mock.patch.object(core, "baseline", return_value=(base, vendor, node, info)), \
                    mock.patch.object(core.shutil, "which", return_value=str(npm)), \
                    mock.patch.object(core.Runner, "run", side_effect=responses), self.assertRaises(core.ReleaseError):
                core.prepare(options, self.tools)
            self.assertFalse(receipt.exists())

    def test_subprocess_errors_timeout_and_output(self):
        runner = core.Runner(self.repo, timeout=0.1)
        with self.assertRaisesRegex(core.ReleaseError, "超时"):
            runner.run([sys.executable, "-c", "import time;time.sleep(20)"])
        # 后代继承捕获文件句柄; 若只杀父进程, Windows 清理将因占用失败。
        runner.timeout = 1
        with self.assertRaisesRegex(core.ReleaseError, "超时"):
            runner.run([sys.executable, "-c", "import subprocess,sys,time;subprocess.Popen([sys.executable,'-c','import time;time.sleep(20)']);time.sleep(20)"])
        with self.assertRaisesRegex(core.ReleaseError, "exited 5"):
            self.runner.run([sys.executable, "-c", "import sys;sys.exit(5)"])
        for _ in range(12):
            self.assertIn("git version", self.runner.git("--version")[0])

    def test_argv_input_not_shell_interpolated(self):
        value = "literal $(echo bad); `echo bad` & with spaces"
        output = self.runner.run([sys.executable, "-c", "import sys;print(sys.argv[1])", value])[0].strip()
        self.assertEqual(output, value)

    def test_link_paths_rejected(self):
        link = self.root / "repo-link"
        if os.name == "nt":
            self.runner.run(["node", "-e", "require('fs').symlinkSync(process.argv[1],process.argv[2],'junction')", str(self.repo), str(link)])
        else:
            link.symlink_to(self.repo, target_is_directory=True)
        try:
            with self.assertRaises(core.ReleaseError):
                core.physical_path(link / "outside-looking")
        finally:
            if os.name == "nt":
                os.rmdir(link)
            else:
                link.unlink()

    def test_invalid_important_files_and_drift(self):
        p = self.root / "important-validation.json"
        for payload in (b"", b"{broken", b"\xff\xfe\xfa"):
            p.write_bytes(payload)
            with self.assertRaises(core.ReleaseError):
                self.generate(important_file=str(p))
        core.write_json(p, {"previousSha": self.base, "targetSha": self.target, "entries": [self.content["entries"][2]]})
        original = p.read_bytes()
        write = core.write_text
        def mutate(destination, text):
            write(destination, text)
            if Path(destination).name == "coverage-review.md":
                p.write_bytes(original + b"\n")
        with mock.patch.object(core, "write_text", side_effect=mutate), self.assertRaisesRegex(core.ReleaseError, "输入发生变化"):
            self.generate(important_file=str(p))

    def test_cli_from_other_cwd_and_compatibility(self):
        command = [sys.executable, str(self.tools / "generate_release_notes.py"), "--from", "Z_BASE", "--to", "A_TARGET", "--runtime-root", RUNTIME_ROOT, "--no-jira"]
        stdout = self.runner.run(command, cwd=self.root)[0]
        self.assertEqual(json.loads(stdout)["status"], "draft")
        # 旧转发入口也使用新的默认在线模式，无凭据时须明确失败，不偷偷降级。
        commands = [["node", self.tools / "src/release-tools.js", "generate", "--previous-tag", "Z_BASE", "--target-tag", "A_TARGET",
                     "--work-root", RUNTIME_ROOT, "--snapshot", "auto-changelog-2.6.1-local01"]]
        if os.name == "nt":
            commands.append(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", self.tools / "generate-release-notes.ps1",
                             "-PreviousTag", "Z_BASE", "-TargetTag", "A_TARGET", "-ReleaseWorkRoot", RUNTIME_ROOT])
        with mock.patch.dict(os.environ, {"ECW_JIRA_USER": "", "ECW_JIRA_PASSWORD": ""}):
            for command in commands:
                _, status, stderr = self.runner.run(command, cwd=self.root, allowed=(1,))
                self.assertEqual(status, 1)
                self.assertIn("credentials_missing", stderr)

    def test_sdk_exclusion_is_wired_in_both_paths(self):
        # 静态连线检查, 不代替 Linux 上的实际 SDK 包清单验收。
        text = (TOOLS.parent / "autobuild/6700_build_sdk.sh").read_text(encoding="utf-8")
        common = text.split("SDK_PRUNE_COMMON_PATHS=(", 1)[1].split(")", 1)[0]
        self.assertIn("dbg_tools/Release_Tools/release", common)
        regular = text.split("RM_FILE_FOR_SDK()", 1)[1].split("RM_FILE_FOR_SDK_6700F()", 1)[0]
        self.assertIn('REMOVE_SDK_PATH "dbg_tools/Release_Tools/release"', regular)
        self.assertIn('for rel_path in "${SDK_PRUNE_COMMON_PATHS[@]}"', text.split("RM_FILE_FOR_SDK_6700F()", 1)[1])

    def test_history_filter_engineering_snapshot_status_and_importance(self):
        start = self.runner.git("rev-parse", "HEAD")[0].strip()
        branch = self.runner.git("symbolic-ref", "--short", "HEAD")[0].strip()
        def change(name, message):
            core.write_text(self.repo / name, message)
            self.runner.git("add", name)
            self.runner.git("commit", "-m", message)
            return self.runner.git("rev-parse", "HEAD")[0].strip()
        self.runner.git("checkout", "-b", "history-topic")
        feature = change("history-side.txt", "[ECW6700-1] add history option")
        self.runner.git("checkout", branch)
        fix = change("history-main.txt", "[ECW6630-2] fix history issue")
        self.runner.git("merge", "--no-ff", "history-topic", "-m", "Merge branch 'history-topic'")
        merge = self.runner.git("rev-parse", "HEAD")[0].strip()
        canceled = change("history-canceled.txt", "[ECW6700-3] add canceled option")
        self.runner.git("revert", "--no-edit", canceled)
        target = self.runner.git("rev-parse", "HEAD")[0].strip()
        jira = jira_report({"ECW6700-1": "11101", "ECW6630-2": "11101", "ECW6700-3": "11101"})
        for key, row in jira["results"].items():
            row["fields"]["status"] = {"id": "1", "name": "打开" if key == "ECW6630-2" else "等待确认"}
            if key != "ECW6630-2":
                row["fields"]["summary"] = "【重要】" + key
        opts = {"from_ref": start, "to_ref": target, "version": "history-check"}
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            batch = self.generate(**opts, jira_config=str(self.tools / "examples/jira-config.example.json"), important=["AUTO_" + canceled])
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(len(list(batch.iterdir())), 8)
        self.assertEqual({e["commits"][0] for e in data["customerEntries"]}, {feature, fix})
        self.assertEqual(data["importantIds"], ["AUTO_" + feature])
        self.assertIn("AUTO_" + canceled, data["historyRemovedEntryIds"])
        stats = data["customerStatistics"]
        self.assertEqual((stats["rawCommitCount"], stats["hiddenMergeCommitCount"], stats["revertedCommitCount"], stats["effectiveCommitCount"]), (5, 1, 2, 2))
        self.assertEqual(stats["unaccountedCommitCount"], 0)
        engineering = (batch / "CHANGELOG.generated.md").read_text(encoding="utf-8")
        record = core.read_json(batch / "release-data.json")[0]
        represented, _ = core.coverage_check(record, data["classification"]["rawCommits"], None, data["identity"])
        self.assertEqual({r["hash"] for r in represented}, {feature, fix, merge, canceled, target})
        for sha in (feature, fix, merge, canceled, target):
            self.assertIn(sha, engineering)
        for prefix in ("Release_Notes_", "github-release-body_"):
            text = next(batch.glob(prefix + "*.md")).read_text(encoding="utf-8")
            self.assertNotIn("canceled option", text)
            self.assertNotIn("Merge branch", text)
            self.assertIn("add history option", text)
        self.assertIn("原提交与完整回退抵消", (batch / "coverage-review.md").read_text(encoding="utf-8"))
        with mock.patch.object(core.jira_client, "load_jira_config", side_effect=AssertionError("online")), \
                mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("online")):
            replay = self.generate(**opts, jira_snapshot=str(batch / core.MANIFEST), jira_status_scope="accepted")
        replayed = core.read_json(replay / core.MANIFEST)
        self.assertEqual([e["commits"] for e in replayed["customerEntries"]], [[feature]])
        self.assertEqual(replayed["customerStatistics"]["statusFilteredCommitCount"], 1)
        self.assertEqual(replayed["customerStatistics"]["historyExcludedCommitCount"], 3)
        self.assertTrue(replayed["checks"]["customerCoverageComplete"])
        altered = copy.deepcopy(data)
        altered["classification"]["historyEvidence"]["exclusions"] = []
        altered["jira"]["snapshotDigest"] = core.classification.snapshot_digest(altered["identity"], altered["classification"], altered["jira"])
        invalid = self.root / "history-tampered.json"
        core.write_json(invalid, altered)
        with self.assertRaisesRegex(core.ReleaseError, "历史证据变化"):
            self.generate(**opts, jira_snapshot=str(invalid))
        empty = self.generate(from_ref=merge, to_ref=target, version="history-empty")
        empty_data = core.read_json(empty / core.MANIFEST)
        self.assertFalse(empty_data["customerEntries"])
        self.assertEqual(empty_data["customerStatistics"]["revertedCommitCount"], 2)
        self.assertTrue(empty_data["checks"]["customerCoverageComplete"])
        self.assertEqual(len(list(empty.iterdir())), 8)

    def test_same_file_revert_and_classification_full_pipeline(self):
        # 合成历史验证规则，不代表已读取现场 ECR6600A-3011/2750 提交。
        filename = "history-same-file.txt"
        lines = ["line %02d\n" % i for i in range(30)]
        def save(message):
            core.write_text(self.repo / filename, "".join(lines))
            self.runner.git("add", filename)
            self.runner.git("commit", "-m", message)
            return self.runner.git("rev-parse", "HEAD")[0].strip()
        start = save("same-file baseline")
        lines[1] = "original feature\n"
        canceled = save("[ECW6700-81] add canceled protocol mode")
        lines[25] = "independent fix\n"
        independent = save("[ECW6700-82] fix independent option")
        self.runner.git("revert", "--no-edit", canceled)
        reverted = self.runner.git("rev-parse", "HEAD")[0].strip()
        feature = self.commit("[ECW6700-83] 新增源码实现UART驱动")
        build = self.commit("[ECW6700-83] update build script for new target")
        fallback = self.commit("[ECW6700-83] adapt customer api")
        lines = (self.repo / filename).read_text(encoding="utf-8").splitlines(keepends=True)
        lines[8], lines[15] = "protocol option one\n", "protocol option two\n"
        partial_original = save("[ECW6700-84] add protocol options")
        lines[8] = "line 08\n"
        partial = save('Revert "[ECW6700-84] add protocol options"\n\nThis reverts commit ' + partial_original + ".")
        hashes = {canceled, independent, reverted, feature, build, fallback, partial_original, partial}
        jira = jira_report({key: "10007" for key in ("ECW6700-81", "ECW6700-82", "ECW6700-83", "ECW6700-84")})
        for key, row in jira["results"].items():
            row["fields"]["status"] = {"id": "1", "name": "打开" if key == "ECW6700-82" else "已关闭"}
            if key in {"ECW6700-81", "ECW6700-83"}:
                row["fields"]["summary"] = "【重要】合成测试事项"
        opts = {"from_ref": start, "to_ref": partial, "version": "same-file-revert"}
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            batch = self.generate(**opts, jira_config=str(self.tools / "examples/jira-config.example.json"),
                                  important=["AUTO_" + canceled])
        data = core.read_json(batch / core.MANIFEST)
        expected = hashes - {canceled, reverted}
        self.assertEqual({e["commits"][0] for e in data["customerEntries"]}, expected)
        stats = data["customerStatistics"]
        self.assertEqual((stats["rawCommitCount"], stats["revertedCommitCount"], stats["effectiveCommitCount"],
                          stats["customerEntryCount"], stats["unaccountedCommitCount"]), (8, 2, 6, 6, 0))
        self.assertEqual(set(data["importantIds"]), {"AUTO_" + sha for sha in (feature, build, fallback)})
        records = data["historyAnalysis"]["commits"]
        for sha in (canceled, reverted):
            self.assertEqual(records[sha]["proof"], "inverse-text-three-way")
        self.assertEqual(records[partial]["detailCode"], "text_merge_mismatch")
        decisions = data["classification"]["entries"]
        for sha, category, source in ((feature, "features", "git-rule"), (build, "adaptations", "git-rule"),
                                      (fallback, "features", "jira-type"), (partial, "unclassified", "history-review")):
            self.assertEqual((decisions["AUTO_" + sha]["category"], decisions["AUTO_" + sha]["decisionSource"]), (category, source))
        engineering = (batch / "CHANGELOG.generated.md").read_text(encoding="utf-8")
        for sha in hashes:
            self.assertIn(sha, engineering)
        represented, _ = core.coverage_check(core.read_json(batch / "release-data.json")[0],
                                             data["classification"]["rawCommits"], None, data["identity"])
        self.assertEqual({row["hash"] for row in represented}, hashes)
        review = (batch / "coverage-review.md").read_text(encoding="utf-8")
        for phrase in ("回退及重新应用的三方结果均逐字节一致", "可能是部分回退或附加修改", "JIRA 类型兜底", "构建"):
            self.assertIn(phrase, review)
        for prefix in ("Release_Notes_", "github-release-body_"):
            text = next(batch.glob(prefix + "*.md")).read_text(encoding="utf-8")
            self.assertNotIn("canceled protocol", text)
            self.assertIn("fix independent option", text)
            self.assertIn('Revert "', text)
            self.assertEqual(text.count("新增源码实现UART驱动"), 1)
            self.assertNotIn("inverse-text-three-way", text)
        with mock.patch.object(core.jira_client, "load_jira_config", side_effect=AssertionError("online")), \
                mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("online")):
            replay = self.generate(**opts, jira_snapshot=str(batch / core.MANIFEST), jira_status_scope="accepted")
        replayed = core.read_json(replay / core.MANIFEST)
        self.assertEqual({e["commits"][0] for e in replayed["customerEntries"]}, expected - {independent})
        self.assertEqual(replayed["historyAnalysis"], data["historyAnalysis"])
        self.assertEqual(replayed["customerStatistics"]["statusFilteredCommitCount"], 1)
        self.assertTrue(replayed["checks"]["customerCoverageComplete"])
        for output in (batch, replay):
            self.assertEqual(len(list(output.iterdir())), 8)

    def test_history_unsafe_merge_kept_even_with_jira_type(self):
        start = self.runner.git("rev-parse", "HEAD")[0].strip()
        branch = self.runner.git("symbolic-ref", "--short", "HEAD")[0].strip()
        self.runner.git("checkout", "-b", "history-extra")
        core.write_text(self.repo / "history-extra-side.txt", "side")
        self.runner.git("add", "history-extra-side.txt")
        self.runner.git("commit", "-m", "[ECW6700-1] add side option")
        self.runner.git("checkout", branch)
        self.runner.git("merge", "--no-ff", "--no-commit", "history-extra")
        core.write_text(self.repo / "history-extra-resolution.txt", "additional effect")
        self.runner.git("add", "history-extra-resolution.txt")
        self.runner.git("commit", "-m", "[ECW6700-1] add integration option")
        merge = self.runner.git("rev-parse", "HEAD")[0].strip()
        jira = jira_report({"ECW6700-1": "11101"})
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            batch = self.generate(from_ref=start, to_ref=merge, version="history-extra", jira_config=str(self.tools / "examples/jira-config.example.json"))
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["customerStatistics"]["customerEntryCount"], 2)
        self.assertFalse(data["historyAnalysis"]["exclusions"])
        decision = data["classification"]["entries"]["AUTO_" + merge]
        self.assertEqual(decision["category"], "unclassified")
        self.assertTrue(decision["needsReview"])
        self.assertIn("add integration option", next(batch.glob("Release_Notes_*.md")).read_text(encoding="utf-8"))

    def test_jira_live_snapshot_complete_pipeline(self):
        config = self.tools / "examples/jira-config.example.json"
        live = jira_report({"ECW6700-1": "11101"})
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=live) as fetch:
            first = self.generate(jira_config=str(config), important=["AUTO_" + self.fix])
        self.assertEqual(fetch.call_args.args[0], ["ECW6700-1"])
        original = core.read_json(first / core.MANIFEST)
        self.assertEqual(original["schemaVersion"], 3)
        self.assertEqual(original["customerStatistics"]["rawCommitCount"], 3)
        self.assertEqual(original["classification"]["entries"]["AUTO_" + self.fix]["decisionSource"], "jira-type")
        self.assertTrue(original["checks"]["customerCoverageComplete"])
        with mock.patch.object(core.jira_client, "load_jira_config", side_effect=AssertionError("no credentials")), \
                mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("no network")), \
                mock.patch.object(core, "read_git_evidence", side_effect=AssertionError("reuse saved evidence")):
            second = self.generate(jira_snapshot=str(first / core.MANIFEST), important=["AUTO_" + self.fix])
        replay = core.read_json(second / core.MANIFEST)
        self.assertEqual(replay["jira"]["mode"], "snapshot")
        self.assertEqual(replay["jira"]["sourceManifestSha256"], core.file_hash(first / core.MANIFEST))
        self.assertEqual(original["classification"], replay["classification"])
        for filename in ("Release_Notes_A_TARGET.md", "github-release-body_A_TARGET.md"):
            self.assertEqual((first / filename).read_bytes(), (second / filename).read_bytes())
        self.assertEqual(len(list(second.iterdir())), 8)

    def test_three_projects_engineering_classification_and_status(self):
        start = self.commit("multi-project baseline")
        types = {f"{project}-{i}": type_id for project in ("ECW6700", "ECW6630", "ECW6280")
                 for i, type_id in enumerate(("11100", "11101", "10604"), 1)}
        types.update({"ECW6630-4": "10512", "ECW6630-5": "10509"})
        hashes = {key: self.commit(f"[{key}] " + ("sync code" if type_id in ("10604", "10512", "10509") else "adjust lifecycle"))
                  for key, type_id in types.items()}
        self.runner.git("tag", "ECW6700SDK_TEST_MULTIPROJECT")
        jira = jira_report(types)
        for key, row in jira["results"].items():
            row["fields"].update(status={"id": "1", "name": "处理中" if key == "ECW6630-2" else "已关闭"},
                                 assignee={"name": "dev", "displayName": "测试处理人"})
            if key.endswith("-1"):
                row["fields"]["summary"] = "【重要】本项目变化"
        opts = {"from_ref": start, "to_ref": "ECW6700SDK_TEST_MULTIPROJECT", "no_jira": False}
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira) as fetch:
            batch = self.generate(**opts)
            selected = self.generate(**opts, jira_status_scope="accepted")
        for call in fetch.call_args_list:
            self.assertEqual(call.args[0], sorted(types))
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["rawCount"], 11)
        self.assertEqual(data["customerStatistics"]["categoryCounts"],
                         {"features": 5, "improvements": 0, "fixes": 6, "adaptations": 0, "unclassified": 0})
        self.assertEqual(data["customerStatistics"]["issuePrefixEntryCounts"], {"ECW6700": 3, "ECW6630": 5, "ECW6280": 3})
        self.assertEqual(len(data["importantIds"]), 3)
        self.assertEqual(data["jiraStatusReview"]["counts"]["associatedIssueCount"], 11)
        self.assertEqual(data["customerCoverage"]["unaccountedCommits"], [])
        engineering = core.read_json(batch / "release-data.json")[0]
        self.assertEqual({issue["id"] for item in engineering["fixes"] for issue in item["fixes"]}, set(types))
        self.assertEqual({item["commit"]["hash"] for item in engineering["fixes"]}, set(hashes.values()))
        text = (batch / "Release_Notes_ECW6700SDK_TEST_MULTIPROJECT.md").read_text(encoding="utf-8")
        for key in types:
            self.assertEqual(text.count("`" + key + "`"), 1)
        filtered = core.read_json(selected / core.MANIFEST)
        self.assertEqual(filtered["statusSelection"]["includedEntryCount"], 10)
        self.assertEqual(filtered["statusSelection"]["filteredCommits"], [hashes["ECW6630-2"]])
        self.assertEqual(len(filtered["importantIds"]), 3)
        self.assertEqual(filtered["jiraStatusReview"], data["jiraStatusReview"])
        self.assertEqual((batch / "CHANGELOG.generated.md").read_bytes(), (selected / "CHANGELOG.generated.md").read_bytes())
        review = (selected / "JIRA_Status_Review_ECW6700SDK_TEST_MULTIPROJECT.md").read_text(encoding="utf-8")
        self.assertIn("ECW6630-2", review)
        self.assertIn("测试处理人 (dev)", review)
        self.assertEqual(len(list(batch.iterdir())), 8)
        self.assertEqual(len(list(selected.iterdir())), 8)

    def test_project_task_fallback_generation_and_status_filter(self):
        start = self.commit("task fallback baseline")
        projects = ("ECW6700", "ECW6630", "ECW6280")
        types, hashes, filtered_hashes = {}, [], []
        for project in projects:
            task, subtask = project + "-36", project + "-37"
            subjects = ["[" + task + "] add ad9613 PWDN always ok", "[" + task + "] add MCP4811 for CFO",
                        "[" + subtask + "] sync code"]
            commits = [self.commit(subject) for subject in subjects]
            hashes.extend(commits)
            filtered_hashes.extend(commits[:2])
            types.update({task: "10007", subtask: "10304"})
        jira = jira_report(types)
        for key, row in jira["results"].items():
            row["fields"]["status"] = {"id": "3", "name": "处理中"} if key.endswith("-36") else {"id": "6", "name": "已关闭"}
        opts = {"from_ref": start, "to_ref": hashes[-1], "version": "TASK_FALLBACK", "no_jira": False}
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            batch = self.generate(**opts)
            selected = self.generate(**opts, jira_status_scope="accepted")
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["rawCount"], 9)
        self.assertEqual(data["jiraStatusReview"]["counts"]["associatedIssueCount"], 6)
        self.assertEqual([e["category"] for e in data["customerEntries"]], ["features"] * 9)
        self.assertEqual(data["customerStatistics"]["jiraFallbackEntryCount"], 9)
        self.assertEqual(data["customerStatistics"]["issuePrefixEntryCounts"], {project: 3 for project in projects})
        self.assertEqual(data["customerCoverage"]["unaccountedCommits"], [])
        for decision in data["classification"]["entries"].values():
            self.assertTrue(decision["needsReview"])
            self.assertIn("jira_category_fallback", decision["reasonCodes"])
        text = (batch / "Release_Notes_TASK_FALLBACK.md").read_text(encoding="utf-8")
        features = text.split("## 2. 新增功能", 1)[1].split("## 3.", 1)[0]
        self.assertIn("add ad9613 PWDN always ok", features)
        self.assertIn("add MCP4811 for CFO", features)
        for project in projects:
            self.assertEqual(features.count("`" + project + "-36`"), 2)
            self.assertEqual(features.count("`" + project + "-37`"), 1)
        filtered = core.read_json(selected / core.MANIFEST)
        self.assertEqual(filtered["statusSelection"]["includedEntryCount"], 3)
        self.assertEqual(set(filtered["statusSelection"]["filteredCommits"]), set(filtered_hashes))
        self.assertEqual(filtered["customerCoverage"]["unaccountedCommits"], [])
        selected_text = (selected / "Release_Notes_TASK_FALLBACK_accepted.md").read_text(encoding="utf-8")
        for project in projects:
            self.assertNotIn("`" + project + "-36`", selected_text)
            self.assertIn("`" + project + "-37`", selected_text)
        self.assertEqual((batch / "CHANGELOG.generated.md").read_bytes(), (selected / "CHANGELOG.generated.md").read_bytes())
        for output in (batch, selected):
            self.assertEqual(len(list(output.iterdir())), 8)

    def test_ecr_project_configuration_generation_and_replay(self):
        start = self.commit("ECR configuration baseline")
        task = self.commit("[ECR6600-1] sync platform code")
        fix = self.commit("[ECR6600A-2] fix radio crash")
        project_path = self.root / "ecr-project.json"
        project = core.release_project.load(self.tools / "examples/release-project.ecr.json")
        project["acceptedJiraStatuses"] = ["已完成"]
        core.write_json(project_path, project)
        jira = jira_report({"ECR6600-1": "10007", "ECR6600A-2": "11101"})
        # 类型映射及状态是隔离测试数据，不代表已核实企业 ECR 项目配置。
        for key, row in jira["results"].items():
            jira["configSnapshot"]["projects"][key.rsplit("-", 1)[0]] = copy.deepcopy(jira["configSnapshot"]["projects"]["ECW6700"])
            row["warnings"] = []
            row["fields"]["status"] = {"id": "1", "name": "已完成" if key == "ECR6600-1" else "已关闭"}
        jira["results"]["ECR6600-1"]["fields"]["summary"] = "【重要】平台调整"
        config_path = self.tools / ".auto-changelog"
        before = config_path.read_bytes()
        config = core.read_json(config_path)
        config["issuePattern"] = r"(?:ECR6600|ECR6600A)-\d+"
        core.write_json(config_path, config)
        opts = {"from_ref": start, "to_ref": fix, "version": "ECR_CONFIG_TEST", "no_jira": False,
                "project_config": str(project_path)}
        try:
            with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                    mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
                all_batch = self.generate(**opts)
                accepted = self.generate(**opts, jira_status_scope="accepted")
            data = core.read_json(accepted / core.MANIFEST)
            self.assertEqual(data["projectConfig"], project)
            self.assertEqual(data["customerMetadata"]["scope"], "ECR6600")
            self.assertEqual(data["jiraStatusReview"]["allowedStatusNames"], ["已完成"])
            self.assertEqual(data["statusSelection"]["filteredCommits"], [fix])
            self.assertEqual(data["importantIds"], ["AUTO_" + task])
            self.assertEqual(data["customerStatistics"]["customerEntryCount"], 1)
            engineering = core.read_json(accepted / "release-data.json")[0]
            self.assertEqual({i["id"] for item in engineering["fixes"] for i in item["fixes"]}, {"ECR6600-1", "ECR6600A-2"})
            self.assertEqual((all_batch / "CHANGELOG.generated.md").read_bytes(), (accepted / "CHANGELOG.generated.md").read_bytes())
            with mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("must stay offline")):
                replay = self.generate(**opts, jira_snapshot=str(all_batch / core.MANIFEST), jira_status_scope="accepted")
            for name in ("Release_Notes_ECR_CONFIG_TEST_accepted.md", "github-release-body_ECR_CONFIG_TEST.md", "JIRA_Status_Review_ECR_CONFIG_TEST.md"):
                text = (accepted / name).read_text(encoding="utf-8")
                self.assertIn("指定状态：已完成", text)
                self.assertEqual((accepted / name).read_bytes(), (replay / name).read_bytes())
            for output in (all_batch, accepted, replay):
                self.assertEqual(len(list(output.iterdir())), 8)
            project["acceptedJiraStatuses"] = ["已关闭"]
            core.write_json(project_path, project)
            with self.assertRaisesRegex(core.ReleaseError, "snapshot_invalid"):
                self.generate(**opts, jira_snapshot=str(all_batch / core.MANIFEST))
            with self.assertRaisesRegex(core.ReleaseError, "项目配置已变更"):
                self.generate(**opts, project_config_sha256="outdated-web-config")
        finally:
            config_path.write_bytes(before)

    def test_default_jira_auto_important_generation_and_replay(self):
        jira = jira_report({"ECW6700-1": "11101"})
        jira["results"]["ECW6700-1"]["fields"]["summary"] = "【重要】关闭连接修复"
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())) as load, \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            result = core.generate(self.options(no_jira=False, important=["AUTO_" + self.feature]), self.tools)
            manual = self.generate(no_jira=False, content=str(self.content_path), important=["N"])
        self.assertEqual(Path(load.call_args.args[0]), self.tools / "examples/jira-config.example.json")
        self.assertEqual(result["importantEntryCount"], 2)
        self.assertEqual(result["autoImportantEntryCount"], 1)
        self.assertEqual(result["noIssueCommitCount"], 2)
        first = Path(result["batch"])
        data = core.read_json(first / core.MANIFEST)
        self.assertEqual(data["importantIds"], ["AUTO_" + self.feature, "AUTO_" + self.fix])
        self.assertEqual(data["importance"]["entries"]["AUTO_" + self.fix]["jiraKeys"], ["ECW6700-1"])
        self.assertTrue(data["checks"]["customerCoverageComplete"])
        self.assertEqual(core.read_json(manual / core.MANIFEST)["importantIds"], ["N", "F"])
        with mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("must stay offline")):
            replay = self.generate(jira_snapshot=str(first / core.MANIFEST), important=["AUTO_" + self.feature])
        for name in ("Release_Notes_A_TARGET.md", "github-release-body_A_TARGET.md"):
            text = (first / name).read_text(encoding="utf-8")
            self.assertEqual(text.count("Fix connection close"), 1)
            self.assertEqual(text.count("Add a configuration target"), 1)
            self.assertEqual((first / name).read_bytes(), (replay / name).read_bytes())
        self.assertEqual(len(list(first.iterdir())), 8)

    def test_web_real_generation_download_and_refresh(self):
        import http.client
        import threading
        import time
        import release_notes_web as web
        jira = jira_report({"ECW6700-1": "11101"})
        jira["results"]["ECW6700-1"]["fields"].update(summary="【重要】关闭连接修复", status={"id": "1", "name": "已关闭"})
        service = web.ReleaseService(self.tools, RUNTIME_ROOT, self.tools / "examples/jira-config.example.json",
                                     lambda *a, **k: ({"caFile": None}, object()), generator=core.generate)
        server = web.create_server(service, 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def request(method, path, body=None):
            conn = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=15)
            try:
                conn.request(method, path, json.dumps(body) if body else None,
                             {"X-Release-Token": service.token, "Content-Type": "application/json"})
                response = conn.getresponse()
                return response.status, response.read()
            finally:
                conn.close()
        try:
            with mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
                status, body = request("POST", "/api/jobs", {"from": "Z_BASE", "to": "A_TARGET", "statusScope": "accepted"})
                self.assertEqual(status, 202)
                job_id = json.loads(body)["id"]
                deadline = time.monotonic() + 100
                while time.monotonic() < deadline:
                    status, body = request("GET", "/api/jobs/" + job_id)
                    job = json.loads(body)
                    if job["status"] != "running":
                        break
                    time.sleep(.1)
                self.assertEqual(job["status"], "done", job["error"])
            self.assertEqual(job["result"]["autoImportantEntryCount"], 1)
            self.assertEqual(len(job["files"]), 8)
            self.assertEqual(json.loads(request("GET", "/api/state")[1])["latest"]["id"], job_id)
            self.assertIn("Release_Notes_A_TARGET_accepted.md", job["files"])
            status, body = request("GET", "/api/jobs/" + job_id + "/files/Release_Notes_A_TARGET_accepted.md")
            self.assertEqual(status, 200)
            self.assertIn("## 2. 重要变更", body.decode())
            self.assertEqual(body.decode().count("Fix connection close"), 1)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
            service.close()

    def test_jira_failures_preserve_commits_and_safe_errors(self):
        config = self.tools / "examples/jira-config.example.json"
        live = jira_report({"ECW6700-1": "11101"}, failed=("ECW6700-1",))
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=live):
            with self.assertRaises(core.ReleaseError) as failure:
                self.generate(jira_config=str(config))
        batch = failure.exception.batch
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["jira"]["status"], "unavailable")
        self.assertEqual(data["status"], "failed")
        self.assertEqual(data["rawCount"], 3)
        self.assertIn(self.fix, (batch / "raw-commits.txt").read_text(encoding="utf-8"))
        self.assertIn("forbidden", (batch / "coverage-review.md").read_text(encoding="utf-8"))
        self.assertIn("forbidden: 1", str(failure.exception))
        self.assertIn(str(config.resolve()), str(failure.exception))
        self.assertFalse(list(batch.glob("Release_Notes_*.md")))
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", side_effect=RuntimeError("secret-must-not-escape")):
            with self.assertRaises(core.ReleaseError) as context:
                self.generate(jira_config=str(config))
        self.assertNotIn("secret-must-not-escape", str(context.exception))

    def test_ecr_jira_configuration_missing_and_recovery(self):
        from test_jira_client import Reply, Transport, issue
        start = self.commit("ECR Jira config baseline")
        self.commit("[ECR6600-1] fix disconnect")
        end = self.commit("[ECR6600A-2] add wakeup command")
        options = {"from_ref": start, "to_ref": end, "no_jira": False,
                   "project_config": str(self.tools / "examples/release-project.ecr.json")}
        transport = Transport([Reply(issue("ECR6600-1")), Reply(issue("ECR6600A-2", summary="【重要】唤醒命令"))])
        with mock.patch.dict(os.environ, {"ECW_JIRA_USER": "test-user", "ECW_JIRA_PASSWORD": "test-secret"}), \
                mock.patch.object(core.jira_client.request, "build_opener", return_value=transport):
            with self.assertRaisesRegex(core.ReleaseError, "project_not_configured") as failure:
                self.generate(**options)
            self.assertEqual(transport.calls, [])
            failed = core.read_json(failure.exception.batch / core.MANIFEST)
            self.assertEqual(failed["jira"]["counts"]["skipped"], 2)
            self.assertFalse(list(failure.exception.batch.glob("Release_Notes_*.md")))
            config = self.tools / "examples/jira-config.ecr.example.json"
            batch = self.generate(**options, jira_config=str(config))
        self.assertEqual(len(transport.calls), 2)
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["jira"]["counts"]["fetched"], 2)
        self.assertEqual(data["customerStatistics"]["customerEntryCount"], 2)
        self.assertEqual(data["importantIds"], ["AUTO_" + end])
        self.assertEqual(data["rawCount"], 2)
        self.assertEqual(len(list(batch.iterdir())), 8)

    def test_ecr_type_mapping_full_generation_and_replay(self):
        from test_jira_client import Reply, Transport, issue
        common = (("10604", "features"), ("10100", "improvements"), ("10102", "fixes"),
                  ("10603", "fixes"), ("10007", "features"), ("10304", "features"))
        types = {f"{project}-{i}": pair for project in ("ECR6600", "ECR6600A") for i, pair in enumerate(common, 1)}
        types["ECR6600A-7"] = ("11100", "fixes")
        start = self.commit("ECR Jira type mapping baseline")
        expected = {self.commit(f"[{key}] sync platform code"): category for key, (_, category) in types.items()}
        explicit_fix = self.commit("[ECR6600-1] fix radio crash")
        expected[explicit_fix] = "fixes"
        end = self.commit("[ECR6600A-1] optimize throughput")
        expected[end] = "improvements"
        replies = []
        for i, (key, (kind, _)) in enumerate(sorted(types.items())):
            value = issue(key, summary="【重要】协议能力" if key == "ECR6600-1" else "事项",
                          issuetype={"id": kind, "name": "Test type"},
                          status={"id": "1", "name": "处理中" if key == "ECR6600A-7" else "已关闭"})
            # 每个样本使用独立事项标识；重复提交仍关联同一事项。
            value["id"] = str(1000 + i)
            replies.append(Reply(value))
        transport = Transport(replies)
        config = self.tools / "examples/jira-config.ecr.example.json"
        options = {"from_ref": start, "to_ref": end, "version": "ECR_TYPE_TEST", "no_jira": False,
                   "project_config": str(self.tools / "examples/release-project.ecr.json")}
        engine_path = self.tools / ".auto-changelog"
        before = engine_path.read_bytes()
        engine = core.read_json(engine_path)
        engine["issuePattern"] = r"(?:ECR6600|ECR6600A)-\d+"
        core.write_json(engine_path, engine)
        try:
            with mock.patch.dict(os.environ, {"ECW_JIRA_USER": "test-user", "ECW_JIRA_PASSWORD": "test-secret"}), \
                    mock.patch.object(core.jira_client.request, "build_opener", return_value=transport):
                batch = self.generate(**options, jira_config=str(config))
            data = core.read_json(batch / core.MANIFEST)
            self.assertEqual(data["customerStatistics"]["categoryCounts"],
                             {"features": 6, "improvements": 3, "fixes": 6, "adaptations": 0, "unclassified": 0})
            self.assertEqual(data["rawCount"], 15)
            self.assertEqual(data["jira"]["counts"]["fetched"], 13)
            self.assertEqual(data["jiraStatusReview"]["counts"]["associatedIssueCount"], 13)
            self.assertEqual(len(transport.calls), 13)
            self.assertEqual({entry["commits"][0]: entry["category"] for entry in data["customerEntries"]}, expected)
            self.assertTrue(data["checks"]["customerCoverageComplete"])
            self.assertEqual(len(data["importantIds"]), 2)
            self.assertTrue(all(not result["warnings"] for result in data["jira"]["results"].values()))
            with mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("must stay offline")):
                replay = self.generate(**options, jira_snapshot=str(batch / core.MANIFEST))
                accepted = self.generate(**options, jira_snapshot=str(batch / core.MANIFEST), jira_status_scope="accepted")
            for name in ("Release_Notes_ECR_TYPE_TEST.md", "github-release-body_ECR_TYPE_TEST.md"):
                self.assertEqual((batch / name).read_bytes(), (replay / name).read_bytes())
                self.assertNotIn("待分类变更\n", (batch / name).read_text(encoding="utf-8"))
            filtered = core.read_json(accepted / core.MANIFEST)
            self.assertEqual(filtered["customerStatistics"]["categoryCounts"],
                             {"features": 6, "improvements": 3, "fixes": 5, "adaptations": 0, "unclassified": 0})
            self.assertEqual(filtered["statusSelection"]["filteredEntryCount"], 1)
            for output in (batch, replay, accepted):
                self.assertEqual(len(list(output.iterdir())), 8)
                self.assertEqual((batch / "CHANGELOG.generated.md").read_bytes(), (output / "CHANGELOG.generated.md").read_bytes())
        finally:
            engine_path.write_bytes(before)

    def test_status_filter_full_pipeline_snapshot_counts_and_artifacts(self):
        start = self.commit("status filter baseline")
        subjects = ["[ECW6700-1] fix confirmed first", "[ECW6700-1] fix confirmed second",
                    "[ECW6700-2] fix pending", "[ECW6700-1][ECW6700-2] fix mixed",
                    "[ECW6700-3] fix unknown", "build: package adjustment",
                    "[ECW6700-4] fix resolved", "[ECW6700-5] fix awaiting", "[ECW6700-6] fix alias"]
        hashes = [self.commit(subject) for subject in subjects]
        jira = jira_report({f"ECW6700-{i}": "11101" for i in range(1, 7)})
        for i, name in enumerate(("已关闭", "处理中", None, "已解决", "等待确认", "已关闭"), 1):
            jira["results"][f"ECW6700-{i}"]["fields"]["status"] = {"id": str(i), "name": name} if name else None
            jira["results"][f"ECW6700-{i}"]["fields"]["assignee"] = {"name": "dev" + str(i), "displayName": "研发" + str(i)}
        for key in ("ECW6700-1", "ECW6700-2"):
            jira["results"][key]["fields"]["summary"] = "【重要】事项"
        jira["results"]["ECW6700-6"].update(issueId="123", canonicalKey="ECW6700-1")
        jira["results"]["ECW6700-6"]["fields"] = copy.deepcopy(jira["results"]["ECW6700-1"]["fields"])
        opts = {"from_ref": start, "to_ref": hashes[-1], "version": "SAME_TAG", "no_jira": False, "important": ["AUTO_" + hashes[5]]}
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            all_batch = self.generate(**opts)
            selected = self.generate(**opts, jira_status_scope="accepted")
        data = core.read_json(selected / core.MANIFEST)
        self.assertEqual(data["rawCount"], 9)
        self.assertEqual(data["jiraStatusReview"]["counts"]["associatedIssueCount"], 5)
        self.assertEqual(data["jiraStatusReview"]["counts"]["otherStatusIssueCount"], 1)
        self.assertEqual(data["jiraStatusReview"]["counts"]["unknownStatusIssueCount"], 1)
        self.assertEqual(data["statusSelection"]["includedEntryCount"], 5)
        self.assertEqual(data["customerStatistics"]["statusFilteredCommitCount"], 4)
        self.assertEqual(len(data["importantIds"]), 3)
        self.assertTrue(data["checks"]["customerCoverageComplete"])
        self.assertEqual(data["customerCoverage"]["unaccountedCommits"], [])
        self.assertEqual(len(data["artifacts"]), 7)
        self.assertEqual(len(list(selected.iterdir())), 8)
        self.assertEqual([p.name for p in all_batch.glob("Release_Notes_*.md")], ["Release_Notes_SAME_TAG.md"])
        self.assertEqual([p.name for p in selected.glob("Release_Notes_*.md")], ["Release_Notes_SAME_TAG_accepted.md"])
        self.assertIn("Release_Notes_SAME_TAG_accepted.md", data["artifacts"])
        self.assertEqual(core.file_hash(selected / "Release_Notes_SAME_TAG_accepted.md"), data["artifacts"]["Release_Notes_SAME_TAG_accepted.md"])
        self.assertEqual((all_batch / "CHANGELOG.generated.md").read_bytes(), (selected / "CHANGELOG.generated.md").read_bytes())
        self.assertEqual((all_batch / "raw-commits.txt").read_bytes(), (selected / "raw-commits.txt").read_bytes())
        for pattern in ("Release_Notes_*.md", "github-release-body_*.md"):
            text = next(selected.glob(pattern)).read_text(encoding="utf-8")
            self.assertNotIn("待填写", text)
            for subject in ("fix pending", "fix mixed", "fix unknown", "package adjustment"):
                self.assertNotIn(subject, text)
            self.assertEqual(text.count("fix confirmed first"), 1)
            self.assertIn("目标代码和完整工程记录仍包含全部合入", text)
        report = next(selected.glob("JIRA_Status_Review_*.md")).read_text(encoding="utf-8")
        self.assertIn("处理中", report)
        self.assertIn("fix mixed", report)
        self.assertIn("状态未知 JIRA", report)
        self.assertIn("当前处理人", report)
        self.assertIn("研发2 (dev2)", report)
        self.assertNotIn("/browse/", report)
        self.assertIn("## 未提交状态 JIRA", report)
        self.assertNotIn("非指定状态", report)
        with mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("no network")):
            replay = self.generate(from_ref=start, to_ref=hashes[-1], version=opts["version"], jira_snapshot=str(all_batch / core.MANIFEST), jira_status_scope="accepted", important=opts["important"])
        for pattern in ("Release_Notes_*.md", "github-release-body_*.md", "JIRA_Status_Review_*.md"):
            path = next(selected.glob(pattern))
            self.assertEqual(path.read_bytes(), (replay / path.name).read_bytes())

    def test_status_filter_empty_and_manual_conflicts_do_not_disappear(self):
        jira = jira_report({"ECW6700-1": "11101"})
        jira["results"]["ECW6700-1"]["fields"].update(summary="【重要】still pending", status={"id": "1", "name": "处理中"})
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=jira):
            batch = self.generate(no_jira=False, jira_status_scope="accepted", important=["AUTO_" + self.fix])
            bad = {**self.content, "customerExclusions": [{"hash": self.fix, "reason": "duplicate", "reviewer": "test"}]}
            path = self.root / "status-conflict.json"
            core.write_json(path, bad)
            with self.assertRaisesRegex(core.ReleaseError, "排除记录冲突"):
                self.generate(no_jira=False, jira_status_scope="accepted", content=str(path))
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["customerEntries"], [])
        self.assertEqual(data["importantIds"], [])
        self.assertTrue(data["checks"]["customerCoverageComplete"])
        self.assertEqual(data["customerStatistics"]["statusFilteredCommitCount"], 3)
        self.assertEqual(len(list(batch.iterdir())), 8)
        text = (batch / "Release_Notes_A_TARGET_accepted.md").read_text(encoding="utf-8")
        self.assertIn("没有符合指定 JIRA 状态", text)
        self.assertNotIn("无符合指定 JIRA 状态的记录。", text)
        self.assertIn("## 2. 新增功能\n\n\n## 3.", text)

    def test_jira_fallback_seven_entries_generation_and_replay(self):
        fixture = core.read_json(TOOLS / "test/fixtures/classification-sample.json")["records"]
        expected = {"ECW6700-1460": "fixes", "ECW6700-342": "fixes", "ECW6700-868": "fixes",
                    "ECW6700-1407": "improvements", "ECW6700-1470": "fixes", "ECW6700-1432": "fixes",
                    "ECW6700-1475": "features"}
        rows = [r for r in fixture if set(r["jiraTypes"]) & set(expected) and
                ("ECW6700-1475" not in r["jiraTypes"] or "RAM" in r["subject"])]
        start = self.commit("fallback baseline")
        hashes = [self.commit(r["subject"]) for r in rows]
        live = jira_report({k: v for row in rows for k, v in row["jiraTypes"].items()})
        options = {"from_ref": start, "to_ref": hashes[-1], "important": ["AUTO_" + hashes[-1]]}
        with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                mock.patch.object(core.jira_client, "fetch_issues", return_value=live):
            first = self.generate(**options, jira_config=str(self.tools / "examples/jira-config.example.json"))
        data = core.read_json(first / core.MANIFEST)
        self.assertEqual(data["customerStatistics"]["categoryCounts"],
                         {"features": 1, "improvements": 1, "fixes": 5, "adaptations": 0, "unclassified": 0})
        self.assertEqual(data["rawCount"], 7)
        self.assertTrue(data["checks"]["customerCoverageComplete"])
        self.assertTrue(data["checks"]["customerRenderIdsValid"])
        self.assertEqual(len(list(first.iterdir())), 8)
        self.assertIn("jira_category_fallback", (first / "coverage-review.md").read_text(encoding="utf-8"))
        for entry in data["customerEntries"]:
            self.assertEqual(entry["category"], expected[entry["issues"][0]])
            self.assertTrue(data["classification"]["entries"][entry["id"]]["needsReview"])
        with mock.patch.object(core.jira_client, "load_jira_config", side_effect=AssertionError("no credentials")), \
                mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("no network")):
            replay = self.generate(**options, jira_snapshot=str(first / core.MANIFEST))
        for pattern in ("Release_Notes_*.md", "github-release-body_*.md"):
            original = next(first.glob(pattern))
            self.assertEqual(original.read_bytes(), (replay / original.name).read_bytes())
            self.assertNotIn("待分类变更\n", original.read_text(encoding="utf-8"))

    def test_disabled_jira_and_manual_exclusions(self):
        content = copy.deepcopy(self.content)
        content["customerExclusions"] = [{"hash": self.target, "reason": "Merge metadata only", "reviewer": "fixture"}]
        path = self.root / "excluded.json"
        core.write_json(path, content)
        with mock.patch.object(core.jira_client, "load_jira_config", side_effect=AssertionError("disabled")), \
                mock.patch.object(core.jira_client, "fetch_issues", side_effect=AssertionError("disabled")):
            batch = self.generate(content=str(path))
        data = core.read_json(batch / core.MANIFEST)
        self.assertEqual(data["jira"]["mode"], "disabled")
        self.assertEqual(data["customerCoverage"]["status"], "complete")
        self.assertEqual(data["customerStatistics"]["excludedCommitCount"], 1)
        self.assertEqual(data["customerStatistics"]["customerEntryCount"], 3)
        self.assertTrue(all(e["decisionSource"] == "manual" for e in data["classification"]["entries"].values()))
        partial = self.generate(content=str(self.content_path))
        data = core.read_json(partial / core.MANIFEST)
        self.assertFalse(data["checks"]["customerCoverageComplete"])
        self.assertEqual(data["customerCoverage"]["unaccountedCommits"], [self.target])
        self.assertIn("尚有 1 条提交待整理", (partial / "Release_Notes_A_TARGET.md").read_text(encoding="utf-8"))
        with self.assertRaisesRegex(core.ReleaseError, "重要文件不接受"):
            core.validate_entries(content, self.raw(), self.identity())
        core.write_json(path, {**content, "customerExclusions": [{"hash": self.fix, "reason": "x", "reviewer": "x"}]})
        with self.assertRaisesRegex(core.ReleaseError, "排除记录冲突"):
            self.generate(content=str(path))

    def test_render_missing_duplicate_ids_fail_generation(self):
        render = core.render
        for duplicate in (False, True):
            def broken(*args, **kwargs):
                text, ids = render(*args, **kwargs)
                return text, ids + ids[:1] if duplicate else ids[1:]
            with mock.patch.object(core, "render", side_effect=broken), self.assertRaisesRegex(core.ReleaseError, "实际渲染 ID"):
                self.generate()

    def test_jira_input_and_snapshot_drift(self):
        config = self.tools / "examples/jira-config.example.json"
        before = config.read_bytes()
        write = core.write_text
        def mutate(destination, text):
            write(destination, text)
            if Path(destination).name == "coverage-review.md":
                config.write_bytes(before + b"\n")
        try:
            with mock.patch.object(core.jira_client, "load_jira_config", return_value=({"caFile": None}, object())), \
                    mock.patch.object(core.jira_client, "fetch_issues", return_value=jira_report({"ECW6700-1": "11101"})), \
                    mock.patch.object(core, "write_text", side_effect=mutate), self.assertRaisesRegex(core.ReleaseError, "输入发生变化"):
                self.generate(jira_config=str(config))
        finally:
            config.write_bytes(before)

    def test_git_evidence_limits_and_failure(self):
        runner = mock.Mock()
        runner.git.side_effect = [("中" * 6000, 0, ""), ("\0".join(str(i) for i in range(201)), 0, "")]
        evidence = core.read_git_evidence(runner, self.fix)
        self.assertLessEqual(len(evidence["body"].encode()), 16384)
        self.assertEqual(len(evidence["paths"]), 200)
        self.assertEqual(evidence["truncatedFields"], ["body", "paths"])
        runner.git.side_effect = core.ReleaseError("git failure")
        self.assertEqual(core.read_git_evidence(runner, self.fix)["reasonCode"], "git_evidence_unavailable")

    def test_jira_cli_preflight_validation(self):
        for args in (["--prepare-runtime", "--jira-config", "missing.json"],
                     ["--from", "Z_BASE", "--to", "A_TARGET", "--jira-prompt", "--no-jira"],
                     ["--from", "Z_BASE", "--to", "A_TARGET", "--jira-config", "a", "--jira-snapshot", "b"]):
            _, status, _ = self.runner.run([sys.executable, self.tools / "generate_release_notes.py", *args], allowed=(2,))
            self.assertEqual(status, 2)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime-root", default=RUNTIME_ROOT)
    ap.add_argument("--test", action="append", help="只运行指定方法名, 可重复")
    args = ap.parse_args()
    RUNTIME_ROOT = str(Path(args.runtime_root).absolute())
    suite = unittest.TestSuite(ReleaseTests(name) for name in args.test) if args.test else unittest.defaultTestLoader.loadTestsFromTestCase(ReleaseTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {"testsRun": result.testsRun, "failures": len(result.failures), "errors": len(result.errors),
              "skipped": result.skipped, "platform": sys.platform, "python": sys.version, "runtimeRoot": RUNTIME_ROOT,
              "details": [(str(test), text) for test, text in result.failures + result.errors]}
    if hasattr(ReleaseTests, "root"):
        core.write_json(ReleaseTests.root / "test-results.json", report)
        print("Report: " + str(ReleaseTests.root / "test-results.json"))
    sys.exit(not result.wasSuccessful())
