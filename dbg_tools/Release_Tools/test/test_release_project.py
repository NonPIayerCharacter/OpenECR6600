"""项目配置校验、默认值与状态口径；不访问网络。"""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import generate_release_notes as core
import release_project
import release_status
from test_release_classification import report


class ProjectTests(unittest.TestCase):
    def test_default_and_explicit_configuration(self):
        default = release_project.load()
        self.assertEqual(default["defaultScope"], "ECW6630、ECW6700")
        self.assertEqual(default["acceptedJiraStatuses"], ["等待确认", "已解决", "已关闭"])
        ecr = release_project.load(TOOLS / "examples/release-project.ecr.json")
        self.assertEqual(ecr["defaultScope"], "ECR6600")
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "读取失败"):
                release_project.load(Path(directory) / "missing.json")

    def test_invalid_configuration_is_rejected(self):
        original = release_project.load()
        changes = [("schemaVersion", True), ("schemaVersion", 2), ("defaultScope", " "),
                   ("webTitle", "bad\nname"), ("fromPlaceholder", 123), ("toPlaceholder", "x" * 257),
                   ("acceptedJiraStatuses", []), ("acceptedJiraStatuses", "已完成"),
                   ("acceptedJiraStatuses", ["完成", " 完成 "]), ("acceptedJiraStatuses", [None]),
                   ("acceptedJiraStatuses", ["完成\x00"]), ("unknown", "value")]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "project.json"
            for name, value in changes:
                with self.subTest(name=name, value=value):
                    path.write_text(json.dumps({**original, name: value}), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        release_project.load(path)
            for value in ([], {}, "not an object"):
                path.write_text(json.dumps(value), encoding="utf-8")
                with self.assertRaises(ValueError):
                    release_project.load(path)
            path.write_text('{"schemaVersion":', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "读取失败"):
                release_project.load(path)

    def test_project_defaults_preserve_manual_metadata(self):
        project = release_project.load(TOOLS / "examples/release-project.ecr.json")
        for scope in (None, "", " "):
            self.assertEqual(core.apply_metadata_defaults({"scope": scope}, project)["scope"], "ECR6600")
        content = {"scope": "ECR6600F test board", "releaseDate": "2030-01-02"}
        self.assertEqual(core.apply_metadata_defaults(content, project), content)

    def test_custom_status_drives_filter_and_both_markdown_outputs(self):
        raw = [{"hash": "a" * 40, "subject": "[ECW6700-1] fix one"},
               {"hash": "b" * 40, "subject": "[ECW6700-2] fix two"}]
        jira = report({"ECW6700-1": "11101", "ECW6700-2": "11101"})
        jira["results"]["ECW6700-1"]["fields"]["status"] = {"id": "1", "name": " 完成|待归档 "}
        jira["results"]["ECW6700-2"]["fields"]["status"] = {"id": "2", "name": "已关闭"}
        identity = {"previous": {"commit": "0" * 40, "tag": "BASE", "type": "tag"},
                    "target": {"commit": "b" * 40, "tag": "TARGET", "type": "tag"}}
        review = release_status.build_review(raw, jira, ["完成|待归档"])
        content = core.automatic_content(raw, identity)
        selected, selection = release_status.select_content(content, review, "accepted")
        self.assertEqual([entry["commits"] for entry in selected["entries"]], [["a" * 40]])
        self.assertEqual(review["allowedStatusNames"], ["完成|待归档"])
        self.assertEqual(release_status.select_content(content, review, "all")[0], content)
        text = release_status.markdown(review, selection, identity, "TARGET")
        self.assertIn("- 指定状态：完成\\|待归档", text)
        for github in (False, True):
            text, _ = core.render(selected, [], identity, "TARGET", github=github, status_review=review, selection=selection)
            self.assertIn("指定状态：完成|待归档", text)
            self.assertNotIn("指定状态：等待确认", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
