#!/usr/bin/env python3
"""Jira 只读客户端；仅使用标准库，不导入发布流程或安装 Node 依赖。"""
import argparse
import base64
import getpass
import http.client
import json
import os
from pathlib import Path
import re
import ssl
import sys
import time
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from urllib import error, parse, request

SCHEMA_VERSION = 1
MAX_RESPONSE_BYTES = 2 * 1024 * 1024
MAX_DESCRIPTION_BYTES = 32 * 1024
MAX_SUMMARY_BYTES = 2 * 1024
MAX_ADF_NODES = 10000
READ_CHUNK_BYTES = 64 * 1024
MAX_RETRY_DELAY = 10
ISSUE = re.compile(r"[A-Z][A-Z0-9_]*-\d+\Z", re.ASCII)
PROJECT = re.compile(r"[A-Z][A-Z0-9_]*\Z", re.ASCII)
ENV_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z", re.ASCII)
CATEGORIES = {"features", "improvements", "fixes", "adaptations"}
FIELDS = ("summary", "issuetype", "project", "description", "components", "labels",
          "fixVersions", "status", "resolution", "updated", "assignee")
RETRYABLE = {"timeout", "network_error", "rate_limited", "server_error"}


class JiraError(Exception):
    """异常只携带固定错误码/安全说明，禁止拼接请求、密码或服务器正文。"""

    def __init__(self, code, detail=""):
        self.code = code
        super().__init__(code + (": " + detail if detail else ""))


class AuthContext:
    """认证头与实例绑定；不放入配置快照或可序列化的结果中。"""
    __slots__ = ("_base_url", "_header")

    def __init__(self, base_url, header):
        self._base_url, self._header = base_url, header

    def __repr__(self):
        return "<Jira AuthContext: redacted>"

    def apply(self, req, base_url):
        if base_url != self._base_url or not req.full_url.startswith(base_url + "/rest/api/"):
            raise JiraError("auth_origin_mismatch")
        req.add_header("Authorization", self._header)


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # 包括同域跳转，防止认证头跟随 SSO、错误实例或 HTTP 降级。
        return None


def _check(condition, detail):
    if not condition:
        raise JiraError("invalid_config", detail)


def _keys(value, allowed, required=()):
    _check(isinstance(value, dict) and set(value) <= set(allowed) and set(required) <= set(value),
           "配置对象包含未知字段或缺少必要字段")


def _json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _category_map(value):
    _check(isinstance(value, dict), "分类映射必须为对象")
    _check(all(isinstance(k, str) and re.fullmatch(r"[0-9]+", k) and
               isinstance(v, str) and v in CATEGORIES for k, v in value.items()),
           "分类映射必须使用实际数字 ID 和支持的分类")


def _ssl_context(ca_file):
    try:
        context = ssl.create_default_context()
        if ca_file:
            context.load_verify_locations(cafile=ca_file)
        return context
    except (OSError, ValueError):
        raise JiraError("invalid_ca", "无法加载 CA 信任配置") from None


def _validate_config(data, directory):
    _keys(data, {"schemaVersion", "baseUrl", "deployment", "auth", "caFile", "projects",
                 "releaseCategoryField", "releaseCategoryMap", "timeoutSeconds",
                 "totalBudgetSeconds", "maxAttempts"},
          {"schemaVersion", "baseUrl", "deployment", "auth", "projects"})
    _check(type(data["schemaVersion"]) is int and data["schemaVersion"] == SCHEMA_VERSION,
           "schemaVersion 必须为 1")
    base = data["baseUrl"]
    _check(isinstance(base, str) and not re.search(r"[\s\\\x00-\x1f\x7f]", base), "baseUrl 非法")
    try:
        url = parse.urlsplit(base)
        valid = (url.scheme == "https" and url.hostname and url.port != 0 and
                 url.username is None and url.password is None and "?" not in base and "#" not in base and
                 not any(p in {".", ".."} for p in parse.unquote(url.path).split("/")))
    except ValueError:
        valid = False
    _check(valid, "baseUrl 必须为无用户名、参数及片段的 HTTPS API 根地址")
    data["baseUrl"] = base.rstrip("/")
    _check(data["deployment"] in ("server", "data-center", "cloud"), "deployment 不支持")
    projects = data["projects"]
    _check(isinstance(projects, dict) and projects, "projects 不能为空")
    for key, project in projects.items():
        _check(isinstance(key, str) and PROJECT.fullmatch(key), "项目 key 非法")
        _keys(project, {"issueTypeMap", "fallbackIssueTypeMap"}, {"issueTypeMap"})
        _category_map(project["issueTypeMap"])
        _category_map(project.get("fallbackIssueTypeMap", {}))
    category = data.setdefault("releaseCategoryField", None)
    _check(category is None or isinstance(category, str) and
           re.fullmatch(r"customfield_[0-9]+", category), "仅支持单选发布分类字段")
    _category_map(data.setdefault("releaseCategoryMap", {}))
    _check(category is not None or not data["releaseCategoryMap"], "无分类字段时映射必须为空")
    for name, default, low, high in (("timeoutSeconds", 10, 1, 60),
                                    ("totalBudgetSeconds", 120, 10, 600), ("maxAttempts", 2, 1, 3)):
        value = data.setdefault(name, default)
        _check(type(value) is int and low <= value <= high, name + " 超出允许范围")
    ca_file = data.setdefault("caFile", None)
    _check(ca_file is None or isinstance(ca_file, str) and ca_file.strip(), "caFile 必须为路径或 null")
    if ca_file:
        data["caFile"] = str((directory / ca_file).resolve())
    _ssl_context(data["caFile"])
    return data


def _load_auth(config, environ, prompt):
    auth = config["auth"]
    _check(isinstance(auth, dict), "auth 必须为对象")
    mode = auth.get("mode")
    allowed = ("basic-token",) if config["deployment"] == "cloud" else ("bearer", "basic-password")
    _check(mode in allowed, "deployment 与认证方式不匹配")
    secret_name = "passwordEnv" if mode == "basic-password" else "tokenEnv"
    fields = {"mode", secret_name} | ({"userEnv"} if mode != "bearer" else set())
    _keys(auth, fields, fields)
    for key in fields - {"mode"}:
        _check(isinstance(auth[key], str) and ENV_NAME.fullmatch(auth[key]), "认证环境变量名非法")
    user = environ.get(auth["userEnv"], "") if mode != "bearer" else ""
    if mode != "bearer" and not user and prompt:
        user = input("Jira username/email: ").strip()
    secret = environ.get(auth[secret_name], "")
    if not secret and prompt:
        # 非 TTY 时 getpass 可能回显，明确拒绝，CI 使用环境变量。
        if not sys.stdin.isatty():
            raise JiraError("interactive_terminal_required", "非交互终端请使用环境变量")
        secret = getpass.getpass("Jira password/token: ")
    if not secret or not secret.strip() or mode != "bearer" and not user.strip():
        raise JiraError("credentials_missing", "请设置认证环境变量，或使用 --prompt")
    if re.search(r"[\x00-\x1f\x7f]", user + secret) or ":" in user:
        raise JiraError("credentials_invalid")
    if mode == "bearer":
        if not secret.isascii() or re.search(r"\s", secret):
            raise JiraError("credentials_invalid")
        header = "Bearer " + secret
    else:
        header = "Basic " + base64.b64encode((user + ":" + secret).encode("utf-8")).decode("ascii")
    return AuthContext(config["baseUrl"], header)


def load_jira_config(path, *, environ=None, prompt=False):
    """返回 (已校验配置, 私有认证上下文)；相对 CA 路径以配置目录为基准。"""
    try:
        path = Path(path).resolve()
        data = json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=_json_object)
    except (OSError, ValueError, RecursionError):
        raise JiraError("invalid_config", "无法读取有效的 UTF-8 JSON 配置") from None
    config = _validate_config(data, path.parent)
    return config, _load_auth(config, os.environ if environ is None else environ, prompt)


def _clip(text, limit):
    encoded = text.encode("utf-8")
    return encoded[:limit].decode("utf-8", errors="ignore"), len(encoded) > limit


def _description(value, warnings):
    if value is None:
        return "", False
    if isinstance(value, str):
        return _clip(value, MAX_DESCRIPTION_BYTES)
    if not isinstance(value, dict) or value.get("type") != "doc":
        warnings.add("unsupported_description")
        return "", False
    # 迭代遍历避免深层 ADF 递归溢出，保留文本/段落，不取媒体和人员属性。
    stack, pieces, size, nodes = [value], [], 0, 0
    paragraph_end = object()
    containers = {"doc", "paragraph", "heading", "bulletList", "orderedList", "listItem",
                  "blockquote", "codeBlock", "table", "tableRow", "tableCell", "tableHeader", "panel"}
    while stack:
        node = stack.pop()
        nodes += 1
        if nodes > MAX_ADF_NODES:
            warnings.add("adf_node_limit")
            return "".join(pieces).strip(), True
        if node is paragraph_end:
            text = "\n"
        elif isinstance(node, dict):
            kind = node.get("type")
            if kind == "text":
                text = node.get("text", "")
                if not isinstance(text, str):
                    warnings.add("invalid_adf_text")
                    continue
            elif kind == "hardBreak":
                text = "\n"
            else:
                if not isinstance(kind, str) or kind not in containers:
                    warnings.add("unsupported_adf_node")
                    continue
                children = node.get("content", [])
                if not isinstance(children, list):
                    warnings.add("invalid_adf_content")
                    continue
                stack.append(paragraph_end)
                stack.extend(reversed(children))
                continue
        else:
            warnings.add("invalid_adf_node")
            continue
        part, truncated = _clip(text, MAX_DESCRIPTION_BYTES - size)
        pieces.append(part)
        size += len(part.encode("utf-8"))
        if truncated:
            return "".join(pieces).strip(), True
    return "".join(pieces).strip(), False


def _named(value):
    if (not isinstance(value, dict) or not isinstance(value.get("id"), str) or not value["id"] or
            not isinstance(value.get("name"), str)):
        raise ValueError("invalid named field")
    return {"id": value["id"], "name": value["name"]}


def _optional_fields(fields, warnings):
    result = {}
    for key in ("components", "fixVersions", "labels", "status", "resolution", "updated", "assignee"):
        default = [] if key in ("components", "fixVersions", "labels") else None
        value = fields.get(key)
        if key not in fields:
            warnings.add("missing_" + key)
        try:
            if value is None:
                result[key] = default
            elif key in ("components", "fixVersions"):
                if not isinstance(value, list):
                    raise ValueError()
                result[key] = [_named(x) for x in value]
            elif key in ("status", "resolution"):
                result[key] = _named(value)
            elif key == "assignee":
                if not isinstance(value, dict):
                    raise ValueError()
                # Server 使用用户名，Cloud 使用 accountId；只归档处理人名称和标识。
                name = value.get("name") or value.get("accountId") or value.get("key") or ""
                display = value.get("displayName") or name
                if not isinstance(name, str) or not isinstance(display, str) or not display.strip():
                    raise ValueError()
                result[key] = {"name": name.strip(), "displayName": display.strip()}
            elif key == "labels":
                if not isinstance(value, list) or not all(isinstance(x, str) for x in value):
                    raise ValueError()
                result[key] = value
            else:
                if not isinstance(value, str):
                    raise ValueError()
                result[key] = value
        except ValueError:
            result[key] = default
            warnings.add("invalid_" + key)
    return result


def normalize_issue(data, config):
    """只保留方案约定字段；返回 key/ID/归一化字段/告警/截断标志。"""
    try:
        if not isinstance(data, dict):
            raise ValueError()
        key, issue_id, fields = data.get("key"), data.get("id"), data.get("fields")
        if not isinstance(key, str) or not ISSUE.fullmatch(key) or not isinstance(issue_id, str) or not issue_id:
            raise ValueError()
        if not isinstance(fields, dict) or not isinstance(fields.get("project"), dict):
            raise ValueError()
        project = fields["project"].get("key")
        if project != key.rsplit("-", 1)[0] or not isinstance(fields.get("summary"), str):
            raise ValueError()
        issue_type = _named(fields.get("issuetype"))
        warnings, truncated = set(), []
        summary, shortened = _clip(fields["summary"], MAX_SUMMARY_BYTES)
        if shortened:
            truncated.append("summary")
        if "description" not in fields:
            warnings.add("missing_description")
        description, shortened = _description(fields.get("description"), warnings)
        if shortened:
            truncated.append("description")
        if project not in config["projects"]:
            warnings.add("canonical_project_not_configured")
        elif (issue_type["id"] not in config["projects"][project]["issueTypeMap"] and
              issue_type["id"] not in config["projects"][project].get("fallbackIssueTypeMap", {})):
            warnings.add("unmapped_issue_type")
        category, field = None, config["releaseCategoryField"]
        if field and field not in fields:
            warnings.add("category_field_missing")
        elif field and fields[field] is not None:
            value = fields[field]
            if (not isinstance(value, dict) or not isinstance(value.get("id"), str) or not value["id"] or
                    not isinstance(value.get("value"), str)):
                warnings.add("invalid_category_field")
            else:
                category = {"id": value["id"], "value": value["value"]}
                if value["id"] not in config["releaseCategoryMap"]:
                    warnings.add("unmapped_release_category")
        normalized = {"projectKey": project, "summary": summary, "issueType": issue_type,
                      "descriptionText": description, **_optional_fields(fields, warnings),
                      "releaseCategory": category}
        return key, issue_id, normalized, sorted(warnings), sorted(truncated)
    except (ValueError, TypeError, UnicodeError, RecursionError):
        raise JiraError("invalid_response") from None


def _utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _request_issue(opener, req, timeout, deadline):
    """返回数据/HTTP 状态/固定错误码/Retry-After，不向外传递 HTTP 异常。"""
    status = None
    try:
        with opener.open(req, timeout=timeout) as response:
            status = response.status
            if status != 200:
                return None, status, "http_error", None
            content_type = response.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
            if content_type != "application/json" and not (content_type.startswith("application/") and content_type.endswith("+json")):
                return None, status, "non_json_response", None
            chunks, size = [], 0
            while True:
                if time.monotonic() >= deadline:
                    return None, status, "budget_exhausted", None
                chunk = response.read(min(READ_CHUNK_BYTES, MAX_RESPONSE_BYTES + 1 - size))
                if not chunk:
                    break
                chunks.append(chunk)
                size += len(chunk)
                if size > MAX_RESPONSE_BYTES:
                    return None, status, "response_too_large", None
            data = json.loads(b"".join(chunks).decode("utf-8-sig"), object_pairs_hook=_json_object)
            return data, status, None, None
    except error.HTTPError as exc:
        status = exc.code
        retry_after = exc.headers.get("Retry-After") if status in (429, 503) else None
        captcha = exc.headers.get("X-Seraph-LoginReason", "") == "AUTHENTICATION_DENIED"
        exc.close()
        if status == 401 or captcha:
            reason = "auth_failed"
        elif 300 <= status < 400:
            reason = "redirect_rejected"
        else:
            reason = {403: "forbidden", 404: "not_found_or_not_visible", 429: "rate_limited",
                      500: "server_error", 502: "server_error", 503: "server_error", 504: "server_error"}.get(status, "http_error")
        return None, status, reason, retry_after
    except (error.URLError, OSError, http.client.HTTPException) as exc:
        cause = exc.reason if isinstance(exc, error.URLError) else exc
        reason = "tls_error" if isinstance(cause, ssl.SSLError) else "timeout" if isinstance(cause, TimeoutError) else "network_error"
        return None, status, reason, None
    except (ValueError, UnicodeError, RecursionError):
        return None, status, "invalid_response", None


def _retry_delay(value):
    if value is not None:
        try:
            if re.fullmatch(r"[0-9]+", value.strip()):
                return float(value)
            stamp = parsedate_to_datetime(value)
            if stamp.tzinfo is None:
                stamp = stamp.replace(tzinfo=timezone.utc)
            return max(0.0, stamp.timestamp() - time.time())
        except (ValueError, TypeError, OverflowError):
            pass
    return 1.0


def _fetch_one(key, config, auth, opener, deadline):
    result = {"requestedKey": key, "canonicalKey": None, "issueId": None, "status": "failed",
              "reasonCode": None, "httpStatus": None, "attempts": 0, "fetchedAt": None,
              "fields": None, "warnings": [], "truncatedFields": []}
    fields = [*FIELDS, *([config["releaseCategoryField"]] if config["releaseCategoryField"] else [])]
    version = "3" if config["deployment"] == "cloud" else "2"
    url = config["baseUrl"] + "/rest/api/" + version + "/issue/" + key
    url += "?" + parse.urlencode({"fields": ",".join(fields), "updateHistory": "false"})
    req = request.Request(url, headers={"Accept": "application/json", "User-Agent": "ECW-Release-Jira/1"}, method="GET")
    auth.apply(req, config["baseUrl"])
    for attempt in range(config["maxAttempts"]):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            if not result["attempts"]:
                result.update(status="skipped", reasonCode="budget_exhausted")
            break
        result["attempts"] += 1
        data, code, reason, retry_after = _request_issue(opener, req, min(config["timeoutSeconds"], remaining), deadline)
        result.update(httpStatus=code, reasonCode=reason)
        if reason is None:
            try:
                canonical, issue_id, normalized, warnings, truncated = normalize_issue(data, config)
                result.update(status="fetched", canonicalKey=canonical, issueId=issue_id, fields=normalized,
                              warnings=warnings, truncatedFields=truncated, fetchedAt=_utc_now())
            except JiraError as exc:
                result["reasonCode"] = exc.code
            break
        if reason not in RETRYABLE or attempt + 1 >= config["maxAttempts"]:
            break
        delay = _retry_delay(retry_after)
        if delay > MAX_RETRY_DELAY or delay >= deadline - time.monotonic():
            result["reasonCode"] = "rate_limited" if reason == "rate_limited" else "retry_deferred"
            break
        time.sleep(delay)
    return result


def fetch_issues(keys, config, auth_context):
    """按项目白名单串行去重查询，失败保留结果；不分类、不保存文件。"""
    if isinstance(keys, str):
        raise JiraError("invalid_issue_keys", "请提供事项 key 的序列")
    keys = list(keys)
    if not all(isinstance(k, str) and ISSUE.fullmatch(k) for k in keys):
        raise JiraError("invalid_issue_keys")
    keys = sorted(set(keys))
    snapshot = {k: v for k, v in config.items() if k not in ("auth", "caFile")}
    # JSON 往返取得独立快照，避免调用者后续修改配置影响已获取的结果。
    snapshot = json.loads(json.dumps(snapshot, ensure_ascii=False))
    report = {"mode": "live", "status": "complete", "normalizedSchemaVersion": SCHEMA_VERSION,
              "configSnapshot": snapshot, "startedAt": _utc_now(), "finishedAt": None, "results": {}}
    deadline, auth_aborted = time.monotonic() + config["totalBudgetSeconds"], False
    opener = request.build_opener(NoRedirect(), request.HTTPSHandler(context=_ssl_context(config["caFile"])))
    try:
        for key in keys:
            skip = "project_not_configured" if key.rsplit("-", 1)[0] not in config["projects"] else "auth_aborted" if auth_aborted else None
            if skip:
                result = {"requestedKey": key, "canonicalKey": None, "issueId": None, "status": "skipped",
                          "reasonCode": skip, "httpStatus": None, "attempts": 0, "fetchedAt": None,
                          "fields": None, "warnings": [], "truncatedFields": []}
            else:
                result = _fetch_one(key, config, auth_context, opener, deadline)
            report["results"][key] = result
            auth_aborted |= result["reasonCode"] == "auth_failed"
    except JiraError:
        raise
    except Exception:
        raise JiraError("internal_error", "Jira 查询未完成") from None
    counts = {s: sum(r["status"] == s for r in report["results"].values()) for s in ("fetched", "failed", "skipped")}
    report["counts"] = {"keys": len(keys), **counts, "attempts": sum(r["attempts"] for r in report["results"].values())}
    if counts["fetched"] != len(keys):
        report["status"] = "partial" if counts["fetched"] else "unavailable"
    report["finishedAt"] = _utc_now()
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description="只读查询 Jira（独立诊断，不生成 Release Notes）")
    parser.add_argument("--config", required=True, help="Jira JSON 配置路径")
    parser.add_argument("--issue", action="append", required=True, help="事项 key，可重复传入，查询前去重")
    parser.add_argument("--prompt", action="store_true", help="交互输入环境变量未提供的用户名和密码/token")
    args = parser.parse_args(argv)
    try:
        config, auth = load_jira_config(args.config, prompt=args.prompt)
        result = fetch_issues(args.issue, config, auth)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        # 独立诊断非完整取数返回 2；未来主流程自行决定是否降级生成草稿。
        return 0 if result["status"] == "complete" else 2
    except JiraError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print("Jira query interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    # 重定向到文件时仍输出 UTF-8，避免 Windows 本地编码损坏 Jira 文本。
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
