"""真实临时 Git 图验证；不访问 JIRA，不改 SDK 历史。"""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate_release_notes as core
import release_history as history


class HistoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release-history-")
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / "repo 中文 space"
        self.repo.mkdir()
        self.runner = core.Runner(self.repo)
        self.git("init", "-b", "main")
        for key, value in (("user.name", "History Test"), ("user.email", "history@example.invalid"),
                           ("commit.gpgsign", "false"), ("core.autocrlf", "false"),
                           ("core.hooksPath", str(self.repo / "no-hooks"))):
            self.git("config", key, value)
        self.base = self.commit("baseline", {"shared.txt": "".join(f"line {i}\n" for i in range(20)), "base.txt": "base"})

    def git(self, *args, **kwargs):
        return self.runner.git(*args, **kwargs)[0].strip()

    def commit(self, message, files):
        for name, value in files.items():
            path = self.repo / name
            if value is None:
                path.unlink()
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(value if isinstance(value, bytes) else value.encode("utf-8"))
        self.git("add", "-A")
        self.git("commit", "-m", message)
        return self.git("rev-parse", "HEAD")

    def analyze(self, base=None):
        target = self.git("rev-parse", "HEAD")
        raw = [dict(zip(("hash", "subject"), line.split("\t", 1)))
               for line in self.git("log", "--format=%H%x09%s", (base or self.base) + ".." + target).splitlines()]
        return history.analyze(self.runner, raw, base or self.base, target)

    def hidden(self, result):
        return {row["hash"] for row in result["exclusions"]}

    def merge_fixture(self, same_file=False):
        self.git("checkout", "-b", "topic")
        first = self.commit("[ECR6600-1] add feature", {"side.txt": "one"} if not same_file else
                            {"shared.txt": "first\n" + "".join(f"line {i}\n" for i in range(1, 20))})
        self.git("checkout", "main")
        second = self.commit("[ECR6600-1] improve feature", {"main.txt": "two"} if not same_file else
                             {"shared.txt": "".join(f"line {i}\n" for i in range(19)) + "last\n"})
        self.git("merge", "--no-ff", "topic", "-m", "Merge branch 'topic'")
        return first, second, self.git("rev-parse", "HEAD")

    def test_clean_merge_keeps_distinct_changes_with_same_issue(self):
        first, second, merge = self.merge_fixture()
        result = self.analyze()
        self.assertEqual(self.hidden(result), {merge})
        self.assertEqual(result["commits"][merge]["relatedCommits"], [first])
        self.assertNotIn(second, self.hidden(result))

    def test_parent_graph_not_subject_and_all_projects(self):
        commits = [self.commit(f"Merge branch '[{project}-1]'", {"counter.txt": project})
                   for project in ("ECR6600", "ECR6600A", "ECW6280", "ECW6630", "ECW6700")]
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(set(result["commits"]), set(commits))

    def test_fast_forward_style_no_ff_merge(self):
        self.git("checkout", "-b", "topic")
        change = self.commit("add option", {"new.txt": "feature"})
        self.git("checkout", "main")
        self.git("merge", "--no-ff", "topic", "-m", "integration without merge title")
        merge = self.git("rev-parse", "HEAD")
        result = self.analyze()
        self.assertEqual(self.hidden(result), {merge})
        self.assertEqual(result["commits"][merge]["relatedCommits"], [change])

    def test_same_file_merge_and_object_isolation(self):
        _, _, merge = self.merge_fixture(same_file=True)
        before = {p.relative_to(self.repo): p.read_bytes() for p in (self.repo / ".git").rglob("*") if p.is_file()}
        result = self.analyze()
        after = {p.relative_to(self.repo): p.read_bytes() for p in (self.repo / ".git").rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        self.assertEqual(self.hidden(result), {merge})
        self.assertEqual(result["commits"][merge]["proof"], "text-three-way")
        self.assertFalse(list(self.repo.glob(".history-*")))

    def test_unavailable_merge_file_conservative_fallback(self):
        _, _, merge = self.merge_fixture(same_file=True)
        run = self.runner.run
        def unavailable(args, **kwargs):
            return (b"", 129, "unsupported") if args[:2] == ["git", "merge-file"] else run(args, **kwargs)
        with mock.patch.object(self.runner, "run", side_effect=unavailable):
            result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["commits"][merge]["reasonCode"], "merge_effect_review")

    def test_custom_merge_driver_not_executed(self):
        self.merge_fixture(same_file=True)
        self.git("config", "merge.test.driver", "must-not-execute")
        with mock.patch.object(self.runner, "run", wraps=self.runner.run) as run:
            self.assertFalse(self.hidden(self.analyze()))
        self.assertFalse(any(call.args[0][:2] == ["git", "merge-file"] for call in run.call_args_list))

    def test_extra_merge_changes_retained(self):
        self.git("checkout", "-b", "topic")
        self.commit("add option", {"new.txt": "one"})
        self.git("checkout", "main")
        self.git("merge", "--no-ff", "--no-commit", "topic")
        merge = self.commit("[ECW6700-1] fix integration", {"extra.txt": "resolution effect"})
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["review"], [{"hash": merge, "reasonCode": "merge_effect_review"}])

    def test_octopus_merge_kept_for_review(self):
        sides = []
        for name in ("side1", "side2"):
            self.git("checkout", "-b", name, self.base)
            sides.append(self.commit("add " + name, {name + ".txt": name}))
        self.git("checkout", "main")
        main = self.commit("main", {"main.txt": "main"})
        # Git for Windows 旧版 octopus shell helper 可能缺失，使用 plumbing 构造同样的三父 DAG。
        for name in ("side1", "side2"):
            (self.repo / (name + ".txt")).write_text(name, encoding="utf-8")
        self.git("add", "-A")
        tree = self.git("write-tree")
        merge = self.git("commit-tree", tree, "-p", main, "-p", sides[0], "-p", sides[1], "-m", "Merge topics")
        self.git("update-ref", "refs/heads/main", merge)
        result = self.analyze()
        self.assertEqual(len(result["commits"][merge]["parents"]), 3)
        self.assertNotIn(merge, self.hidden(result))

    def test_binary_same_path_merge_not_silently_hidden(self):
        base = self.commit("binary base", {"binary.dat": b"\0base"})
        self.git("checkout", "-b", "binary-side")
        self.commit("binary side", {"binary.dat": b"\0side"})
        self.git("checkout", "main")
        self.commit("binary main", {"binary.dat": b"\0main"})
        self.git("merge", "--no-ff", "binary-side", allowed=(1,))
        merge = self.commit("Merge binary", {"binary.dat": b"\0resolved"})
        self.assertNotIn(merge, self.hidden(self.analyze(base)))

    def test_conflict_resolution_and_ours_merge_retained(self):
        self.git("checkout", "-b", "topic")
        self.commit("side", {"base.txt": "side"})
        self.git("checkout", "main")
        self.commit("main", {"base.txt": "main"})
        self.git("merge", "--no-ff", "topic", allowed=(1,))
        merge = self.commit("Merge with resolution", {"base.txt": "resolved"})
        self.assertNotIn(merge, self.hidden(self.analyze()))
        self.git("checkout", "-b", "more", self.base)
        self.commit("other", {"other.txt": "discarded by ours"})
        self.git("checkout", "main")
        self.git("merge", "--no-ff", "-s", "ours", "more", "-m", "Merge branch 'more'")
        self.assertNotIn(self.git("rev-parse", "HEAD"), self.hidden(self.analyze()))

    def test_exact_revert_with_unrelated_intervening_change(self):
        change = self.commit("[ECW6280-1] add option", {"new.txt": "one"})
        unrelated = self.commit("[ECW6280-1] fix other", {"other.txt": "two"})
        self.git("revert", "--no-edit", change)
        revert = self.git("rev-parse", "HEAD")
        result = self.analyze()
        self.assertEqual(self.hidden(result), {change, revert})
        self.assertNotIn(unrelated, self.hidden(result))

    def test_revert_preserves_independent_changes_in_same_file(self):
        lines = [f"line {i}\n" for i in range(20)]
        lines[1] = "new feature\n"
        change = self.commit("[ECR6600A-1] add option", {"shared.txt": "".join(lines)})
        lines[18] = "independent fix\n"
        other = self.commit("[ECR6600A-1] fix independent issue", {"shared.txt": "".join(lines)})
        self.git("revert", "--no-edit", change)
        revert = self.git("rev-parse", "HEAD")
        before = {p.relative_to(self.repo): p.read_bytes() for p in (self.repo / ".git").rglob("*") if p.is_file()}
        result = self.analyze()
        self.assertEqual(self.hidden(result), {change, revert})
        self.assertEqual(result["commits"][revert]["proof"], "inverse-text-three-way")
        self.assertIsNone(result["commits"][revert]["detailCode"])
        self.assertNotIn(other, self.hidden(result))
        self.assertIn("independent fix", (self.repo / "shared.txt").read_text())
        self.assertEqual(before, {p.relative_to(self.repo): p.read_bytes() for p in (self.repo / ".git").rglob("*") if p.is_file()})
        self.git("revert", "--no-edit", revert)
        restored = self.git("rev-parse", "HEAD")
        self.assertEqual(self.hidden(self.analyze()), {revert, restored})

    def test_partial_revert_in_same_file_has_specific_reason(self):
        lines = [f"line {i}\n" for i in range(20)]
        lines[1], lines[10] = "feature one\n", "feature two\n"
        change = self.commit("add two options", {"shared.txt": "".join(lines)})
        lines[18] = "independent fix\n"
        self.commit("fix other", {"shared.txt": "".join(lines)})
        lines[1] = "line 1\n"
        partial = self.commit('Revert "add two options"\n\nThis reverts commit ' + change + '.', {"shared.txt": "".join(lines)})
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["commits"][partial]["detailCode"], "text_merge_mismatch")

    def test_revert_with_extra_edit_in_same_file_is_not_hidden(self):
        lines = [f"line {i}\n" for i in range(20)]
        lines[1] = "feature\n"
        change = self.commit("add option", {"shared.txt": "".join(lines)})
        lines[18] = "independent fix\n"
        self.commit("fix other", {"shared.txt": "".join(lines)})
        lines[1], lines[10] = "line 1\n", "extra behavior\n"
        revert = self.commit('Revert "add option"\n\nThis reverts commit ' + change + '.', {"shared.txt": "".join(lines)})
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["commits"][revert]["detailCode"], "text_merge_mismatch")

    def test_conflicting_revert_and_evidence_failure_explain_retention(self):
        change = self.commit("add option", {"base.txt": "feature"})
        self.commit("modify same option", {"base.txt": "changed feature"})
        revert = self.commit('Revert "add option"\n\nThis reverts commit ' + change + '.', {"base.txt": "base"})
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["commits"][revert]["detailCode"], "text_merge_conflict")
        with mock.patch.object(history, "_delta", side_effect=ValueError("object unavailable")):
            unavailable = self.analyze()
        self.assertEqual(unavailable["commits"][revert]["detailCode"], "git_evidence_unavailable")

    def test_binary_with_independent_edit_stays_reviewable(self):
        change = self.commit("add option", {"base.txt": b"\0feature\nbase\n"})
        self.commit("other edit", {"base.txt": b"\0feature\nother\n"})
        revert = self.commit('Revert "add option"\n\nThis reverts commit ' + change + '.', {"base.txt": b"base\nother\n"})
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["commits"][revert]["detailCode"], "binary_content")

    def test_revert_of_revert_restores_original_and_next_revert_cancels(self):
        change = self.commit("add option", {"new.txt": "one"})
        self.git("revert", "--no-edit", change)
        first = self.git("rev-parse", "HEAD")
        self.git("revert", "--no-edit", first)
        second = self.git("rev-parse", "HEAD")
        self.assertEqual(self.hidden(self.analyze()), {first, second})
        self.git("revert", "--no-edit", second)
        third = self.git("rev-parse", "HEAD")
        self.assertEqual(self.hidden(self.analyze()), {change, first, second, third})

    def test_outside_range_and_partial_reverts_remain_visible(self):
        change = self.commit("add option", {"new.txt": "one", "other.txt": "two"})
        partial = self.commit('Revert "add option"\n\nThis reverts commit ' + change + '.', {"new.txt": None})
        self.assertFalse(self.hidden(self.analyze()))
        self.assertEqual(self.analyze()["commits"][partial]["detailCode"], "revert_paths_differ")
        self.assertEqual(self.analyze(change)["commits"][partial]["reasonCode"], "revert_target_outside_range")

    def test_external_original_full_revert_retained(self):
        change = self.commit("add option", {"new.txt": "one"})
        self.git("revert", "--no-edit", change)
        result = self.analyze(change)
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["review"][0]["reasonCode"], "revert_target_outside_range")

    def test_reintroduction_is_not_deduplicated(self):
        change = self.commit("[ECR6600A-1] add option", {"new.txt": "one"})
        self.git("revert", "--no-edit", change)
        revert = self.git("rev-parse", "HEAD")
        self.git("cherry-pick", change)
        restored = self.git("rev-parse", "HEAD")
        self.assertEqual(self.hidden(self.analyze()), {change, revert})
        self.assertNotIn(restored, self.hidden(self.analyze()))

    def test_duplicate_target_reverts_are_reviewed_as_complex_chain(self):
        change = self.commit("add option", {"new.txt": "one"})
        self.git("revert", "--no-edit", change)
        self.git("cherry-pick", change)
        self.git("revert", "--no-edit", change)
        self.assertFalse(self.hidden(self.analyze()))

    def test_binary_rename_delete_and_modes_revert(self):
        self.git("mv", "base.txt", "改名.txt")
        self.git("update-index", "--chmod=+x", "shared.txt")
        change = self.commit("add binary and rename", {"binary.dat": b"\x00\xff\x80\r\n", "shared.txt": "updated"})
        self.git("revert", "--no-edit", change)
        self.assertEqual(self.hidden(self.analyze()), {change, self.git("rev-parse", "HEAD")})

    def test_merge_revert_not_silently_canceled(self):
        _, _, merge = self.merge_fixture()
        self.git("revert", "-m", "1", "--no-edit", merge)
        revert = self.git("rev-parse", "HEAD")
        result = self.analyze()
        self.assertNotIn(revert, self.hidden(result))
        self.assertEqual(result["commits"][revert]["revertTarget"], merge)
        self.assertEqual(result["commits"][revert]["reasonCode"], "revert_effect_review")
        self.assertEqual(result["commits"][revert]["detailCode"], "revert_target_not_single_parent")

    def test_missing_or_false_revert_footer_retained(self):
        change = self.commit("add option", {"new.txt": "one"})
        fake = self.commit("ordinary\n\nThis reverts commit " + change + '.', {"other.txt": "two"})
        missing = self.commit('Revert "title only"', {"third.txt": "three"})
        result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual({r["hash"] for r in result["review"]}, {fake, missing})
        self.assertEqual(result["commits"][missing]["detailCode"], "revert_target_missing")

    def test_evidence_failure_preserves_commit(self):
        _, _, merge = self.merge_fixture()
        with mock.patch.object(history, "_delta", side_effect=ValueError("object unavailable")):
            result = self.analyze()
        self.assertFalse(self.hidden(result))
        self.assertEqual(result["commits"][merge]["reasonCode"], "merge_effect_review")

    def test_manual_entries_and_coverage_accounting(self):
        first, second, merge = self.merge_fixture()
        result = self.analyze()
        entry = {"id": "manual", "commits": [first, merge], "text": "change", "category": "features", "issues": []}
        original = {"entries": [entry]}
        content, removed = history.select_content(original, result)
        self.assertEqual(content["entries"][0]["commits"], [first])
        self.assertEqual(original["entries"][0]["commits"], [first, merge])
        self.assertFalse(removed)
        audit = {"manual": {"needsReview": False, "decisionSource": "manual", "reasonCodes": []}}
        raw = [{"hash": sha, "subject": "test"} for sha in (first, second, merge)]
        cov, stats = core.classification.customer_coverage(raw, content, audit, filtered=[second], history=result["exclusions"], automatic=True)
        self.assertEqual(cov["status"], "complete")
        self.assertEqual(stats["rawCommitCount"], 3)
        self.assertEqual(stats["hiddenMergeCommitCount"], 1)
        self.assertEqual(stats["statusFilteredCommitCount"], 1)
        self.assertEqual(stats["unaccountedCommitCount"], 0)
        with self.assertRaises(ValueError):
            core.classification.customer_coverage(raw, original, audit, history=result["exclusions"])
        canceled = copy.deepcopy(result)
        canceled["exclusions"][0]["reasonCode"] = "revert_pair"
        with self.assertRaisesRegex(ValueError, "请先拆分"):
            history.select_content(original, canceled)
        only_hidden = {"entries": [{**entry, "commits": [merge]}]}
        self.assertEqual(history.select_content(only_hidden, canceled), ({"entries": []}, ["manual"]))


class HistoryPathTests(unittest.TestCase):
    def test_non_utf8_and_newline_paths_remain_distinct(self):
        prefix = b":100644 100644 " + b"a" * 40 + b" " + b"b" * 40 + b" M\0"
        runner = mock.Mock()
        runner.run.return_value = (prefix + b"name\r\n\0" + prefix + b"name\n\0" + prefix + b"\xff\0", 0, b"")
        delta = history._delta(runner, "before", "after")
        self.assertEqual(set(delta), {b"name\r\n", b"name\n", b"\xff"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
