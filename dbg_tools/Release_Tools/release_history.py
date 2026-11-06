"""客户变更的 Git 历史筛选；原始工程记录始终保持完整。"""
from collections import Counter
import copy
from pathlib import Path
import re
import tempfile

RULE_VERSION = "effective-history-v2"
SHA = r"[0-9a-f]{40}(?:[0-9a-f]{24})?"
REVERT = re.compile(r"^This reverts commit (" + SHA + r")(?:\.|, reversing)?[ \t]*$", re.M | re.I)
LABELS = {
    "clean_merge": "纯合并流程记录，实际变更由关联提交表达",
    "revert_pair": "原提交与完整回退抵消",
    "merge_effect_review": "合并结果包含额外变化或无法证明为纯合并，保留复核",
    "revert_target_outside_range": "回退目标位于本次范围外，保留实际撤销变化",
    "revert_effect_review": "回退不完整、目标不明确或关系复杂，保留复核",
}
DETAIL_LABELS = {
    "revert_target_missing": "未找到标准格式的完整回退目标 SHA",
    "revert_target_ambiguous": "存在多个回退目标，不能确定配对关系",
    "revert_target_not_single_parent": "目标不是单父提交，暂不自动抵消",
    "revert_target_not_ancestor": "目标不是回退前状态的祖先",
    "revert_target_empty": "目标提交没有文件变化",
    "revert_paths_differ": "修改路径集合不同，可能是部分回退或附带了其他文件修改",
    "revert_chain_ambiguous": "同一目标被多次回退，相关链需要人工核对",
    "non_regular_file": "非普通文本文件或涉及增删，无法按文本三方结果证明抵消",
    "file_mode_mismatch": "文件模式变化不符合预期",
    "custom_merge_driver": "配置了自定义合并驱动，未执行该驱动",
    "file_size_limit": "文件超过 1 MiB 文本取证限额",
    "binary_content": "二进制内容不能按文本三方结果证明抵消",
    "text_merge_conflict": "三方校验存在冲突，可能改动了相同位置",
    "text_merge_mismatch": "实际结果与完整操作的三方计算结果不同，可能是部分回退或附加修改",
    "text_merge_failed": "Git 文本三方校验执行失败",
    "git_evidence_unavailable": "Git 历史证据读取失败，未据此排除提交",
}
PROOF_LABELS = {"tree-deltas": "文件树差异一致", "text-three-way": "文本三方结果逐字节一致",
                "inverse-tree-deltas": "完整文件变化互逆", "inverse-text-three-way": "回退及重新应用的三方结果均逐字节一致"}


def _delta(runner, before, after):
    # 不启用 rename/textconv/external diff；同时比较路径、模式和完整 blob ID，支持二进制。
    text = runner.run(["git", "diff-tree", "--no-commit-id", "--raw", "--no-abbrev", "--no-renames",
                       "--no-ext-diff", "-r", "-z", before, after, "--"], binary=True)[0]
    # Git 路径是字节串，避免文本换行转换把 Linux 的不同路径误认为相同。
    parts = text.split(b"\0")
    result = {}
    for index in range(0, len(parts) - 1, 2):
        old_mode, new_mode, old_hash, new_hash, _ = parts[index].decode("ascii").lstrip(":").split()
        result[parts[index + 1]] = ((old_mode, old_hash), (new_mode, new_hash))
    return result


def _clean_merge(runner, sha, parents):
    if len(parents) != 2:
        return None
    bases = runner.git("merge-base", "--all", *parents, allowed=(0, 1))[0].splitlines()
    if len(bases) != 1:
        return None
    left, right, merged = [_delta(runner, bases[0], ref) for ref in (*parents, sha)]
    expected = {}
    text_merge = False
    for path in left.keys() | right.keys():
        if path in left and path in right and left[path] != right[path]:
            actual = merged.get(path, (left[path][0], left[path][0]))
            if not _text_merge_check(runner, left[path], right[path], actual)[0]:
                return None
            if actual[0] != actual[1]:
                expected[path] = actual
            text_merge = True
        else:
            expected[path] = left.get(path, right.get(path))
    return ("text-three-way" if text_merge else "tree-deltas") if merged == expected else None


def _text_merge_check(runner, left, right, actual):
    """使用旧版 Git 同样具备的 merge-file；逐字节核对，不写对象库或索引。"""
    base, ours, theirs, result = left[0], left[1], right[1], actual[1]
    if base != right[0] or base != actual[0] or any(mode not in {"100644", "100755"} for mode, _ in (base, ours, theirs, result)):
        return False, "non_regular_file"
    mode = ours[0] if ours[0] == theirs[0] or theirs[0] == base[0] else theirs[0] if ours[0] == base[0] else None
    if result[0] != mode:
        return False, "file_mode_mismatch"
    # 不执行项目自定义驱动；超大文件或二进制同文件冲突交由人工复核。
    if runner.git("config", "--get-regexp", r"^merge\..*\.driver$", allowed=(0, 1))[1] == 0:
        return False, "custom_merge_driver"
    blobs = []
    for _, sha in (ours, base, theirs, result):
        if int(runner.git("cat-file", "-s", sha)[0]) > 1024 * 1024:
            return False, "file_size_limit"
        data = runner.run(["git", "cat-file", "blob", sha], binary=True)[0]
        if b"\0" in data:
            return False, "binary_content"
        blobs.append(data)
    with tempfile.TemporaryDirectory(prefix=".history-", dir=runner.temp_root) as tmp:
        paths = [Path(tmp) / name for name in ("ours", "base", "theirs")]
        for path, data in zip(paths, blobs):
            path.write_bytes(data)
        output, code, _ = runner.run(["git", "merge-file", "-p", *paths], allowed=tuple(range(256)), binary=True)
        if code:
            return False, "text_merge_conflict" if code < 128 else "text_merge_failed"
        return (True, None) if output == blobs[3] else (False, "text_merge_mismatch")


def _revert_proof(runner, forward, backward):
    if not forward:
        return None, "revert_target_empty"
    if forward.keys() != backward.keys():
        return None, "revert_paths_differ"
    proof = "inverse-tree-deltas"
    for path, (before, after) in forward.items():
        current, reverted = backward[path]
        if (current, reverted) == (after, before):
            continue
        # P→C 是原改动，U→V 是回退；以 C 为基点合并 U/P 得到 V，再以 P 为基点合并 V/C 恢复 U。
        # 两向均无冲突且字节相等，才接受同文件保留了其他独立修改的完整抵消。
        for left, right, actual in (((after, current), (after, before), (after, reverted)),
                                    ((before, reverted), (before, after), (before, current))):
            matched, reason = _text_merge_check(runner, left, right, actual)
            if not matched:
                return None, reason
        proof = "inverse-text-three-way"
    return proof, None


def analyze(runner, raw, previous, target):
    """按拓扑处理；只有可证明的整笔抵消才隐藏，不按 JIRA 去重。"""
    hashes = {row["hash"] for row in raw}
    topology = runner.git("rev-list", "--topo-order", "--reverse", "--parents", previous + ".." + target, "--")[0]
    parents = {row[0]: row[1:] for row in map(str.split, topology.splitlines())}
    if set(parents) != hashes:
        raise ValueError("历史分析提交范围与原始记录不一致")
    result = {"ruleVersion": RULE_VERSION, "commits": {}, "exclusions": [], "review": []}
    records, exclusions = result["commits"], {}
    edges = {}
    for sha, ancestor in parents.items():
        record = records[sha] = {"parents": ancestor, "revertTarget": None, "reasonCode": None, "detailCode": None,
                                 "proof": None, "relatedCommits": []}
        try:
            if len(ancestor) > 1:
                proof = _clean_merge(runner, sha, ancestor)
                record["reasonCode"] = "clean_merge" if proof else "merge_effect_review"
                record["proof"] = proof
                related = runner.git("rev-list", *ancestor[1:], "^" + ancestor[0], "--")[0].splitlines()
                record["relatedCommits"] = sorted(hashes.intersection(related))
                if proof:
                    exclusions[sha] = "clean_merge"
                continue
            body = runner.git("show", "-s", "--format=%B", sha)[0]
            targets = REVERT.findall(body)
            if not targets and not re.match(r"\s*(?:revert\b|回退|回滚|撤销)", body, re.I):
                continue
            record["reasonCode"] = "revert_effect_review"
            if len(targets) != 1:
                record["detailCode"] = "revert_target_missing" if not targets else "revert_target_ambiguous"
                continue
            reverted = targets[0].lower()
            record["revertTarget"] = reverted
            record["relatedCommits"] = [reverted]
            if reverted not in hashes:
                record["reasonCode"] = "revert_target_outside_range"
                continue
            if len(ancestor) != 1 or len(parents[reverted]) != 1:
                record["detailCode"] = "revert_target_not_single_parent"
                continue
            _, code, _ = runner.git("merge-base", "--is-ancestor", reverted, ancestor[0], allowed=(0, 1))
            if code:
                record["detailCode"] = "revert_target_not_ancestor"
                continue
            forward = _delta(runner, parents[reverted][0], reverted)
            backward = _delta(runner, ancestor[0], sha)
            record["proof"], record["detailCode"] = _revert_proof(runner, forward, backward)
            if record["proof"]:
                edges[sha] = reverted
        except Exception:
            # 取证失败不得变成客户漏项；范围读取失败则由上面的整体校验终止。
            record["reasonCode"] = "merge_effect_review" if len(ancestor) > 1 else "revert_effect_review"
            record["proof"] = None
            record["detailCode"] = "git_evidence_unavailable"

    # 同一目标多次被回退（通常涉及重引入）不作简单配对，整条相关链保留复核。
    counts = Counter(edges.values())
    ambiguous = {sha for sha, count in counts.items() if count > 1}
    while True:
        expanded = ambiguous | {x for r, t in edges.items() if r in ambiguous or t in ambiguous for x in (r, t)}
        if expanded == ambiguous:
            break
        ambiguous = expanded
    # 从最新回退向前消对：C→R1→R2 保留 C，隐藏 R1/R2；再次回退则全部抵消。
    for sha in reversed(parents):
        reverted = edges.get(sha)
        if not reverted or sha in ambiguous or sha in exclusions or reverted in exclusions:
            continue
        proof = records[sha]["proof"]
        for current, other in ((sha, reverted), (reverted, sha)):
            exclusions[current] = "revert_pair"
            records[current].update(reasonCode="revert_pair", relatedCommits=[other], proof=proof, detailCode=None)
    for sha, record in records.items():
        if sha in exclusions:
            result["exclusions"].append({"hash": sha, "reasonCode": exclusions[sha],
                                         "relatedCommits": record["relatedCommits"], "proof": record["proof"]})
        elif record["reasonCode"]:
            if sha in ambiguous:
                record["detailCode"] = "revert_chain_ambiguous"
            result["review"].append({"hash": sha, "reasonCode": record["reasonCode"]})
    return result


def select_content(content, history):
    """完整隐藏的人工条目可移除；混合抵消与有效变化的人工文字须先拆项。"""
    excluded = {row["hash"]: row["reasonCode"] for row in history["exclusions"]}
    result = copy.deepcopy(content)
    kept, removed = [], []
    for entry in result["entries"]:
        hidden = set(entry["commits"]) & excluded.keys()
        if hidden == set(entry["commits"]):
            removed.append(entry["id"])
            continue
        if any(excluded[sha] == "revert_pair" for sha in hidden):
            raise ValueError("人工条目混合已抵消与有效提交，请先拆分条目后重新生成: " + entry["id"])
        entry["commits"] = [sha for sha in entry["commits"] if sha not in hidden]
        kept.append(entry)
    result["entries"] = kept
    if "customerExclusions" in result:
        result["customerExclusions"] = [row for row in result["customerExclusions"] if row["hash"] not in excluded]
    return result, removed
