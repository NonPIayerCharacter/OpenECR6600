"""状态口径、筛选与覆盖契约；使用模拟事项，不访问 JIRA。"""
import copy
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate_release_notes as core
import release_classification as classification
import release_status as status
from test_release_classification import report


class StatusTests(unittest.TestCase):
    def setUp(self):
        subjects = ['[ECW6700-1] fix first', '[ECW6700-1] fix second', '[ECW6700-2] fix pending',
                    '[ECW6700-1][ECW6700-2] fix mixed', '[ECW6700-3] fix unknown', 'build: no issue']
        self.raw = [{"hash": f"{i + 1:040x}", "subject": text} for i, text in enumerate(subjects)]
        self.jira = report({"ECW6700-1": "11101", "ECW6700-2": "11101", "ECW6700-3": "11101"})
        for key, name in (("ECW6700-1", "等待确认"), ("ECW6700-2", "处理中")):
            self.jira["results"][key]["fields"]["status"] = {"id": key[-1], "name": name}
        self.identity = {"previous": {"commit": "0" * 40, "tag": "BASE", "type": "tag"},
                         "target": {"commit": self.raw[-1]["hash"], "tag": "TARGET", "type": "tag"}}
        self.content = core.automatic_content(self.raw, self.identity)

    def test_counts_use_real_issues_and_entire_range(self):
        review = status.build_review(self.raw, self.jira)
        self.assertEqual(review["counts"], {"rawCommitCount": 6, "associatedIssueCount": 3, "acceptedIssueCount": 1,
                         "otherStatusIssueCount": 1, "unknownStatusIssueCount": 1, "unavailableIssueKeyCount": 0, "noIssueCommitCount": 1})
        self.assertEqual(len(review["issues"][0]["commits"]), 3)
        selected, selection = status.select_content(self.content, review, "accepted")
        self.assertEqual([e["commits"][0] for e in selected["entries"]], [r["hash"] for r in self.raw[:2]])
        self.assertEqual(selection["filteredEntryCount"], 4)
        self.assertEqual(len(selection["filteredCommits"]), 4)
        self.assertEqual(len(self.content["entries"]), 6)
        self.assertEqual(status.select_content(self.content, review, "all")[0], self.content)
        self.assertEqual(review["counts"]["associatedIssueCount"], 3)

    def test_aliases_deduplicate_and_status_disagreement_is_unknown(self):
        self.jira["results"]["ECW6700-2"].update(issueId="123", canonicalKey="ECW6700-1")
        self.jira["results"]["ECW6700-2"]["fields"]["status"] = {"id": "1", "name": "等待确认"}
        review = status.build_review(self.raw, self.jira)
        self.assertEqual(review["counts"]["associatedIssueCount"], 2)
        self.assertEqual(review["issues"][0]["requestedKeys"], ["ECW6700-1", "ECW6700-2"])
        self.assertEqual(len(review["issues"][0]["commits"]), 4)
        self.jira["results"]["ECW6700-2"]["fields"]["status"]["name"] = "处理中"
        review = status.build_review(self.raw, self.jira)
        self.assertEqual(review["counts"]["unknownStatusIssueCount"], 2)
        self.assertFalse(review["commitEligibility"][self.raw[0]["hash"]]["eligible"])

    def test_exact_status_names_no_resolution_or_priority_inference(self):
        for name, expected in (("等待确认", True), ("已解决", True), ("已关闭", True), (" 已关闭 ", True),
                               ("未解决", False), ("等待确认中", False), ("Closed", False), ("", False)):
            with self.subTest(name=name):
                self.jira["results"]["ECW6700-1"]["fields"].update(status={"id": "1", "name": name}, resolution={"id": "1", "name": "Fixed"})
                self.assertEqual(status.build_review(self.raw, self.jira)["commitEligibility"][self.raw[0]["hash"]]["eligible"], expected)

    def test_merged_manual_entries_whole_exclusion_preserves_shared_commit(self):
        entries = copy.deepcopy(self.content["entries"][:2])
        entries[1]["commits"] = [self.raw[0]["hash"], self.raw[2]["hash"]]
        content = {**self.content, "entries": entries}
        selected, selection = status.select_content(content, status.build_review(self.raw, self.jira), "accepted")
        self.assertEqual(selected["entries"], entries[:1])
        self.assertEqual(selection["filteredCommits"], [self.raw[2]["hash"]])
        self.assertEqual(selection["excludedEntries"][0]["text"], entries[1]["text"])
        # 原本未被人工映射的提交不能混进“状态排除”，否则会掩盖真正缺口。
        audit = {e["id"]: {"needsReview": False, "decisionSource": "manual", "reasonCodes": []} for e in selected["entries"]}
        coverage, _ = classification.customer_coverage(self.raw, selected, audit, filtered=selection["filteredCommits"])
        self.assertIn(self.raw[1]["hash"], coverage["unaccountedCommits"])
        with self.assertRaises(classification.ClassificationError):
            classification.customer_coverage(self.raw, selected, audit, filtered=[self.raw[0]["hash"]])

    def test_filtered_coverage_and_empty_customer_document(self):
        for result in self.jira["results"].values():
            result["fields"]["status"] = {"id": "5", "name": "处理中"}
        review = status.build_review(self.raw, self.jira)
        content, selection = status.select_content(self.content, review, "accepted")
        coverage, stats = classification.customer_coverage(self.raw, content, {}, automatic=True, filtered=selection["filteredCommits"])
        self.assertEqual(coverage["status"], "complete")
        self.assertEqual(stats["statusFilteredCommitCount"], 6)
        self.assertEqual(stats["customerEntryCount"], 0)
        text, ids = core.render(content, [], self.identity, "TARGET", statistics=stats, status_review=review, selection=selection)
        self.assertIn("没有符合指定 JIRA 状态", text)
        self.assertIn("关联编号 3 个", text)
        self.assertEqual(ids, [])
        self.assertTrue(text.startswith("# TARGET Release Notes\n"))
        internal = core.coverage_markdown(self.raw, {"missing": [], "decisions": []}, content, [], self.identity,
                                          customer_coverage=coverage, statistics=stats)
        self.assertIn("按 JIRA 状态筛选排除", internal)
        self.assertNotIn("待人工映射/说明排除", internal)

    def test_report_contains_pending_unknown_unkeyed_and_safe_text(self):
        self.jira["results"]["ECW6700-2"]["fields"]["summary"] = '<script>x</script> | [bad](javascript:x)\nnew'
        self.jira["results"]["ECW6700-2"]["fields"]["assignee"] = {"name": "E0001", "displayName": "研发甲|[x](bad)"}
        self.jira["results"]["ECW6700-3"]["fields"]["assignee"] = None
        review = status.build_review(self.raw, self.jira)
        _, selection = status.select_content(self.content, review, "accepted")
        text = status.markdown(review, selection, self.identity, "TARGET")
        for expected in ("处理中", "ECW6700-2", "ECW6700-3", "build: no issue", "按状态筛选排除", "2026-09-28T00:00:01Z"):
            self.assertIn(expected, text)
        self.assertNotIn("<script>", text)
        self.assertNotIn("[bad](javascript:x)", text)
        self.assertNotIn("/browse/", text)
        self.assertIn("| 当前处理人 |", text)
        self.assertIn(r"研发甲\|\[x\](bad) (E0001)", text)
        self.assertIn("未分配", text)

    def test_assignee_unavailable_and_alias_disagreement(self):
        self.assertEqual(status.build_review(self.raw, self.jira)["issues"][0]["assigneeName"], "未取得")
        first = self.jira["results"]["ECW6700-1"]
        second = self.jira["results"]["ECW6700-2"]
        first["fields"]["assignee"] = {"name": "dev", "displayName": "研发甲"}
        second.update(issueId=first["issueId"], canonicalKey=first["canonicalKey"])
        second["fields"]["assignee"] = {"name": "other", "displayName": "研发乙"}
        self.assertEqual(status.build_review(self.raw, self.jira)["issues"][0]["assigneeName"], "未知 / 不一致")
        second["fields"]["assignee"] = first["fields"]["assignee"]
        self.assertEqual(status.build_review(self.raw, self.jira)["issues"][0]["assigneeName"], "研发甲 (dev)")
        first["fields"]["assignee"] = second["fields"]["assignee"] = None
        first["warnings"] = ["invalid_assignee"]
        second["warnings"] = ["missing_assignee"]
        self.assertEqual(status.build_review(self.raw, self.jira)["issues"][0]["assigneeName"], "未取得")

    def test_titles_and_offline_mode_boundaries(self):
        for product in ("", "  ", "待填写"):
            text, _ = core.render({**self.content, "product": product}, [], self.identity, "TARGET")
            self.assertEqual(text.splitlines()[0], "# TARGET Release Notes")
        text, _ = core.render({**self.content, "product": "ECW SDK"}, [], self.identity, "TARGET", github=True)
        self.assertEqual(text.splitlines()[0], "# ECW SDK TARGET")
        with self.assertRaises(core.ReleaseError):
            core.generation_options(core.parser().parse_args(["--no-jira", "--jira-status-scope", "accepted"]), core.TOOLS)
        review = status.build_review(self.raw, classification.disabled_jira())
        self.assertEqual(review["counts"]["associatedIssueCount"], 0)
        self.assertEqual(review["counts"]["unavailableIssueKeyCount"], 3)
        self.assertEqual(status.select_content(self.content, review, "all")[0], self.content)


if __name__ == "__main__":
    unittest.main(verbosity=2)
