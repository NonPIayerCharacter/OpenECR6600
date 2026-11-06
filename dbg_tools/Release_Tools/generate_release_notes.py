#!/usr/bin/env python3
"""ECW Release Notes: cross-platform orchestration, frozen Git range and release notes output."""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
import uuid
import jira_client
import release_classification as classification
import release_status
import release_project
import release_history

CATEGORIES = {"features": "新增功能", "improvements": "功能优化", "fixes": "问题修复",
              "adaptations": "构建、测试与产品适配变化", "unclassified": "待分类变更"}
FIELDS = ("product", "releaseDate", "scope", "summary", "upgrade", "knownIssues",
          "validation", "artifacts", "rollback")
HASH = re.compile(r"[a-f0-9]{40}(?:[a-f0-9]{24})?\Z")
NPM_VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?")
ISSUE = re.compile(r"\b[A-Z][A-Z0-9_]*-\d+\b")
TOOLS = Path(__file__).absolute().parent
MANIFEST = "generation-manifest.json"


class ReleaseError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ReleaseError(message)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def file_hash(path):
    with Path(path).open("rb") as stream:
        value = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_text(path, text):
    with Path(path).open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


def write_json(path, value):
    write_text(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def now():
    return datetime.now(timezone.utc).isoformat()


def physical_path(value):
    """检查已有父路径, 防止 junction/symlink 将写入重定向到别处。"""
    full = Path(os.path.abspath(value))
    for part in (full, *full.parents):
        require(not part.is_symlink(), f"不允许 symlink/junction: {part}")
        if part.exists():
            require(os.path.normcase(str(part.resolve())) == os.path.normcase(str(part)),
                    f"路径实际落点不一致: {part}")
    return full


def inside(path, root):
    return Path(path).is_relative_to(root)


class WindowsJob:
    """持有本次子进程树, 关闭句柄时由内核终止后代, 不依赖 taskkill 权限。"""
    def __init__(self):
        import ctypes
        from ctypes import wintypes as w

        class Basic(ctypes.Structure):
            _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                        ("flags", w.DWORD), ("min_ws", ctypes.c_size_t), ("max_ws", ctypes.c_size_t),
                        ("active", w.DWORD), ("affinity", ctypes.c_size_t), ("priority", w.DWORD), ("scheduling", w.DWORD)]

        class Extended(ctypes.Structure):
            _fields_ = [("basic", Basic), ("io", ctypes.c_uint64 * 6), ("process_memory", ctypes.c_size_t),
                        ("job_memory", ctypes.c_size_t), ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]

        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]
        self.api.CreateJobObjectW.restype = w.HANDLE
        self.api.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        self.api.SetInformationJobObject.restype = w.BOOL
        self.api.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        self.api.AssignProcessToJobObject.restype = w.BOOL
        self.api.CloseHandle.argtypes = [w.HANDLE]
        self.api.CloseHandle.restype = w.BOOL
        self.handle = self.api.CreateJobObjectW(None, None)
        require(self.handle, f"创建 Windows 作业对象失败: {ctypes.get_last_error()}")
        limits = Extended()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            code = ctypes.get_last_error()
            self.close()
            raise ReleaseError(f"配置 Windows 作业对象失败: {code}")

    def attach(self, process):
        if not self.api.AssignProcessToJobObject(self.handle, int(process._handle)):
            import ctypes
            code = ctypes.get_last_error()
            if process.poll() is not None:
                return
            self.close()
            process.kill()
            process.wait(timeout=10)
            raise ReleaseError(f"绑定 Windows 子进程作业失败: {code}")

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


class Runner:
    def __init__(self, cwd, timeout=120, temp_root=None):
        self.cwd, self.timeout, self.temp_root = Path(cwd), timeout, temp_root
        self.commands = []

    def run(self, args, allowed=(0,), cwd=None, timeout=None, binary=False):
        args = [str(x) for x in args]
        self.commands.append({"args": args, "cwd": str(cwd or self.cwd)})
        # Windows Git 管道存在空读历史, 两端都先捕获到本次临时文件。
        with tempfile.TemporaryDirectory(prefix=".process-", dir=self.temp_root) as tmp:
            out, err = Path(tmp) / "stdout", Path(tmp) / "stderr"
            env = {**os.environ, "GIT_TERMINAL_PROMPT": "0", "LC_ALL": "C",
                   "GIT_NO_REPLACE_OBJECTS": "1"}
            if self.temp_root:
                env["ECW_RELEASE_TMP"] = str(self.temp_root)
            kwargs = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {"start_new_session": True}
            with out.open("wb") as stdout, err.open("wb") as stderr:
                job = WindowsJob() if os.name == "nt" else None
                try:
                    process = subprocess.Popen(args, cwd=cwd or self.cwd, env=env, stdin=subprocess.DEVNULL,
                                               stdout=stdout, stderr=stderr, **kwargs)
                    if job:
                        job.attach(process)
                    try:
                        code = process.wait(timeout=timeout or self.timeout)
                    except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
                        # 只终止本次启动的进程树, 不按进程名清理用户进程。
                        if job:
                            job.close()
                        else:
                            os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=10)
                        if isinstance(error, KeyboardInterrupt):
                            raise
                        raise ReleaseError(f"子进程超时或被中断: {args[0]}")
                finally:
                    if job:
                        job.close()
            require(max(out.stat().st_size, err.stat().st_size) <= 64 * 1024 * 1024,
                    "单次进程输出超过 64 MiB 限额")
            stdout = out.read_bytes() if binary else out.read_text(encoding="utf-8")
            stderr = err.read_text(encoding="utf-8", errors="replace")
            require(code in allowed, f"{args[0]} exited {code}: {stderr.strip()[:4000]}")
            return stdout, code, stderr

    def git(self, *args, allowed=(0,)):
        return self.run(["git", *args], allowed=allowed)


def inventory(root, dependencies=False):
    result = {}
    root = Path(root)
    require(root.is_dir(), f"目录不存在: {root}")
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in dirs:
            physical_path(Path(directory) / name)
        for name in files:
            p = Path(directory) / name
            key = p.relative_to(root).as_posix()
            if p.is_symlink():
                # npm 在 Linux 的 .bin 中创建相对文件链接, 不允许外部目标。
                require(dependencies and p.parent.name == ".bin" and
                        not Path(os.readlink(p)).is_absolute() and p.resolve().is_file() and
                        inside(p.resolve(), root / "node_modules"), f"快照链接不受支持: {p}")
                result[key] = digest(("symlink:" + os.readlink(p)).encode())
            else:
                require(p.is_file(), f"快照包含非普通文件: {p}")
                result[key] = file_hash(p)
    return dict(sorted(result.items()))


def baseline(tools, runner):
    base = read_json(tools / "tool-baseline.json")
    require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", base["snapshotId"]), "工具快照标识非法")
    vendor = physical_path(tools / "vendor/auto-changelog")
    require(inventory(vendor) == base["files"], "维护源码/锁文件发生变化")
    require(digest(compact(base["files"]).encode()) == base["sourceSha256"], "工具基线指纹不一致")
    node = shutil.which("node")
    require(node, "缺少 Node.js, 请安装后确保 node 在 PATH 中")
    # 使用新旧 Node 均支持的内置模块名称, 与项目适配器保持一致。
    probe = ("require('fs');require('os');require('path');require('child_process');"
             "console.log(JSON.stringify({version:process.version,platform:process.platform,arch:process.arch}))")
    try:
        info = json.loads(runner.run([node, "-e", probe])[0])
    except ReleaseError as error:
        raise ReleaseError(f"Node.js 运行能力检查失败, 无法加载 fs/os/path/child_process 内置模块: {error}") from error
    return base, vendor, node, info


def runtime_state(runtime, base):
    items = inventory(physical_path(runtime), dependencies=True)
    source = {k: v for k, v in items.items() if not k.startswith("node_modules/")}
    deps = {k: v for k, v in items.items() if k.startswith("node_modules/")}
    require(source == base["files"], "运行副本源码/锁文件发生变化")
    require(deps, "运行依赖未安装")
    return digest(compact(deps).encode())


def runtime_paths(root, repo, base):
    root = physical_path(root)
    require(not inside(root, repo), "运行依赖目录必须在 SDK 仓库之外")
    parent = physical_path(root / "runtime" / base["snapshotId"])
    return parent, parent / "auto-changelog", parent / "runtime-manifest.json"


def verify_runtime(runtime, receipt_file, base, node_info):
    require(receipt_file.is_file(), "缺少运行回执, 请先使用 --prepare-runtime 准备新的仓库外目录")
    saved = read_json(receipt_file)
    require(saved.get("schemaVersion") == 2 and saved.get("node") == node_info,
            "运行回执平台/版本不匹配或属于 V1.5, 请用新的 --runtime-root 准备 V1.6 环境")
    require(saved.get("sourceSha256") == base["sourceSha256"], "运行快照身份不一致")
    # 生成只依赖已准备的文件; npm 版本用于追溯, 不要求本机再次安装相同 npm。
    require(isinstance(saved.get("npmVersion"), str) and NPM_VERSION.fullmatch(saved["npmVersion"]),
            "运行回执缺少有效 npm 版本, 请用新的 --runtime-root 重新准备")
    deps = runtime_state(runtime, base)
    require(deps == saved.get("dependenciesSha256"), "运行依赖发生变化")
    return saved, deps


def prepare(options, tools=TOOLS):
    tools = physical_path(tools)
    repo = physical_path(tools.parent.parent)
    runner = Runner(repo, options.timeout)
    base, vendor, node, info = baseline(tools, runner)
    parent, runtime, receipt = runtime_paths(options.runtime_root, repo, base)
    if parent.exists():
        verify_runtime(runtime, receipt, base, info)
        return {"runtime": str(runtime), "reused": True}
    npm = shutil.which("npm")
    require(npm, "缺少 npm, 无法准备固定运行依赖")
    # Windows 用 node 执行 npm-cli.js, 避免 cmd/PowerShell 字符串拼接。
    if os.name == "nt":
        npm_cli = Path(npm).parent / "node_modules/npm/bin/npm-cli.js"
        require(npm_cli.is_file(), f"找不到 npm-cli.js: {npm_cli}")
        npm_command = [node, str(npm_cli)]
    else:
        npm_command = [npm]
    npm_version = runner.run([*npm_command, "--version"])[0].strip()
    require(NPM_VERSION.fullmatch(npm_version), f"无法读取有效 npm 版本: {npm_version!r}")
    parent.mkdir(parents=True, exist_ok=False)
    shutil.copytree(vendor, runtime)
    runner.run([*npm_command, "ci", "--omit=dev", "--ignore-scripts", "--package-lock=true",
                "--no-audit", "--no-fund", "--cache", str(parent / "npm-cache")], cwd=runtime, timeout=300)
    require(runner.run([node, runtime / "src/index.js", "--version"])[0].strip() == base["version"], "工具版本不符")
    write_json(receipt, {"schemaVersion": 2, "snapshotId": base["snapshotId"], "sourceSha256": base["sourceSha256"],
                         "dependenciesSha256": runtime_state(runtime, base), "node": info,
                         "npmVersion": npm_version, "preparedAt": now()})
    return {"runtime": str(runtime), "reused": False}


def resolve_ref(runner, ref):
    kind, value = (ref.split(":", 1) if ref.startswith(("tag:", "commit:")) else (None, ref))
    require(value and value != "HEAD", "必须指定 tag 或 commit ID, 不接受 HEAD/分支/revision 表达式")
    tag = commit = None
    if kind != "commit":
        _, valid, _ = runner.git("check-ref-format", "refs/tags/" + value, allowed=(0, 1))
        if valid == 0:
            obj, code, _ = runner.git("rev-parse", "--verify", "refs/tags/" + value, allowed=(0, 128))
            if code == 0:
                sha = runner.git("rev-parse", "--verify", "refs/tags/" + value + "^{commit}")[0].strip()
                tag = {"type": "tag", "tag": value, "object": obj.strip(), "commit": sha}
    if kind != "tag" and re.fullmatch(r"[0-9a-fA-F]{7,64}", value):
        objects = runner.git("rev-parse", "--disambiguate=" + value.lower())[0].splitlines()
        if len(objects) == 1 and runner.git("cat-file", "-t", objects[0])[0].strip() == "commit":
            commit = {"type": "commit", "tag": None, "object": objects[0], "commit": objects[0]}
        elif len(objects) > 1:
            raise ReleaseError("commit ID 有歧义, 请提供完整 SHA")
    require(not (tag and commit), "tag 与 commit ID 有歧义, 请使用 tag: 或 commit: 前缀")
    result = tag or commit
    require(result and HASH.fullmatch(result["commit"]), f"无法解析 tag/commit: {ref}")
    return {"input": ref, **result}


def safe_version(value):
    require(isinstance(value, str) and value.strip(), "版本标识不能为空")
    name = re.sub(r"[^A-Za-z0-9._-]", "_", value)[:96].strip(". ")
    if not name or re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", name):
        name = "version_" + (name or "unnamed")
    return name


def load_config(tools, repo):
    config = read_json(tools / ".auto-changelog")
    allowed = {"template", "remote", "commitLimit", "backfillLimit", "sortCommits", "hideCredit", "hideEmptyReleases",
               "releaseSummary", "autodetectMonorepoDisabled", "issuePattern", "breakingPattern",
               "commitUrl", "issueUrl", "mergeUrl", "compareUrl"}
    require(isinstance(config, dict) and set(config) <= allowed, "配置包含不支持的键, 禁止加载代码/改变范围")
    require(all(config.get(k) is False for k in ("commitLimit", "backfillLimit", "hideEmptyReleases", "releaseSummary")) and
            config.get("autodetectMonorepoDisabled") is True, "配置不符合完整范围约束")
    require(isinstance(config.get("remote"), str) and re.fullmatch(r"[A-Za-z0-9._-]+", config["remote"]), "remote 名称非法")
    require(config.get("template") == "dbg_tools/Release_Tools/templates/engineering.zh.hbs", "模板路径不符")
    for key in ("issuePattern", "breakingPattern"):
        require(isinstance(config.get(key), str) and config[key], f"缺少 {key}")
    for key in ("commitUrl", "issueUrl", "mergeUrl", "compareUrl"):
        if key in config:
            require(isinstance(config[key], str) and re.match(r"https?://[^\r\n]+\Z", config[key]), f"{key} 必须为 HTTP(S) 地址")
    pkg = repo / "package.json"
    require(not pkg.exists() or "auto-changelog" not in read_json(pkg), "目标 package.json 配置有覆盖歧义")
    return config


def validate_entries(data, raw, identity, full=False):
    require(isinstance(data, dict), "人工输入必须为 JSON 对象")
    require(data.get("previousSha") == identity["previous"]["commit"] and
            data.get("targetSha") == identity["target"]["commit"], "人工输入与当前版本 SHA 不匹配")
    require(isinstance(data.get("entries"), list), "人工输入缺少 entries 数组")
    if full:
        require(all(isinstance(data.get(k), str) for k in FIELDS), "客户内容字段必须是文本")
    else:
        require("customerExclusions" not in data, "重要文件不接受 customerExclusions")
    ids, hashes = set(), {x["hash"] for x in raw}
    for e in data["entries"]:
        require(isinstance(e, dict) and isinstance(e.get("id"), str) and re.fullmatch(r"[A-Za-z0-9_-]+", e["id"]) and
                e["id"] not in ids, "客户条目 ID 非法或重复")
        ids.add(e["id"])
        require(e.get("category") in CATEGORIES, "客户条目分类无效")
        text = e.get("text")
        require(isinstance(text, str) and text.strip() and not re.search(r"[\r\n\x00-\x1f]", text), "条目必须为非空单行文本")
        require(not re.search(r"\b[a-f0-9]{7,64}\b|https?://[^\s)]*/(?:commit|commits)/", text, re.I), "客户条目不得含 SHA 或提交链接")
        require(isinstance(e.get("issues"), list) and all(isinstance(x, str) and re.fullmatch(r"[A-Z][A-Z0-9_]*-\d+", x) for x in e["issues"]), "事项编号非法")
        require(isinstance(e.get("commits"), list) and e["commits"] and all(isinstance(x, str) and x in hashes for x in e["commits"]), "条目必须映射到范围内完整 SHA")
        require(isinstance(e.get("validation"), str) and e["validation"].strip(), "每条记录必须提供验证依据或未验证说明")
    return data


def automatic_content(raw, identity, decisions=None):
    """按提交生成待审候选, 不把事项关联当作修复结论或按事项合并不同变更。"""
    entries = []
    for commit in {x["hash"]: x for x in raw}.values():
        subject = " ".join(commit["subject"].split())
        issues = list(dict.fromkeys(ISSUE.findall(subject)))
        # 客户条目不携带提交链接/SHA；原始主题和完整映射保留在内部归档中。
        text = re.sub(r"\[[^\]]*\]\(https?://[^\s)]*/(?:commit|commits)/[^\s)]*\)", "提交链接已省略", subject)
        text = re.sub(r"https?://[^\s)]*/(?:commit|commits)/[^\s)]*", "提交链接已省略", text)
        text = re.sub(r"`?\b[a-f0-9]{7,64}\b`?", "提交标识已省略", text, flags=re.I)
        text = ISSUE.sub("", text)
        text = re.sub(r"\[\s*\]|\(\s*\)|（\s*）|【\s*】|`\s*`", "", text)
        text = " ".join(text.split()).strip(" :：,，;；-") or "提交主题待人工补充"
        decision = decisions[commit["hash"]] if decisions is not None else classification.classify_commit(commit)
        category = decision["category"]
        entries.append({"id": "AUTO_" + commit["hash"], "category": category, "text": text,
                        "issues": issues, "commits": [commit["hash"]],
                        "validation": "自动候选：依据提交及本批可用事项证据分类，客户适用性待确认；未验证。"})
    return {**{k: "" for k in FIELDS}, "previousSha": identity["previous"]["commit"],
            "targetSha": identity["target"]["commit"], "entries": entries,
            "summary": f"本次版本范围包含 {len(entries)} 条 Git 提交。以下为自动整理的候选变更，分类及客户适用性待人工确认。",
            "validation": "自动候选尚未验证，测试结果及交付结论待项目确认。"}


def merge_content(content, important_file, selected, raw, identity, decisions=None):
    require(not (important_file is not None and selected), "--important 与 --important-file 互斥")
    if content is not None:
        validate_entries(content, raw, identity, full=True)
    entries = {e["id"]: e for e in (content or {}).get("entries", [])}
    important = list(dict.fromkeys(selected or []))
    if important_file is not None:
        validate_entries(important_file, raw, identity)
        for e in important_file["entries"]:
            require(e["id"] not in entries or entries[e["id"]] == e, "同 ID 人工条目内容冲突: " + e["id"])
            entries[e["id"]] = e
            important.append(e["id"])
    # 显式人工 content (包括空 entries) 和独立重要说明维持原行为, 不混入自动候选。
    if content is None and not entries:
        content = automatic_content(raw, identity, decisions)
        entries = {e["id"]: e for e in content["entries"]}
    require(all(i in entries for i in important), "重要变更 ID 不存在, 请核对客户条目 ID")
    return {**(content or {}), "entries": list(entries.values())}, important


def select_important(content, raw, jira, selected):
    """仅按范围内提交关联的事项概要识别重要性，人工拆项使用原条目 ID。"""
    marked = {key for key, result in jira["results"].items() if result["status"] == "fetched" and
              "【重要】" in result["fields"]["summary"]}
    by_hash = {row["hash"]: set(ISSUE.findall(row["subject"])) & marked for row in raw}
    important, audit = list(dict.fromkeys(selected)), {}
    for entry in content["entries"]:
        keys = sorted({key for sha in entry["commits"] for key in by_hash[sha]})
        sources = (["manual"] if entry["id"] in selected else []) + (["jira-summary"] if keys else [])
        if sources:
            audit[entry["id"]] = {"sources": sources, "jiraKeys": keys}
            if entry["id"] not in important:
                important.append(entry["id"])
    return important, {"ruleVersion": "jira-summary-important-v1", "marker": "【重要】", "entries": audit}


def default_jira_config(tools=TOOLS):
    local = Path(tools) / "jira-config.local.json"
    return local if local.exists() else Path(tools) / "examples/jira-config.example.json"


def generation_options(options, tools):
    # 不改调用者的 Namespace，也不把网页凭据或回调放进可序列化参数。
    value = argparse.Namespace(**vars(options))
    value.no_jira = getattr(value, "no_jira", False)
    value.jira_status_scope = getattr(value, "jira_status_scope", "all")
    require(value.jira_status_scope in release_status.SCOPES, "JIRA 状态范围无效")
    require(not value.no_jira or value.jira_status_scope == "all", "指定状态筛选需要 JIRA 在线或快照数据")
    require(sum(bool(getattr(value, k, None)) for k in ("jira_config", "jira_snapshot", "no_jira")) <= 1,
            "Jira 在线、快照与 --no-jira 互斥")
    require(not value.jira_prompt or not (value.jira_snapshot or value.no_jira), "--jira-prompt 仅用于在线生成")
    if not (value.jira_config or value.jira_snapshot or value.no_jira):
        value.jira_config = str(default_jira_config(tools))
    return value


def check_jira_complete(jira, stage, config_path=None):
    if jira["mode"] == "disabled":
        return
    issues = [(key, row["reasonCode"] or row["status"]) for key, row in jira["results"].items() if row["status"] != "fetched"]
    issues += [(key, "summary_truncated") for key, row in jira["results"].items() if "summary" in row["truncatedFields"]]
    if issues or jira["status"] != "complete":
        config = jira["configSnapshot"] or {}
        source = "生效 JIRA 配置：" + str(Path(config_path).resolve()) if config_path else "配置来源：JIRA 快照"
        groups = {}
        for key, reason in issues:
            groups.setdefault(reason, []).append(key)
        hints = {
            "project_not_configured": "在生效 JIRA 配置的 projects 中添加缺失项目；--project-config 只控制展示和状态，不配置 JIRA 查询项目。",
            "auth_failed": "检查账号/密码或 token；若触发验证码，请先在 JIRA 页面解除登录限制。",
            "auth_aborted": "此前认证失败，后续查询已停止；先处理 auth_failed。",
            "forbidden": "检查该账号对事项所属项目的浏览权限。",
            "not_found_or_not_visible": "核对提交中的 JIRA 编号，并用同一账号确认该事项存在且可见。",
            "budget_exhausted": "总查询预算已耗尽；检查响应速度，按需调大 totalBudgetSeconds（上限 600 秒）。",
            "timeout": "请求超时；检查网络和 JIRA 响应速度，按需调整 timeoutSeconds 和 totalBudgetSeconds。",
            "network_error": "检查当前机器到 JIRA 的网络、DNS 和代理设置。",
            "tls_error": "检查证书信任链及 caFile 配置。",
            "rate_limited": "JIRA 限流；按服务端要求稍后重试。",
            "retry_deferred": "重试等待超过预算；稍后重试，或核查总查询预算。",
            "server_error": "JIRA 服务端异常；确认服务恢复后重试。",
            "redirect_rejected": "检查 baseUrl 是否为可直接访问的 HTTPS API 根地址。",
            "invalid_response": "接口未返回有效 JIRA JSON；检查地址、登录跳转及代理响应。",
            "summary_truncated": "事项概要被截断，无法完整识别重要标记；核查概要长度。",
        }
        # 仅输出原因码、事项编号和配置位置，不输出认证信息或服务端原始响应。
        summaries = [f"{reason}: {len(keys)} 项（" + "、".join(keys[:3]) + (" 等" if len(keys) > 3 else "") + "）"
                     for reason, keys in sorted(groups.items())]
        missing = sorted({key.rsplit("-", 1)[0] for key in groups.get("project_not_configured", [])})
        fixes = (["缺少项目：" + "、".join(missing)] if missing else [])
        fixes += [hints[reason] for reason in sorted(groups) if reason in hints]
        lines = ["# JIRA 查询未完成", "", "本批未生成客户正文。请处理下列问题后重试，原始提交保存在 raw-commits.txt。", "",
                 source, "已配置项目：" + "、".join(sorted(config.get("projects", {}))),
                 "查询统计：" + compact(jira["counts"]), "",
                 *summaries, "", *fixes, "",
                 "配置修改后请重启网页服务；jira-config.local.json 优先于默认示例，--jira-config 可显式指定。", "",
                 "| 事项 | 原因 | HTTP 状态 | 尝试次数 |", "| --- | --- | --- | --- |"]
        lines += [f"| {key} | {reason} | {jira['results'][key]['httpStatus'] or ''} | {jira['results'][key]['attempts']} |"
                  for key, reason in issues]
        write_text(stage / "coverage-review.md", "\n".join(lines) + "\n")
        raise ReleaseError("JIRA 查询不完整，无法完整分类或识别【重要】。\n" + source + "\n" +
                           "\n".join(summaries + fixes) + "\n详情见失败批次的 coverage-review.md")


def coverage_check(record, raw, reviews, identity):
    require(all(isinstance(record.get(k), list) for k in ("commits", "merges", "fixes")), "工程 JSON 结构异常")
    commits = record["commits"] + [x["commit"] for x in record["merges"] + record["fixes"]]
    hashes, represented = {x["hash"] for x in raw}, {x["hash"] for x in commits}
    require(represented <= hashes, "工程数据包含范围外 SHA")
    missing = hashes - represented
    decisions = []
    if reviews is not None:
        require(isinstance(reviews, dict) and reviews.get("previousSha") == identity["previous"]["commit"] and
                reviews.get("targetSha") == identity["target"]["commit"], "覆盖决定 SHA 不匹配")
        decisions = reviews.get("decisions")
    require(isinstance(decisions, list), "decisions 必须是数组")
    seen = set()
    for e in decisions:
        require(isinstance(e, dict) and isinstance(e.get("hash"), str) and e["hash"] in missing and e["hash"] not in seen,
                "遗漏决定必须唯一对应本次遗漏 SHA")
        require(all(isinstance(e.get(k), str) and e[k].strip() for k in ("reason", "reviewer")), "遗漏决定缺少理由/确认人")
        seen.add(e["hash"])
    return commits, {"missing": sorted(missing), "decisions": decisions, "unresolved": sorted(missing - seen)}


def item_line(entry):
    # 保留既有行内代码, 对 HTML 括号转义, 单行输入不会注入章节。
    text = html.escape(entry["text"].strip().rstrip("。."), quote=False)
    issues = list(dict.fromkeys(entry["issues"]))
    return "- " + text + (" (" + "; ".join("`" + i + "`" for i in issues) + ")." if issues else ".")


def apply_metadata_defaults(content, project=None):
    """空值采用生成机器的本地日期及约定芯片；人工明确填写的值保留。"""
    project = release_project.load() if project is None else project
    defaults = {"releaseDate": datetime.now().astimezone().date().isoformat(), "scope": project["defaultScope"]}
    return {**content, **{key: content.get(key) if (content.get(key) or "").strip() else value
                         for key, value in defaults.items()}}


def render(content, important, identity, version, github=False, statistics=None, status_review=None, selection=None):
    value = lambda k: "" if (content.get(k) or "").strip() in ("", "待填写") else content[k]
    entries = content["entries"]
    by_id = {e["id"]: e for e in entries}
    rendered = []

    def emit(entry):
        line = item_line(entry)
        rendered.append(entry["id"])
        return line
    labels = [x["tag"] or "commit-" + x["commit"][:12] for x in (identity["previous"], identity["target"])]
    product = (content.get("product") or "").strip()
    if product == "待填写":
        product = ""
    title = (html.escape(product, quote=False) + " " if product else "") + html.escape(version, quote=False)
    overview = ["# " + title + ("" if github else " Release Notes"), ""]
    if github:
        overview += ["- 发布日期：" + value("releaseDate"), "", value("summary")]
    else:
        overview += ["- 发布日期：" + value("releaseDate"), "- 适用芯片、板型和配置：" + value("scope"),
                     "- 升级基线：`" + labels[0] + "`", "- 目标版本：`" + labels[1] + "`", "",
                     "## 1. 版本概述", "", value("summary")]
    if statistics is not None:
        counts = statistics["categoryCounts"]
        overview += ["", f"范围内提交 {statistics['rawCommitCount']} 条，客户变更 {statistics['customerEntryCount']} 条，关联编号 {statistics['distinctIssueKeyCount']} 个；" +
                     "、".join(f"{title} {counts[key]} 条" for key, title in CATEGORIES.items()) + "。"]
        overview += ["", f"分类复核：JIRA 兜底 {statistics['jiraFallbackEntryCount']} 条，需人工复核 {statistics['reviewRequiredEntryCount']} 条；已归类不代表已完成内容确认或测试验证。"]
        if statistics.get("historyExcludedCommitCount"):
            overview += [f"历史筛选：隐藏纯 Merge {statistics['hiddenMergeCommitCount']} 条，完整回退抵消 {statistics['revertedCommitCount']} 条；筛选后有效候选提交 {statistics['effectiveCommitCount']} 条。"]
        if statistics["unaccountedCommitCount"]:
            overview += [f"客户内容尚有 {statistics['unaccountedCommitCount']} 条提交待整理。"]
    if status_review is not None:
        counts = status_review["counts"]
        overview += ["", f"关联真实 JIRA {counts['associatedIssueCount']} 项，未提交状态 {counts['otherStatusIssueCount']} 项，状态未知 {counts['unknownStatusIssueCount']} 项。"]
    if selection is not None:
        overview += ["", "发布说明范围：" + selection["label"] + "。"]
        if selection["scope"] == "accepted":
            overview += ["指定状态：" + "、".join(status_review["allowedStatusNames"]) + "。",
                         f"纳入客户条目 {selection['includedEntryCount']} 条，对应提交 {selection['includedCommitCount']} 条；按状态筛选排除条目 {selection['filteredEntryCount']} 条。",
                         "筛选仅影响发布说明，目标代码和完整工程记录仍包含全部合入。"]
            if not entries:
                overview += ["本次范围没有符合指定 JIRA 状态的客户变更。"]
    number = 2
    if important:
        overview += ["", "## Important Changes" if github else "## 2. 重要变更", "", *[emit(by_id[i]) for i in important]]
        number += 1
    if github:
        overview += ["", "适用范围：" + value("scope"), "", "## Release assets", "", value("artifacts"), "",
                     "## Known Issues", "", value("knownIssues"), "", "## Changelog"]
    for category, title in CATEGORIES.items():
        all_entries = [e for e in entries if e["category"] == category]
        if category == "unclassified" and not all_entries:
            continue
        regular = [e for e in all_entries if e["id"] not in important]
        overview += ["", "### " + title if github else f"## {number}. {title}", "", *[emit(e) for e in regular]]
        number += 1
    if github:
        overview += ["", "## Upgrade and validation", "", value("upgrade"), "", value("validation"), "", value("rollback"), "",
                     "## Source and release traceability", "", "- Base ref: `" + labels[0] + "`", "- Target ref: `" + labels[1] + "`"]
    else:
        for key, title in (("upgrade", "不兼容变更与升级说明"), ("knownIssues", "已知问题与限制"), ("validation", "验证与交付信息"),
                           ("artifacts", "交付物及 SHA-256"), ("rollback", "回退与支持")):
            overview += ["", f"## {number}. {title}", "", value(key)]
            number += 1
        overview += ["", f"## {number}. 版本追溯", ""]
    overview += ["- 基线类型：" + identity["previous"]["type"], "- 目标类型：" + identity["target"]["type"],
                 "- 基线提交：`" + identity["previous"]["commit"] + "`", "- 目标提交：`" + identity["target"]["commit"] + "`", ""]
    return "\n".join(overview), rendered


def coverage_markdown(raw, coverage, content, important, identity, audit=None, jira=None,
                      customer_coverage=None, statistics=None, history=None):
    cell = lambda s: html.escape(str(s), quote=False).replace("|", "&#124;").replace("\n", " ").replace("\r", " ")
    output = ["# 内部覆盖核对", "", "范围：" + identity["previous"]["commit"] + ".." + identity["target"]["commit"]]
    if history is not None:
        output += ["", "## Merge / Revert 历史分析", "", "仅客户正文隐藏已证明的流程记录和完整抵消；工程记录保留全部历史。",
                   "", "| SHA / 原始主题 | 处理 | 关联提交 | 证据 | 未排除的具体原因 |", "| --- | --- | --- | --- | --- |"]
        subjects = {row["hash"]: row["subject"] for row in raw}
        for sha, record in history["commits"].items():
            if record["reasonCode"]:
                output.append("| " + " | ".join(map(cell, [sha + " / " + subjects[sha], release_history.LABELS[record["reasonCode"]],
                              ", ".join(record["relatedCommits"]), release_history.PROOF_LABELS.get(record["proof"], "保留复核"),
                              release_history.DETAIL_LABELS.get(record.get("detailCode"), "—")])) + " |")
    if audit is not None:
        review_labels = {"problem_only": "问题描述待核实并改写为具体变更", "insufficient_change": "变更内容不明确，需补充说明",
                         "compound_change": "复合变更，需确认是否拆项", "multiple_actions_review": "多个动作，需确认是否拆项",
                         "jira_git_conflict": "Git 与 JIRA 分类冲突", "jira_category_fallback": "采用 JIRA 兜底，需确认分类",
                         "requires_net_effect_review": "合并/回退的实际效果待确认", "unmapped_issue_type": "事项类型未映射",
                         "partial_issue_evidence": "部分事项分类依据不足"}
        output += ["", "## 优先复核", "", "以下条目即使已有分类，仍需确认；同一条目可包含多个原因。",
                   "", "| 客户条目 / 事项 | 当前分类 | 复核原因 |", "| --- | --- | --- |"]
        for entry in content["entries"]:
            decision = audit[entry["id"]]
            if decision["needsReview"]:
                reasons = ["自动候选曾按 JIRA 兜底，最终以人工分类为准" if r == "jira_category_fallback" and decision["decisionSource"] == "manual"
                           else review_labels.get(r, r) for r in decision["reasonCodes"]]
                output.append("| " + " | ".join(map(cell, [entry["text"] + " / " + ", ".join(entry["issues"]),
                              CATEGORIES[entry["category"]], "；".join(reasons) or "分类待确认"])) + " |")
        if not any(d["needsReview"] for d in audit.values()):
            output += ["| 无规则复核项（客户适用性及测试结论仍需确认） | — | — |"]
    output += ["", "## 提交逐条覆盖", "",
               "| 完整 SHA | 原始主题 | 工程覆盖 / 遗漏处理 | 客户条目 / 原分类 / 展示位置 | 逐项验证 |", "| --- | --- | --- | --- | --- |"]
    decisions = {x["hash"]: x for x in coverage["decisions"]}
    status_filtered = set((customer_coverage or {}).get("statusFilteredCommits", []))
    history_excluded = {row["hash"]: release_history.LABELS[row["reasonCode"]]
                        for row in (customer_coverage or {}).get("historyExclusions", [])}
    for commit in raw:
        sha = commit["hash"]
        decision = decisions.get(sha)
        status = "已显示" if sha not in coverage["missing"] else (decision["reason"] + "（" + decision["reviewer"] + "）" if decision else "未解释遗漏，阻止生成成功")
        linked = [e for e in content["entries"] if sha in e["commits"]]
        rows = [e["id"] + ": " + ", ".join(e["issues"]) + " / " + CATEGORIES[e["category"]] + " / " +
                ("重要变更" if e["id"] in important else CATEGORIES[e["category"]]) for e in linked]
        missing_customer = history_excluded.get(sha) or ("按 JIRA 状态筛选排除" if sha in status_filtered else "待人工映射/说明排除")
        output.append("| " + " | ".join(map(cell, [sha, commit["subject"], status, "; ".join(rows) or missing_customer,
                                                       "; ".join(e["id"] + ": " + e["validation"] for e in linked) or "未验证"])) + " |")
    if statistics is not None:
        output += ["", "## 客户覆盖与数量口径", "", "| 指标 | 数量 |", "| --- | --- |"]
        for key, value in statistics.items():
            output.append(f"| {key} | {cell(compact(value) if isinstance(value, dict) else value)} |")
        raw_prefixes = {}
        for commit in raw:
            for prefix in {key.rsplit("-", 1)[0] for key in ISSUE.findall(commit["subject"])}:
                raw_prefixes[prefix] = raw_prefixes.get(prefix, 0) + 1
        output += ["", "原始提交前缀关联数：" + cell(compact(raw_prefixes)),
                   "", "客户覆盖状态：" + customer_coverage["status"],
                   "", "未说明提交：" + (", ".join(customer_coverage["unaccountedCommits"]) or "无"),
                   "", "按状态筛选排除的提交：" + (", ".join(customer_coverage.get("statusFilteredCommits", [])) or "无"),
                   "", "| 客户排除 SHA | 原因 | 确认人 |", "| --- | --- | --- |"]
        for e in customer_coverage["exclusions"]:
            output.append("| " + " | ".join(cell(e[k]) for k in ("hash", "reason", "reviewer")) + " |")
    if audit is not None:
        output += ["", "## 分类依据与待复核清单", "", "按每笔提交的实际变化分类；需求、任务和子任务类型只在无法明确分类时兜底。同一 JIRA 的不同提交可以进入不同类别。",
                   "", "| 客户条目 / JIRA / ID | 分类 / 依据来源 | 命中规则 | 复核原因 | 提交证据 | JIRA 候选 | 需复核 |", "| --- | --- | --- | --- | --- | --- | --- |"]
        by_id = {entry["id"]: entry for entry in content["entries"]}
        for key, decision in audit.items():
            entry = by_id[key]
            source = classification.SOURCE_LABELS.get(decision["decisionSource"], decision["decisionSource"])
            if "jira_category_fallback" in decision["reasonCodes"] and decision["decisionSource"] in {"jira-type", "jira-field"}:
                source += "兜底"
            reasons = [review_labels.get(reason, reason) for reason in decision["reasonCodes"]]
            for sha in entry["commits"]:
                record = (history or {}).get("commits", {}).get(sha, {})
                if record.get("detailCode"):
                    reasons.append(release_history.DETAIL_LABELS[record["detailCode"]])
            rules = [rule + "：" + classification.RULE_LABELS.get(rule, rule) for rule in decision.get("ruleIds", [])]
            output.append("| " + " | ".join(map(cell, [entry["text"] + " / " + ", ".join(entry["issues"]) + " / " + key,
                          CATEGORIES[decision["category"]] + " / " + source, "；".join(rules) or "无提交规则命中",
                          "；".join(dict.fromkeys(reasons)) or "无", compact(decision["gitCandidates"]),
                          compact(decision["jiraCandidates"]), decision["needsReview"]])) + " |")
    if jira is not None:
        output += ["", "## Jira 取数与类型映射", "", "模式 / 状态：" + jira["mode"] + " / " + jira["status"],
                   "", "取数统计：" + cell(compact(jira["counts"])),
                   "", "| 原始 key / 实际 key | 状态 / 原因 | 类型 ID / 名称 | 类型映射 / 发布字段 ID 和值 | 告警 |", "| --- | --- | --- | --- | --- |"]
        for key, result in jira["results"].items():
            fields = result["fields"] or {}
            kind = fields.get("issueType", {})
            project = (jira["configSnapshot"] or {}).get("projects", {}).get(fields.get("projectKey"), {})
            mapping = project.get("issueTypeMap", {}).get(kind.get("id"))
            if mapping is None and kind.get("id") in project.get("fallbackIssueTypeMap", {}):
                mapping = "仅待分类兜底: " + project["fallbackIssueTypeMap"][kind["id"]]
            output.append("| " + " | ".join(map(cell, [key + " / " + str(result["canonicalKey"]),
                          result["status"] + " / " + str(result["reasonCode"]), compact(kind),
                          (mapping or "未映射") + " / " + compact(fields.get("releaseCategory")), ", ".join(result["warnings"])])) + " |")
    return "\n".join(output) + "\n"


def read_git_evidence(runner, sha):
    evidence = {**classification.empty_evidence(), "requested": True}
    try:
        body = runner.git("show", "-s", "--format=%b", sha, "--")[0].encode("utf-8")
        evidence["body"] = body[:16384].decode("utf-8", errors="ignore").strip()
        if len(body) > 16384:
            evidence["truncatedFields"].append("body")
        paths = runner.git("diff-tree", "--root", "--no-commit-id", "--name-only", "-r", "-z", sha, "--")[0].split("\0")
        paths = [p for p in paths if p]
        evidence["paths"] = paths[:200]
        if len(paths) > 200:
            evidence["truncatedFields"].append("paths")
    except (OSError, UnicodeError, ReleaseError):
        # Git 异常仅影响补充证据；不以原始 stderr 充当客户变更说明。
        evidence["reasonCode"] = "git_evidence_unavailable"
    return evidence


def generate(options, tools=TOOLS, *, jira_loader=None, progress=None):
    tools = physical_path(tools)
    options = generation_options(options, tools)
    notify = progress or (lambda stage: None)
    repo = physical_path(tools.parent.parent)
    root = physical_path(tools / "release")
    root.mkdir(exist_ok=True)
    batch_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:12]
    stage = root / (".pending_" + batch_id)
    stage.mkdir()
    runner = Runner(repo, options.timeout, stage)
    manifest = {"schemaVersion": 3, "status": "running", "customerApproved": False, "startedAt": now(),
                "repository": str(repo), "batchId": batch_id, "pythonVersion": platform.python_version(),
                "platform": platform.platform(), "arguments": vars(options).copy(), "commands": runner.commands}
    started = time.monotonic()
    version_name, failure = "failed", None
    temporary = stage / ".engine-input.json"
    allowed = {"CHANGELOG.generated.md", "release-data.json", "raw-commits.txt", MANIFEST, "coverage-review.md"}
    try:
        notify("检查运行环境")
        base, _, node, info = baseline(tools, runner)
        _, runtime, receipt_file = runtime_paths(options.runtime_root, repo, base)
        receipt, deps = verify_runtime(runtime, receipt_file, base, info)
        config = load_config(tools, repo)
        project_path = physical_path(options.project_config or tools / "release-project.json")
        watched = [tools / ".auto-changelog", tools / "templates/engineering.zh.hbs", tools / "tool-baseline.json", receipt_file,
                   tools / "generate_release_notes.py", tools / "jira_client.py", tools / "release_classification.py", tools / "release_status.py",
                   tools / "release_project.py", tools / "release_history.py", project_path,
                   tools / "src/run-auto-changelog.js", tools / "src/process.js", repo / "package.json"]
        snapshots = {}
        for key in ("content", "coverage", "important_file", "jira_config", "jira_snapshot"):
            path = getattr(options, key)
            if path:
                path = physical_path(path)
                watched.append(path)
        fingerprints = {str(p): file_hash(p) if p.exists() else None for p in watched}
        # 网页启动时冻结配置；生成前已变更也必须报错，避免页面与正文口径不同。
        expected_project_hash = getattr(options, "project_config_sha256", None)
        require(expected_project_hash is None or fingerprints[str(project_path)] == expected_project_hash,
                "项目配置已变更，请重启网页服务")
        project = release_project.load(project_path)
        manifest["projectConfig"] = project
        jira_config = auth = None
        if options.jira_config:
            try:
                jira_config, auth = (jira_loader or jira_client.load_jira_config)(options.jira_config, prompt=options.jira_prompt)
            except jira_client.JiraError as error:
                raise ReleaseError(str(error)) from None
            except Exception:
                raise ReleaseError("jira_preflight_error: Jira 配置加载失败") from None
            if jira_config["caFile"]:
                ca = physical_path(jira_config["caFile"])
                watched.append(ca)
                fingerprints[str(ca)] = file_hash(ca)
        for key in ("content", "coverage", "important_file"):
            path = getattr(options, key)
            snapshots[key] = read_json(path) if path else None
        manifest.update({"snapshotId": base["snapshotId"], "sourceSha256": base["sourceSha256"], "dependenciesSha256": deps,
                         "node": info, "npmVersion": receipt["npmVersion"], "toolVersion": base["version"],
                         "inputSha256": fingerprints, "inputs": snapshots, "config": config})
        require(Path(runner.git("rev-parse", "--show-toplevel")[0].strip()).resolve() == repo, "脚本所在目录不是目标 Git 根目录")
        require(runner.git("rev-parse", "--is-shallow-repository")[0].strip() == "false", "浅克隆不支持生成")
        notify("读取提交范围")
        identity = {"previous": resolve_ref(runner, options.from_ref), "target": resolve_ref(runner, options.to_ref)}
        runner.git("merge-base", "--is-ancestor", identity["previous"]["commit"], identity["target"]["commit"])
        diff = identity["previous"]["commit"] + ".." + identity["target"]["commit"]
        raw = []
        for line in runner.git("log", "--format=%H%x09%s", diff, "--")[0].splitlines():
            sha, separator, subject = line.partition("\t")
            require(separator and HASH.fullmatch(sha), "原始 Git 提交读取异常")
            raw.append({"hash": sha, "subject": subject})
        require(raw, "范围为空, 不生成成功批次")
        require(sorted(x["hash"] for x in raw) == sorted(runner.git("rev-list", diff, "--")[0].splitlines()), "原始范围清单不一致")
        version = options.version if options.version is not None else identity["target"]["tag"] or "commit-" + identity["target"]["commit"][:12]
        require(not re.search(r"[\r\n\x00-\x1f]", version), "版本标识不能含控制字符")
        version_name = safe_version(version)
        release_notes_filename = f"Release_Notes_{version_name}" + ("_accepted" if options.jira_status_scope == "accepted" else "") + ".md"
        status_filename = f"JIRA_Status_Review_{version_name}.md"
        allowed.update({release_notes_filename, f"github-release-body_{version_name}.md", status_filename})
        manifest.update({"identity": identity, "range": diff, "version": version, "fileVersion": version_name,
                         "gitVersion": runner.git("--version")[0].strip(), "rawCount": len(raw)})
        write_text(stage / "raw-commits.txt", "".join(x["hash"] + "\t" + x["subject"] + "\n" for x in raw))
        for key, full in (("content", True), ("important_file", False)):
            if snapshots[key] is not None:
                validate_entries(snapshots[key], raw, identity, full=full)
        source_hashes = {name: fingerprints[str(tools / name)] for name in
                         ("generate_release_notes.py", "jira_client.py", "release_classification.py", "release_status.py", "release_project.py", "release_history.py")}
        source_hashes["projectConfig"] = digest(compact(project).encode("utf-8"))
        notify("分析 Merge / Revert 历史")
        history = release_history.analyze(runner, raw, identity["previous"]["commit"], identity["target"]["commit"])
        manifest["historyAnalysis"] = history
        hidden_hashes = {row["hash"] for row in history["exclusions"]}
        history_review = {row["hash"] for row in history["review"]}
        keys = classification.issue_keys(raw)
        jira, evidence = classification.disabled_jira(), {}
        notify("读取 JIRA 快照" if options.jira_snapshot else "查询 JIRA" if jira_config else "本地诊断分类")
        if options.jira_snapshot:
            source = physical_path(options.jira_snapshot)
            try:
                jira, evidence = classification.replay_snapshot(read_json(source), identity, raw, source_hashes, fingerprints[str(source)], history)
            except (ValueError, OSError) as error:
                raise ReleaseError("snapshot_invalid: 请检查快照版本、范围及规则。" +
                                   (str(error) if isinstance(error, classification.ClassificationError) else "")) from None
        elif jira_config is not None:
            try:
                jira = jira_client.fetch_issues(keys, jira_config, auth)
                jira.update(snapshotDigest=None, sourceManifestSha256=None)
                classification.validate_snapshot_report(jira, keys)
            except jira_client.JiraError as error:
                raise ReleaseError(str(error)) from None
            except Exception:
                raise ReleaseError("jira_integration_error: Jira 取数或归一化契约异常") from None
            finally:
                auth = None
        manifest["jira"] = jira
        check_jira_complete(jira, stage, options.jira_config)
        notify("分类与识别重要变更")
        decisions = {}
        for commit in raw:
            sha = commit["hash"]
            if not options.jira_snapshot:
                preliminary = classification.classify_commit(commit, jira=jira)
                evidence[sha] = read_git_evidence(runner, sha) if sha not in hidden_hashes and classification.needs_git_evidence(preliminary) else classification.empty_evidence()
            decisions[sha] = classification.classify_commit(commit, evidence[sha], jira)
            if sha in history_review:
                decisions[sha].update(category="unclassified", decisionSource="history-review", needsReview=True)
                if "requires_net_effect_review" not in decisions[sha]["reasonCodes"]:
                    decisions[sha]["reasonCodes"].append("requires_net_effect_review")
        content, important = merge_content(snapshots["content"], snapshots["important_file"], options.important, raw, identity, decisions)
        automatic = snapshots["content"] is None and not (snapshots["important_file"] or {}).get("entries")
        original_audit = ({"AUTO_" + sha: d for sha, d in decisions.items()} if automatic else
                          classification.manual_decisions(content["entries"], raw, decisions))
        # 先验证原人工映射，避免状态筛选隐藏条目与人工排除之间的冲突。
        classification.customer_coverage(raw, content, original_audit, important, automatic=automatic)
        content, removed_ids = release_history.select_content(content, history)
        manifest["historyRemovedEntryIds"] = removed_ids
        important = [key for key in important if key not in removed_ids]
        status_review = release_status.build_review(raw, jira, project["acceptedJiraStatuses"])
        content, selection = release_status.select_content(content, status_review, options.jira_status_scope)
        retained_ids = {e["id"] for e in content["entries"]}
        important = [key for key in important if key in retained_ids]
        important, manifest["importance"] = select_important(content, raw, jira, important)
        # 一批只取一次本地日期，保证两份正文在跨午夜生成时仍一致；输入快照不改写。
        content = apply_metadata_defaults(content, project)
        manifest["customerMetadata"] = {key: content[key] for key in ("releaseDate", "scope")}
        if automatic:
            validate_entries(content, raw, identity, full=True)
        audit = {key: decision for key, decision in original_audit.items() if key in retained_ids}
        customer_cov, statistics = classification.customer_coverage(raw, content, audit, important, automatic=automatic,
                                                                    filtered=selection["filteredCommits"], history=history["exclusions"])
        manifest.update(jiraStatusReview=status_review, statusSelection=selection)
        write_text(stage / status_filename, release_status.markdown(status_review, selection, identity, version))
        classification_data = {"ruleVersion": classification.RULE_VERSION, "sourceHashes": source_hashes,
                               "rawCommits": raw, "issueKeys": keys, "gitEvidence": evidence, "historyEvidence": history, "entries": audit}
        if jira["mode"] != "disabled":
            jira["snapshotDigest"] = classification.snapshot_digest(identity, classification_data, jira)
        manifest.update(jira=jira, classification=classification_data, customerCoverage=customer_cov, customerStatistics=statistics)
        date = runner.git("log", "-1", "--format=%cI", identity["target"]["commit"])[0].strip()
        if identity["target"]["tag"]:
            ref = "refs/tags/" + identity["target"]["tag"]
            dates = runner.git("for-each-ref", "--format=%(refname)%09%(creatordate:iso-strict)", ref)[0].splitlines()
            date = next(row.split("\t", 1)[1] for row in dates if row.split("\t", 1)[0] == ref)
        notify("生成工程记录")
        preserve_hashes = sorted({sha for sha, row in history["commits"].items() if row["reasonCode"]} |
                                 ({h for row in history["commits"].values() for h in row["relatedCommits"]} & {r["hash"] for r in raw}))
        write_json(temporary, {"runtime": str(runtime), "timeout": options.timeout * 1000, "output": str(stage),
                               "preserveHashes": preserve_hashes,
                               "template": str(tools / "templates/engineering.zh.hbs"), "config": config,
                               "release": {"tag": identity["target"]["tag"], "title": version, "diff": diff,
                                           "isoDate": date[:10], "from": identity["previous"]["commit"], "to": identity["target"]["commit"]}})
        stdout, _, stderr = runner.run([node, tools / "src/run-auto-changelog.js", temporary])
        if stdout.strip():
            print(stdout.strip())
        if stderr.strip():
            print(stderr.strip(), file=sys.stderr)
        temporary.unlink()
        data = read_json(stage / "release-data.json")
        require(isinstance(data, list) and len(data) == 1 and data[0].get("diff") == diff and
                data[0].get("tag") == identity["target"]["tag"], "工程 JSON 范围/目标不符")
        commits, coverage = coverage_check(data[0], raw, snapshots["coverage"], identity)
        manifest.update({"coverage": coverage, "importantIds": important,
                         "customerContentSource": "git-subjects" if snapshots["content"] is None and
                         not (snapshots["important_file"] or {}).get("entries") else "manual",
                         "customerEntries": [{**e, "displaySection": "重要变更" if e["id"] in important else CATEGORIES[e["category"]]} for e in content["entries"]]})
        write_text(stage / "coverage-review.md", coverage_markdown(raw, coverage, content, important, identity, audit, jira, customer_cov, statistics, history))
        require(not coverage["unresolved"], "存在未解释遗漏 SHA, 请提供 --coverage 后重跑")
        markdown = (stage / "CHANGELOG.generated.md").read_text(encoding="utf-8")
        require(markdown.strip(), "工程 Markdown 为空")
        for commit in commits:
            require(commit["hash"] in markdown, "模板遗漏完整提交 SHA")
            if commit.get("breaking"):
                require(any(commit["hash"] in line and "不兼容变更" in line for line in markdown.splitlines()), "模板遗漏不兼容变更提示")
        permitted = {x["hash"] for x in raw} | {identity["previous"]["commit"], identity["target"]["commit"]}
        require(set(re.findall(r"\b[a-f0-9]{40}(?:[a-f0-9]{24})?\b", markdown)) <= permitted, "工程 Markdown 含范围外 SHA")
        rendered = {}
        notify("校验并保存文档")
        for name, filename, github in (("releaseNotes", release_notes_filename, False),
                                       ("githubReleaseBody", f"github-release-body_{version_name}.md", True)):
            text, rendered[name] = render(content, important, identity, version, github=github, statistics=statistics,
                                          status_review=status_review, selection=selection)
            write_text(stage / filename, text)
        classification.customer_coverage(raw, content, audit, important, rendered, automatic,
                                         filtered=selection["filteredCommits"], history=history["exclusions"])
        manifest["renderedIds"] = rendered
        require(resolve_ref(runner, options.from_ref) == identity["previous"] and resolve_ref(runner, options.to_ref) == identity["target"], "生成期间引用发生变化")
        require({str(p): file_hash(p) if p.exists() else None for p in watched} == fingerprints, "生成期间配置/模板/人工输入发生变化")
        baseline(tools, runner)
        require(runtime_state(runtime, base) == deps, "生成期间工具/依赖发生变化")
        require({p.name for p in stage.iterdir()} == allowed - {MANIFEST}, "结果文件清单不符合八文件约束")
        manifest["checks"] = {"range": True, "coverage": True, "markdown": True, "inputsUnchanged": True,
                              "rawCount": len(raw), "representedCount": len({x['hash'] for x in commits}),
                              "reviewedOmissions": len(coverage["decisions"]), "customerApproved": False,
                              "customerMappingsValid": True, "customerCoverageComplete": customer_cov["status"] == "complete",
                              "customerRenderIdsValid": True}
        manifest["status"] = "draft"
    except (Exception, KeyboardInterrupt) as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)[:4000] or "用户中断"
        failure = error
    finally:
        if temporary.exists():
            temporary.unlink()
        manifest["finishedAt"] = now()
        manifest["elapsedMs"] = round((time.monotonic() - started) * 1000)
        manifest["artifacts"] = {p.name: file_hash(p) for p in stage.iterdir() if p.is_file() and p.name in allowed - {MANIFEST}}
        destination = root / (version_name + "_" + batch_id)
        manifest["batch"] = str(destination)
        write_json(stage / MANIFEST, manifest)
        require(not destination.exists(), "批次目录已存在, 禁止覆盖")
        physical_path(stage)
        stage.rename(destination)
    if failure:
        error = ReleaseError(manifest["error"] + "\n失败批次: " + str(destination))
        error.batch = destination
        raise error from failure
    return {"batch": str(destination), "rawCount": manifest["rawCount"], "status": "draft",
            "historyStatistics": {key: statistics[key] for key in ("hiddenMergeCommitCount", "revertedCommitCount", "effectiveCommitCount")},
            "customerEntryCount": statistics["customerEntryCount"], "distinctIssueKeyCount": statistics["distinctIssueKeyCount"],
            "jira": {"mode": jira["mode"], "status": jira["status"], **jira["counts"]},
            "unclassifiedCount": statistics["categoryCounts"]["unclassified"], "unaccountedCommitCount": statistics["unaccountedCommitCount"],
            "jiraFallbackEntryCount": statistics["jiraFallbackEntryCount"], "reviewRequiredEntryCount": statistics["reviewRequiredEntryCount"],
            "releaseDate": content["releaseDate"], "scope": content["scope"],
            "importantEntryCount": len(important), "noIssueCommitCount": statistics["noIssueCommitCount"],
            "autoImportantEntryCount": sum("jira-summary" in row["sources"] for row in manifest["importance"]["entries"].values()),
            "jiraStatusReview": status_review, "statusSelection": selection, "jiraStatusReport": str(destination / status_filename),
            "coverageReview": str(destination / "coverage-review.md")}


def parser():
    p = argparse.ArgumentParser(description="生成 tag/commit 的 A..B Release Notes(不含 A)。成功批次在 Release_Tools/release/<版本>_<批次>, 共八文件。依赖 Python 3.10+、Git、Node.js; 首次准备需要 npm。")
    p.add_argument("--from", dest="from_ref", help="基线 tag/commit ID; 可用 tag: 或 commit: 前缀")
    p.add_argument("--to", dest="to_ref", help="目标 tag/commit ID")
    p.add_argument("--version", help="显示版本, 不改变 Git 范围")
    p.add_argument("--project-config", help="项目展示/状态配置；默认工具目录 release-project.json")
    p.add_argument("--runtime-root", default=str(Path.home() / ".ecw-release-tools"), help="仓库外运行依赖根目录")
    p.add_argument("--prepare-runtime", action="store_true", help="准备固定运行依赖后退出")
    p.add_argument("--content", help="人工客户内容 JSON; 未提供人工输入时从提交主题生成待审候选")
    p.add_argument("--coverage", help="遗漏提交确认 JSON")
    jira_group = p.add_mutually_exclusive_group()
    jira_group.add_argument("--jira-config", help="Jira 配置 JSON；默认工具目录 jira-config.local.json，其次 examples/jira-config.example.json")
    jira_group.add_argument("--jira-snapshot", help="既有 v3 manifest；同范围/规则完全离线重放")
    jira_group.add_argument("--no-jira", action="store_true", help="仅用于显式本地诊断，不查询 Jira、不自动识别重要事项")
    p.add_argument("--jira-prompt", action="store_true", help="在线生成时交互输入缺失的账号/密码，配置路径可省略")
    p.add_argument("--jira-status-scope", choices=list(release_status.SCOPES), default="all", help="all=全部记录(默认)；accepted=仅项目配置指定的 JIRA 状态关联记录")
    group = p.add_mutually_exclusive_group()
    group.add_argument("--important", action="append", default=[], help="提升人工或自动候选条目 ID; 可重复")
    group.add_argument("--important-file", help="结构化重要变更 JSON")
    p.add_argument("--timeout", type=int, default=120, help="子进程超时秒数, 10—1800")
    return p


def main(argv=None):
    p = parser()
    options = p.parse_args(argv)
    if not 10 <= options.timeout <= 1800:
        p.error("--timeout 必须为 10—1800 秒")
    if options.prepare_runtime:
        if any((options.from_ref, options.to_ref, options.version, options.content, options.coverage, options.important, options.important_file,
                options.jira_config, options.jira_snapshot, options.jira_prompt, options.no_jira, options.jira_status_scope != "all")):
            p.error("--prepare-runtime 不接受生成参数")
    elif not options.from_ref or not options.to_ref:
        p.error("生成必须同时提供 --from 和 --to")
    if options.jira_prompt and (options.jira_snapshot or options.no_jira):
        p.error("--jira-prompt 仅用于在线生成")
    try:
        require(sys.version_info >= (3, 10), "需要 Python 3.10 或更新版本")
        result = prepare(options) if options.prepare_runtime else generate(options)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError, ReleaseError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
