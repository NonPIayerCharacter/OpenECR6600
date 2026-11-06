"""同批 JIRA 状态统计、客户条目筛选与研发清单；不重新查询或修改 JIRA。"""
import html
import re
from urllib.parse import quote

from release_classification import ISSUE, issue_keys
import release_project

SCOPES = {"all": "全部记录", "accepted": "仅包含指定 JIRA 状态的记录"}
REASONS = {"no_issue_key": "提交无 JIRA 编号", "jira_unavailable": "JIRA 未取得",
           "status_unknown": "JIRA 状态未知或别名查询结果不一致", "status_not_allowed": "JIRA 不处于指定状态"}


def build_review(raw, jira, accepted_statuses=None):
    """统计始终覆盖原始范围；同一 issueId 的旧编号/新编号合为一个真实事项。"""
    accepted_statuses = release_project.load()["acceptedJiraStatuses"] if accepted_statuses is None else accepted_statuses
    groups, key_to_id, unavailable = {}, {}, []
    for key in issue_keys(raw):
        result = jira["results"].get(key)
        if not result or result["status"] != "fetched":
            unavailable.append({"key": key, "reason": (result or {}).get("reasonCode") or "jira_disabled"})
            continue
        issue_id = result["issueId"]
        key_to_id[key] = issue_id
        groups.setdefault(issue_id, []).append((key, result))
    issues, by_id = [], {}
    base_url = (jira.get("configSnapshot") or {}).get("baseUrl", "").rstrip("/")
    for issue_id, aliases in groups.items():
        first = aliases[0][1]
        fields = first["fields"]
        statuses = {(r["fields"]["status"]["id"], r["fields"]["status"]["name"].strip())
                    if r["fields"]["status"] else None for _, r in aliases}
        state = next(iter(statuses)) if len(statuses) == 1 else None
        group = "unknown" if not state or not state[1] else "accepted" if state[1] in accepted_statuses else "other"
        keys = sorted(k for k, _ in aliases)
        owners = set()
        for _, result in aliases:
            owner = result["fields"].get("assignee")
            if "assignee" not in result["fields"] or {"missing_assignee", "invalid_assignee"} & set(result["warnings"]):
                owners.add("未取得")
            elif owner is None:
                owners.add("未分配")
            else:
                label = owner["displayName"]
                owners.add(label + (" (" + owner["name"] + ")" if owner["name"] and owner["name"] != label else ""))
        row = {"issueId": issue_id, "key": first["canonicalKey"], "requestedKeys": keys,
               "summary": fields["summary"], "status": {"id": state[0], "name": state[1]} if state else None,
               "group": group, "issueType": fields["issueType"],
               "assigneeName": next(iter(owners)) if len(owners) == 1 else "未知 / 不一致",
               "url": base_url + "/browse/" + quote(first["canonicalKey"], safe=""),
               "commits": [dict(c) for c in raw if set(ISSUE.findall(c["subject"])) & set(keys)]}
        issues.append(row)
        by_id[issue_id] = row
    issues.sort(key=lambda r: r["key"])
    eligibility = {}
    for commit in raw:
        keys = sorted(set(ISSUE.findall(commit["subject"])))
        reasons = {"no_issue_key"} if not keys else set()
        for key in keys:
            group = by_id[key_to_id[key]]["group"] if key in key_to_id else "unavailable"
            if group != "accepted":
                reasons.add({"unknown": "status_unknown", "other": "status_not_allowed", "unavailable": "jira_unavailable"}[group])
        eligibility[commit["hash"]] = {"eligible": not reasons, "reasonCodes": sorted(reasons)}
    return {"schemaVersion": 1, "allowedStatusNames": list(accepted_statuses), "jiraMode": jira["mode"],
            "queryStartedAt": jira.get("startedAt"), "queryFinishedAt": jira.get("finishedAt"),
            "counts": {"rawCommitCount": len(raw), "associatedIssueCount": len(issues),
                       "acceptedIssueCount": sum(r["group"] == "accepted" for r in issues),
                       "otherStatusIssueCount": sum(r["group"] == "other" for r in issues),
                       "unknownStatusIssueCount": sum(r["group"] == "unknown" for r in issues),
                       "unavailableIssueKeyCount": len(unavailable),
                       "noIssueCommitCount": sum(not ISSUE.search(c["subject"]) for c in raw)},
            "issues": issues, "unavailableIssues": unavailable,
            "noIssueCommits": [dict(c) for c in raw if not ISSUE.search(c["subject"])], "commitEligibility": eligibility}


def select_content(content, review, scope):
    """人工合并条目整体筛选，不截断文案；仅把原本有映射的提交计为筛选排除。"""
    kept, excluded = [], []
    for entry in content["entries"]:
        reasons = sorted({r for sha in entry["commits"] for r in review["commitEligibility"][sha]["reasonCodes"]})
        if scope == "accepted" and reasons:
            excluded.append({**entry, "reasonCodes": reasons})
        else:
            kept.append(entry)
    before = {sha for e in content["entries"] for sha in e["commits"]}
    after = {sha for e in kept for sha in e["commits"]}
    selection = {"scope": scope, "label": SCOPES[scope], "includedEntryCount": len(kept),
                 "includedCommitCount": len(after), "filteredEntryCount": len(excluded),
                 "filteredCommits": sorted(before - after), "excludedEntries": excluded}
    return {**content, "entries": kept}, selection


def markdown(review, selection, identity, version):
    # 表格字段来自 Git/JIRA，编号不生成链接；转义 Markdown，防止伪造行或链接。
    def cell(value):
        text = html.escape(str(value), quote=False).replace("\r", " ").replace("\n", " ")
        return re.sub(r"([\\`*_|\[\]])", r"\\\1", text)

    counts = review["counts"]
    output = ["# JIRA 状态核对 — " + cell(version), "", "- 基线：" + identity["previous"]["commit"],
              "- 目标：" + identity["target"]["commit"], "- 生成范围：" + selection["label"],
              "- 指定状态：" + "、".join(cell(value) for value in review["allowedStatusNames"]),
              "- 查询开始：" + (review["queryStartedAt"] or "未查询"),
              "- 查询结束：" + (review["queryFinishedAt"] or "未查询"),
              "", "状态来自本批查询快照，不代表目标版本发布时的历史状态或测试结论。筛选仅影响客户发布说明，目标代码和工程记录保留全部合入。",
              "", "## 范围与数量", "", "| 指标 | 数量 |", "| --- | --- |"]
    labels = {"rawCommitCount": "Git 提交数", "associatedIssueCount": "关联真实 JIRA 数（issueId 去重）",
              "acceptedIssueCount": "指定状态 JIRA 数", "otherStatusIssueCount": "未提交状态 JIRA 数",
              "unknownStatusIssueCount": "状态未知 JIRA 数", "unavailableIssueKeyCount": "未取得的 JIRA 编号数",
              "noIssueCommitCount": "无 JIRA 编号提交数"}
    output += [f"| {label} | {counts[key]} |" for key, label in labels.items()]
    output += [f"| 纳入客户条目 / 对应提交 | {selection['includedEntryCount']} / {selection['includedCommitCount']} |",
               f"| 按状态筛选排除条目 / 完全移出的提交 | {selection['filteredEntryCount']} / {len(selection['filteredCommits'])} |",
               "", "同一事项多次合入仅计一个 JIRA；同一提交可关联多个 JIRA，人工条目可拆分或合并，三种数量不相等。"]
    for group, title in (("other", "未提交状态 JIRA"), ("unknown", "状态未知 JIRA")):
        rows = [r for r in review["issues"] if r["group"] == group]
        output += ["", "## " + title, ""]
        if not rows:
            output += ["无。"]
            continue
        output += ["| JIRA / 关联编号 | 概要 | 状态 | 当前处理人 | 类型 | 关联提交数 |", "| --- | --- | --- | --- | --- | --- |"]
        for row in rows:
            output.append("| " + cell(row["key"]) + " / " + cell(', '.join(row["requestedKeys"])) + " | " +
                          " | ".join(cell(v) for v in (row["summary"], (row["status"] or {}).get("name") or "未知/不一致",
                                                       row["assigneeName"], row["issueType"]["name"], len(row["commits"]))) + " |")
        output += ["", "### 对应提交", ""]
        for row in rows:
            output += ["- " + cell(row["key"]) + "："]
            output += ["  - `" + c["hash"] + "` " + cell(c["subject"]) for c in row["commits"]]
    output += ["", "## 无 JIRA 编号提交", ""]
    output += ["- `" + c["hash"] + "` " + cell(c["subject"]) for c in review["noIssueCommits"]] or ["无。"]
    output += ["", "## 未取得的 JIRA 编号", ""]
    output += ["- " + cell(r["key"] + "：" + r["reason"]) for r in review["unavailableIssues"]] or ["无。"]
    output += ["", "## 按状态筛选排除的客户条目", ""]
    if not selection["excludedEntries"]:
        output += ["无。当前使用全部记录模式。" if selection["scope"] == "all" else "无。"]
    for entry in selection["excludedEntries"]:
        output += ["- " + cell(entry["id"] + "：" + entry["text"]),
                   "  - 原因：" + "；".join(REASONS[r] for r in entry["reasonCodes"]),
                   "  - 关联提交：" + ", ".join(entry["commits"])]
    return "\n".join(output) + "\n"
