"""客户分类、覆盖和可重放证据；不访问网络，不按 Jira 单号硬编码类别。"""
from collections import Counter
import copy
import hashlib
import json
import re
from urllib.parse import unquote, urlsplit

RULE_VERSION = "customer-classification-v3"
CATEGORIES = ("features", "improvements", "fixes", "adaptations", "unclassified")
ISSUE = re.compile(r"\b[A-Z][A-Z0-9_]*-\d+\b")
CONVENTIONAL = {"feat": "features", "fix": "fixes", "perf": "improvements", "refactor": "improvements",
                "docs": "adaptations", "build": "adaptations", "test": "adaptations", "ci": "adaptations", "chore": "adaptations"}
SOURCE_LABELS = {"manual": "人工指定", "git-rule": "提交描述", "jira-type": "JIRA 类型", "jira-field": "JIRA 发布分类字段",
                 "history-review": "Git 历史待复核", "unresolved": "依据不足"}
RULE_LABELS = {"G01": "提交类型标记", "G02": "修复动作或故障规避目的", "G03": "明确脚本、文档或配套维护",
               "G04": "明确新增功能或配置", "G05": "优化或行为调整"}


class ClassificationError(ValueError):
    pass


def check(condition, message):
    if not condition:
        raise ClassificationError(message)


def issue_keys(raw):
    return sorted({key for c in raw for key in ISSUE.findall(c["subject"])})


def empty_evidence():
    return {"requested": False, "body": "", "paths": [], "truncatedFields": [], "reasonCode": None}


def disabled_jira():
    return {"mode": "disabled", "status": "disabled", "normalizedSchemaVersion": 1,
            "configSnapshot": None, "results": {}, "startedAt": None, "finishedAt": None,
            "counts": {"keys": 0, "fetched": 0, "failed": 0, "skipped": 0, "attempts": 0},
            "snapshotDigest": None, "sourceManifestSha256": None}


def git_signals(subject):
    """先识别目的和对象，再聚合动作；连接词仅分析，不擅自拆客户条目。"""
    text = " ".join(subject.split())
    text = ISSUE.sub("", text)
    text = re.sub(r"\[[^\]]*\]|【[^】]*】", "", text).strip(" :：-")
    if re.search(r"[;；]", text):
        parts = [git_signals(part) for part in re.split(r"[;；]", text) if part.strip()]
        candidates = [candidate for found, _ in parts for candidate in found]
        reasons = {reason for _, found in parts for reason in found}
        if len({x["category"] for x in candidates}) > 1:
            reasons.add("compound_change")
        elif len(candidates) > 1:
            reasons.add("multiple_actions_review")
        return candidates, reasons
    marker = re.match(r"^(feat|fix|perf|refactor|docs|build|test|ci|chore)(?:\([^)]*\))?!?:\s*", text, re.I)
    text = text[marker.end():] if marker else re.sub(r"^(?:wifi|transport|rom_gen|ai_rules|at|ble):\s*", "", text, flags=re.I)
    reasons, candidates = set(), []
    if re.match(r"^(?:merge\b|revert\b|回退|回滚|撤销)", text, re.I):
        return [], {"requires_net_effect_review"}
    if re.search(r"默认值确认|功能未合入|\bshould\s+use\b|\bsync\b.*\bcode\b", text, re.I):
        reasons.add("problem_only" if re.search(r"默认值确认|功能未合入", text) else "insufficient_change")
    if re.search(r"未(?:新增|增加|添加|支持|合入)|尚未|\b(?:not|never)\s+(?:add|support)", text, re.I):
        reasons.add("problem_only")

    def add(category, rule, fragment):
        candidates.append({"category": category, "ruleIds": [rule], "evidenceText": fragment[:1000]})

    # 同一故障的配套手段：增加队列避免丢包、调整标志修复通知等。
    purpose = re.search(r"(?:避免|解决|规避|防止|\bavoid\b|\bprevent\b).*(?:丢包|死机|死循环|越界|失败|错误|异常|\bloss\b|\bcrash\b|\boverflow\b)", text, re.I)
    remedy = re.search(r"^将.+(?:改为|接入).*[，,].*修复", text)
    if purpose or remedy:
        add("fixes", "G02", text)
        # 故障目的只覆盖其配套手段，后续独立新增/优化仍须进入复合检查。
        tail = text[(purpose or remedy).end():]
        if re.search(r"[,，]|并|同时|\band\b", tail, re.I):
            tail = re.split(r"[,，]|并|同时|\band\b", tail, maxsplit=1, flags=re.I)[-1]
            extra, extra_reasons = git_signals(tail)
            candidates.extend(extra)
            reasons.update(extra_reasons)
    else:
        clauses = re.split(r"[;；,，]|并|同时|以及|\band\s+", text, flags=re.I)
        for clause in clauses:
            clause = clause.strip()
            if not clause or re.search(r"未(?:新增|增加|添加|支持|合入)|尚未|\b(?:not|never)\s+(?:add|support)", clause, re.I):
                continue
            feature = (re.search(r"\b(?:add(?:ed)?|support(?:ed)?)\b|新增|增加|添加|支持", clause, re.I) and re.search(
                r"命令|分区|配置|功能|接口|驱动|协议|模式|\b(?:commands?|partitions?|config(?:uration)?|cfg|options?|targets?|features?|api|interfaces?|drivers?|protocols?|modes?)\b", clause, re.I | re.ASCII)
                and not re.search(r"默认.*(?:开启|关闭)", clause))
            # 源码/目录/pinmux/API 仅描述实现对象，不能单独证明是构建适配。
            # 文档、构建脚本可直接识别；宏和迁移等配套词不能盖过明确新增功能。
            maintenance = re.search(r"\b(?:guide|doc|docs|documentation|readme|makefile|kconfig|defconfig)\b|build\s+script|构建脚本|链接脚本|指南|文档|发布内容", clause, re.I)
            if not feature:
                maintenance = maintenance or re.search(r"\bmacro\b|宏定义|初始化未使用\s*IO", clause, re.I) or (
                    re.search(r"\b(?:delete|remove)\b|删除|清理|迁移", clause, re.I) and re.search(r"目录|源码|\b(?:directory|source|code)\b", clause, re.I)) or re.search(
                    r"\bmove\b.*\bto\s+[\w.-]+/", clause, re.I)
            if re.search(r"修复|修正|解决|规避|\bfix(?:ed|es)?\b|\bwork\s*around\b|\bcorrect(?:ed)?\b|\breturn\s+when\b.*\bfail\b|\bEBADF\b.*\brecreate\b", clause, re.I):
                add("fixes", "G02", clause)
            elif re.search(r"\bclose\b.*\bprintf\b", clause, re.I):
                add("adaptations", "G03", clause)
            elif maintenance and re.search(r"\b(?:add|update|move|delete|remove|adapt)\b|新增|增加|删除|清理|迁移|固化|适配", clause, re.I):
                add("adaptations", "G03", clause)
            elif feature:
                add("features", "G04", clause)
            elif re.search(r"\b(?:opt|optimize|optimise|refactor|improve)(?:d|s|ing)?\b|优化|重构|默认.*(?:开启|关闭)|(?:改为|调整|恢复).*默认值|初始化.*(?:开启|保存)|迁入.*释放.*空间|断开使用.*keep-alive.*实现|\bexpand\s+\w+\s+to\s+\d+\b", clause, re.I):
                add("improvements", "G05", clause)
    if marker:
        add(CONVENTIONAL[marker[1].lower()], "G01", marker[0])
    if not candidates and re.search(r"初始化失败|会死机|无法|不能|\b(?:fails?|failed|cannot)\b", text, re.I):
        reasons.add("problem_only")
    categories = {c["category"] for c in candidates}
    if len(categories) > 1:
        reasons.add("compound_change")
    elif len(candidates) > 1:
        reasons.add("multiple_actions_review")
    return candidates, reasons


def jira_signals(keys, jira, fallback=False):
    candidates, reasons = [], set()
    if jira["mode"] == "disabled":
        return candidates, reasons
    config = jira["configSnapshot"]
    for key in keys:
        result = jira["results"][key]
        reasons.update(result["warnings"])
        if result["truncatedFields"]:
            reasons.add("jira_fields_truncated")
        if result["status"] != "fetched":
            reasons.add(result["reasonCode"])
            continue
        fields = result["fields"]
        project = config["projects"].get(fields["projectKey"])
        if project is None:
            reasons.add("canonical_project_not_configured")
            continue
        category_field = fields["releaseCategory"]
        if config["releaseCategoryField"] and category_field is not None:
            mapping = category_field["id"]
            category = config["releaseCategoryMap"].get(mapping)
            source = "jira-field"
            if category is None:
                reasons.add("unmapped_release_category")
        else:
            mapping = fields["issueType"]["id"]
            category = project["issueTypeMap"].get(mapping)
            if category is None and fallback:
                category = project.get("fallbackIssueTypeMap", {}).get(mapping)
            source = "jira-type"
            if category is None and mapping not in project.get("fallbackIssueTypeMap", {}):
                reasons.add("unmapped_issue_type")
        if category:
            candidates.append({"requestedKey": key, "canonicalKey": result["canonicalKey"],
                               "category": category, "source": source, "mappingId": mapping})
    if len(candidates) < len(keys) and reasons:
        reasons.add("partial_issue_evidence")
    return candidates, reasons


def classify_commit(commit, evidence=None, jira=None):
    evidence, jira = evidence or empty_evidence(), jira or disabled_jira()
    keys = sorted(set(ISSUE.findall(commit["subject"])))
    git, reasons = git_signals(commit["subject"])
    if evidence["body"]:
        body, body_reasons = git_signals(evidence["body"])
        git += body
        reasons.update(body_reasons)
        if len({c["category"] for c in git}) > 1:
            reasons.add("compound_change")
    if evidence["reasonCode"]:
        reasons.add(evidence["reasonCode"])
    if evidence["truncatedFields"]:
        reasons.add("git_evidence_truncated")
    candidates, jira_reasons = jira_signals(keys, jira)
    reasons.update(jira_reasons)
    gc, jc = {c["category"] for c in git}, {c["category"] for c in candidates}
    if len(jc) > 1:
        reasons.add("multiple_issue_categories")
    if gc and jc and gc != jc:
        reasons.add("jira_git_conflict")
    blockers = {"requires_net_effect_review", "problem_only", "insufficient_change", "compound_change",
                "multiple_issue_categories", "jira_git_conflict", "category_field_missing",
                "invalid_category_field", "unmapped_release_category",
                "canonical_project_not_configured"}
    category, source = "unclassified", "unresolved"
    if not reasons & blockers:
        if len(jc) == 1 and len(candidates) == len(keys):
            category = next(iter(jc))
            source = "jira-type" if any(c["source"] == "jira-type" for c in candidates) else "jira-field"
        elif len(gc) == 1:
            category, source = next(iter(gc)), "git-rule"
    if category == "unclassified" and not reasons:
        reasons.add("insufficient_change")
    # 只兜底未定类别；已明确的 Git/人工分类不受需求类型的兜底映射影响。
    hard_blockers = {"requires_net_effect_review", "multiple_issue_categories", "category_field_missing",
                     "invalid_category_field", "unmapped_release_category", "canonical_project_not_configured"}
    if category == "unclassified" and keys and not reasons & hard_blockers:
        fallback_candidates, fallback_reasons = jira_signals(keys, jira, fallback=True)
        candidates = fallback_candidates
        fallback_categories = {c["category"] for c in fallback_candidates}
        if len(fallback_categories) > 1:
            reasons.add("multiple_issue_categories")
        if (len(fallback_candidates) == len(keys) and len(fallback_categories) == 1 and
                not fallback_reasons & hard_blockers):
            category = next(iter(fallback_categories))
            source = "jira-type" if any(c["source"] == "jira-type" for c in candidates) else "jira-field"
            # 兜底映射已补全分类证据，保留真实冲突/复合原因而非过期的映射缺口。
            reasons.difference_update({"unmapped_issue_type", "partial_issue_evidence"})
            reasons.update(fallback_reasons - {"unmapped_issue_type", "partial_issue_evidence"})
            reasons.add("jira_category_fallback")
    return {"category": category, "decisionSource": source,
            "ruleIds": sorted({r for c in git for r in c["ruleIds"]}), "reasonCodes": sorted(reasons),
            "jiraKeys": keys, "gitCandidates": git, "jiraCandidates": candidates,
            "needsReview": bool(reasons) or category == "unclassified"}


def needs_git_evidence(decision):
    return not decision["gitCandidates"] or decision["category"] == "unclassified" or bool(set(decision["reasonCodes"]) & {
        "compound_change", "jira_git_conflict", "problem_only", "requires_net_effect_review"})


def manual_decisions(entries, raw, decisions):
    result = {}
    by_hash = {c["hash"]: c for c in raw}
    for entry in entries:
        parts = [decisions[h] for h in dict.fromkeys(entry["commits"])]
        reasons = {r for part in parts for r in part["reasonCodes"]}
        if any(p["category"] not in (entry["category"], "unclassified") for p in parts):
            reasons.add("manual_evidence_conflict")
        keys = issue_keys([by_hash[h] for h in entry["commits"]])
        if set(entry["issues"]) - set(keys):
            reasons.add("manual_extra_issue_reference")
        result[entry["id"]] = {"category": entry["category"], "decisionSource": "manual",
                              "ruleIds": sorted({r for p in parts for r in p["ruleIds"]}),
                              "reasonCodes": sorted(reasons), "jiraKeys": keys,
                              "gitCandidates": [c for p in parts for c in p["gitCandidates"]],
                              "jiraCandidates": [c for p in parts for c in p["jiraCandidates"]],
                              "needsReview": bool(reasons) or entry["category"] == "unclassified"}
    return result


def customer_coverage(raw, content, decisions, important=(), rendered=None, automatic=False, filtered=(), history=()):
    entries = content["entries"]
    r = {c["hash"] for c in raw}
    m = {h for e in entries for h in e["commits"]}
    exclusions = content.get("customerExclusions", [])
    check(isinstance(exclusions, list), "customerExclusions 必须为数组")
    x = set()
    for item in exclusions:
        check(isinstance(item, dict) and set(item) == {"hash", "reason", "reviewer"} and
              isinstance(item["hash"], str) and item["hash"] in r and item["hash"] not in x and
              all(isinstance(item[k], str) and item[k].strip() for k in ("reason", "reviewer")), "客户排除记录非法或重复")
        x.add(item["hash"])
    check(m <= r and not m & x, "客户映射越界或与排除记录冲突")
    # 状态筛选由生成器明确传入，和人工排除、真正遗漏分开核对，不伪造确认人。
    f = set(filtered)
    check(len(f) == len(filtered) and f <= r and not f & (m | x), "状态筛选提交非法或与保留/人工排除冲突")
    h = {row["hash"] for row in history}
    check(len(h) == len(history) and h <= r and not h & (m | x | f) and
          all(row["reasonCode"] in {"clean_merge", "revert_pair"} and row["proof"] and
              isinstance(row["relatedCommits"], list) for row in history), "历史排除提交非法或与保留/其他排除冲突")
    u = r - m - x - f - h
    check(not automatic or not u, "自动客户覆盖存在遗漏")
    ids = [e["id"] for e in entries]
    check(len(set(ids)) == len(ids) and set(ids) == set(decisions), "客户 ID/分类审计不一致")
    if rendered is not None:
        check(set(rendered) == {"releaseNotes", "githubReleaseBody"}, "必须校验两份客户正文")
        for actual in rendered.values():
            check(Counter(actual) == Counter(ids), "客户正文实际渲染 ID 漏项或重复")
    counts = {category: sum(e["category"] == category for e in entries) for category in CATEGORIES}
    regular = {category: sum(e["category"] == category and e["id"] not in important for e in entries) for category in CATEGORIES}
    prefixes = Counter(p for e in entries for p in {i.rsplit("-", 1)[0] for i in e["issues"]})
    stats = {"rawCommitCount": len(r), "customerEntryCount": len(entries), "distinctIssueKeyCount": len(issue_keys(raw)),
             "issuePrefixEntryCounts": dict(sorted(prefixes.items())), "categoryCounts": counts, "regularCategoryCounts": regular,
             "importantEntryCount": len(set(important)), "reviewRequiredEntryCount": sum(d["needsReview"] for d in decisions.values()),
             "jiraFallbackEntryCount": sum(d["decisionSource"] in {"jira-type", "jira-field"} and
                                           "jira_category_fallback" in d["reasonCodes"] for d in decisions.values()),
             "representedCommitCount": len(m), "excludedCommitCount": len(x), "unaccountedCommitCount": len(u),
             "statusFilteredCommitCount": len(f),
             "historyExcludedCommitCount": len(h), "effectiveCommitCount": len(r - h),
             "hiddenMergeCommitCount": sum(row["reasonCode"] == "clean_merge" for row in history),
             "revertedCommitCount": sum(row["reasonCode"] == "revert_pair" for row in history),
             "noIssueCommitCount": sum(not ISSUE.search(c["subject"]) for c in raw)}
    return {"status": "incomplete" if u else "complete", "representedCommits": sorted(m),
            "exclusions": exclusions, "historyExclusions": list(history), "statusFilteredCommits": sorted(f), "unaccountedCommits": sorted(u)}, stats


def snapshot_digest(identity, classification, jira):
    value = {"previousSha": identity["previous"]["commit"], "targetSha": identity["target"]["commit"],
             **{k: classification[k] for k in ("rawCommits", "issueKeys", "ruleVersion", "sourceHashes", "gitEvidence", "historyEvidence")},
             **{k: jira[k] for k in ("configSnapshot", "normalizedSchemaVersion", "results")}}
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()


def _strings(value):
    return isinstance(value, list) and all(isinstance(x, str) for x in value)


def _named(value, label="name"):
    return isinstance(value, dict) and set(value) == {"id", label} and all(isinstance(v, str) for v in value.values()) and bool(value["id"])


def validate_snapshot_report(jira, keys):
    """快照专用校验：不调用在线 loader，不读取凭据、CA 或访问网络。"""
    check(isinstance(jira, dict) and jira.get("mode") in ("live", "snapshot") and
          type(jira.get("normalizedSchemaVersion")) is int and jira["normalizedSchemaVersion"] == 1 and
          set(jira) == {"mode", "status", "normalizedSchemaVersion", "configSnapshot", "results", "counts", "startedAt", "finishedAt", "snapshotDigest", "sourceManifestSha256"}, "snapshot_invalid: Jira 结构")
    check(all(isinstance(jira[k], str) and jira[k] for k in ("startedAt", "finishedAt")), "snapshot_invalid: 查询时间")
    config = jira["configSnapshot"]
    check(isinstance(config, dict) and set(config) == {"schemaVersion", "baseUrl", "deployment", "projects",
          "releaseCategoryField", "releaseCategoryMap", "timeoutSeconds", "totalBudgetSeconds", "maxAttempts"}, "snapshot_invalid: 配置结构")
    check(type(config["schemaVersion"]) is int and config["schemaVersion"] == 1, "snapshot_invalid: 配置版本")
    url = urlsplit(config["baseUrl"])
    check(url.scheme == "https" and url.hostname and url.port != 0 and url.username is None and url.password is None and
          "?" not in config["baseUrl"] and "#" not in config["baseUrl"] and
          not any(p in {".", ".."} for p in unquote(url.path).split("/")) and
          not re.search(r"[\s\\\x00-\x1f\x7f]", config["baseUrl"]), "snapshot_invalid: 实例地址")
    check(config["deployment"] in ("server", "data-center", "cloud"), "snapshot_invalid: 部署类型")
    check(isinstance(config["projects"], dict) and config["projects"], "snapshot_invalid: 项目映射")

    def category_map(value):
        check(isinstance(value, dict) and all(isinstance(k, str) and re.fullmatch(r"[0-9]+", k) and isinstance(v, str) and v in CATEGORIES[:-1] for k, v in value.items()), "snapshot_invalid: 分类映射")

    for key, project in config["projects"].items():
        check(re.fullmatch(r"[A-Z][A-Z0-9_]*", key) and isinstance(project, dict) and
              {"issueTypeMap"} <= set(project) <= {"issueTypeMap", "fallbackIssueTypeMap"}, "snapshot_invalid: 项目")
        category_map(project["issueTypeMap"])
        category_map(project.get("fallbackIssueTypeMap", {}))
    field = config["releaseCategoryField"]
    check(field is None or isinstance(field, str) and re.fullmatch(r"customfield_\d+", field), "snapshot_invalid: 发布分类字段")
    category_map(config["releaseCategoryMap"])
    check(field is not None or not config["releaseCategoryMap"], "snapshot_invalid: 无字段的分类映射")
    for key, low, high in (("timeoutSeconds", 1, 60), ("totalBudgetSeconds", 10, 600), ("maxAttempts", 1, 3)):
        check(type(config[key]) is int and low <= config[key] <= high, "snapshot_invalid: 请求限额")
    results = jira["results"]
    check(isinstance(results, dict) and set(results) == set(keys), "snapshot_invalid: 事项集合")
    for key, result in results.items():
        check(isinstance(result, dict) and set(result) == {"requestedKey", "canonicalKey", "issueId", "status", "reasonCode",
              "httpStatus", "attempts", "fetchedAt", "fields", "warnings", "truncatedFields"}, "snapshot_invalid: 事项结构")
        check(result["requestedKey"] == key and result["status"] in ("fetched", "failed", "skipped") and
              type(result["attempts"]) is int and 0 <= result["attempts"] <= config["maxAttempts"] and
              (result["httpStatus"] is None or type(result["httpStatus"]) is int and 100 <= result["httpStatus"] <= 599) and
              _strings(result["warnings"]) and _strings(result["truncatedFields"]), "snapshot_invalid: 事项状态")
        f = result["fields"]
        if result["status"] != "fetched":
            check(f is None and all(result[k] is None for k in ("canonicalKey", "issueId", "fetchedAt")) and
                  isinstance(result["reasonCode"], str) and result["reasonCode"], "snapshot_invalid: 失败事项")
            continue
        check(result["reasonCode"] is None and result["httpStatus"] == 200 and isinstance(result["fetchedAt"], str) and
              isinstance(result["canonicalKey"], str) and ISSUE.fullmatch(result["canonicalKey"]) and
              isinstance(result["issueId"], str) and result["issueId"], "snapshot_invalid: 成功事项")
        check(isinstance(f, dict) and set(f) - {"assignee"} == {"projectKey", "summary", "issueType", "descriptionText", "components", "fixVersions",
              "labels", "status", "resolution", "updated", "releaseCategory"}, "snapshot_invalid: 归一化字段")
        assignee = f.get("assignee")
        check(assignee is None or (isinstance(assignee, dict) and set(assignee) == {"name", "displayName"} and
              isinstance(assignee["name"], str) and isinstance(assignee["displayName"], str) and
              bool(assignee["displayName"].strip())), "snapshot_invalid: 处理人")
        check(f["projectKey"] == result["canonicalKey"].rsplit("-", 1)[0] and _named(f["issueType"]) and
              isinstance(f["summary"], str) and len(f["summary"].encode()) <= 2048 and isinstance(f["descriptionText"], str) and
              len(f["descriptionText"].encode()) <= 32768 and _strings(f["labels"]) and
              all(isinstance(f[k], list) and all(_named(v) for v in f[k]) for k in ("components", "fixVersions")) and
              all(f[k] is None or _named(f[k]) for k in ("status", "resolution")) and
              (f["updated"] is None or isinstance(f["updated"], str)) and
              (f["releaseCategory"] is None or _named(f["releaseCategory"], "value")), "snapshot_invalid: 字段值")
    counts = {"keys": len(results), **{s: sum(r["status"] == s for r in results.values()) for s in ("fetched", "failed", "skipped")},
              "attempts": sum(r["attempts"] for r in results.values())}
    status = "complete" if counts["fetched"] == counts["keys"] else "partial" if counts["fetched"] else "unavailable"
    check(jira["counts"] == counts and jira.get("status") == status, "snapshot_invalid: Jira 统计")


def replay_snapshot(saved, identity, raw, source_hashes, source_file_hash, history=None):
    try:
        check(isinstance(saved, dict) and saved.get("schemaVersion") == 3 and saved.get("status") == "draft", "snapshot_invalid: 仅接受成功生成的 v3 manifest")
        for endpoint in ("previous", "target"):
            check(saved["identity"][endpoint]["commit"] == identity[endpoint]["commit"], "snapshot_invalid: 端点 SHA 不符")
        c, jira = saved["classification"], saved["jira"]
        check(c["rawCommits"] == raw and c["issueKeys"] == issue_keys(raw), "snapshot_invalid: 原始提交范围不符")
        check(c["ruleVersion"] == RULE_VERSION and c["sourceHashes"] == source_hashes, "snapshot_invalid: 规则或源码指纹变化，需重新在线生成")
        check(isinstance(c["historyEvidence"], dict) and (history is None or c["historyEvidence"] == history),
              "snapshot_invalid: Merge / Revert 历史证据变化，需重新生成")
        validate_snapshot_report(jira, c["issueKeys"])
        evidence = c["gitEvidence"]
        check(isinstance(evidence, dict) and set(evidence) == {r["hash"] for r in raw}, "snapshot_invalid: Git 证据集合")
        for e in evidence.values():
            check(isinstance(e, dict) and set(e) == {"requested", "body", "paths", "truncatedFields", "reasonCode"} and
                  type(e["requested"]) is bool and isinstance(e["body"], str) and len(e["body"].encode()) <= 16384 and
                  _strings(e["paths"]) and len(e["paths"]) <= 200 and _strings(e["truncatedFields"]) and
                  set(e["truncatedFields"]) <= {"body", "paths"} and e["reasonCode"] in (None, "git_evidence_unavailable") and
                  (e["requested"] or e == empty_evidence()), "snapshot_invalid: Git 证据结构")
        check(jira["snapshotDigest"] == snapshot_digest(identity, c, jira), "snapshot_invalid: 摘要校验失败")
        report = copy.deepcopy(jira)
        report.update(mode="snapshot", sourceManifestSha256=source_file_hash)
        return report, copy.deepcopy(evidence)
    except (KeyError, TypeError, ValueError, AttributeError) as error:
        if isinstance(error, ClassificationError):
            raise
        raise ClassificationError("snapshot_invalid: 字段结构错误") from None
