#!/usr/bin/env python3
"""本机 Release Notes 页面；只调用现有生成器，不接受浏览器提供的命令或路径。"""
from __future__ import annotations

import argparse
import copy
import html
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import secrets
import sys
import threading
from urllib.parse import quote, unquote, urlsplit

import generate_release_notes as core


class WebError(ValueError):
    pass


class BusyError(WebError):
    pass


def cached_jira_loader(path, prompt=False):
    """启动时取凭据，仅保留在服务进程中；配置变更后要求重启以免使用旧配置。"""
    path = core.physical_path(path)
    config, auth = core.jira_client.load_jira_config(path, prompt=prompt)
    watched = [path] + ([Path(config["caFile"])] if config["caFile"] else [])
    fingerprints = {p: core.file_hash(p) for p in watched}

    def load(requested, *, prompt=False):
        try:
            unchanged = core.physical_path(requested) == path and all(core.file_hash(p) == value for p, value in fingerprints.items())
        except (OSError, core.ReleaseError):
            unchanged = False
        if not unchanged:
            raise core.jira_client.JiraError("config_changed", "JIRA 配置已变更，请重启网页服务")
        return copy.deepcopy(config), auth
    return load


class ReleaseService:
    def __init__(self, tools, runtime_root, config_path, loader, generator=None, *, project_config=None):
        self.tools = Path(tools).resolve()
        self.runtime_root, self.config_path = str(runtime_root), str(config_path)
        self.loader, self.generator = loader, generator or core.generate
        self.project_path = core.physical_path(project_config or self.tools / "release-project.json")
        self.project_sha256 = core.file_hash(self.project_path)
        self.project_config = core.release_project.load(self.project_path)
        self._check_project()
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.RLock()
        self.jobs, self.active, self.latest, self.worker = {}, None, None, None
        self.stopping = False

    def _check_project(self):
        try:
            if core.file_hash(self.project_path) == self.project_sha256:
                return
        except OSError:
            pass
        raise WebError("项目配置已变更，请重启网页服务")

    def start(self, value):
        self._check_project()
        if not isinstance(value, dict) or not {"from", "to"} <= set(value) <= {"from", "to", "statusScope"}:
            raise WebError("请提供基线和目标版本")
        scope = value.get("statusScope", "all")
        if not isinstance(scope, str) or scope not in core.release_status.SCOPES:
            raise WebError("生成范围无效")
        for ref in (value["from"], value["to"]):
            if not isinstance(ref, str) or not ref.strip() or len(ref) > 256 or any(ord(c) < 32 for c in ref):
                raise WebError("版本不能为空，且不得包含换行或控制字符")
        with self.lock:
            if self.stopping or self.active:
                raise BusyError("已有任务正在生成，请等待完成")
            job_id = secrets.token_hex(12)
            job = {"id": job_id, "status": "running", "stage": "准备生成", "from": value["from"].strip(),
                   "to": value["to"].strip(), "statusScope": scope, "result": None, "error": None, "files": [], "batch": None}
            # 只保留最近八个任务的内存状态；磁盘批次仍由原生成器归档。
            while len(self.jobs) >= 8:
                self.jobs.pop(next(iter(self.jobs)))
            self.jobs[job_id] = job
            self.active = self.latest = job_id
            self.worker = threading.Thread(target=self._run, args=(job_id,), name="release-notes-job")
            self.worker.start()
            return self.get(job_id)

    def _progress(self, job_id, stage):
        with self.lock:
            self.jobs[job_id]["stage"] = stage

    def _files(self, batch):
        if batch is None:
            return []
        batch = core.physical_path(batch)
        if batch.parent != (self.tools / "release").resolve():
            return []
        manifest = core.read_json(batch / core.MANIFEST)
        permitted = set(manifest.get("artifacts", {})) | {core.MANIFEST}
        names = []
        for name in sorted(permitted):
            path = batch / name
            if (name in {"CHANGELOG.generated.md", "release-data.json", "raw-commits.txt", core.MANIFEST, "coverage-review.md"}
                    or name.startswith(("Release_Notes_", "github-release-body_", "JIRA_Status_Review_")) and name.endswith(".md")):
                if path.is_file() and not path.is_symlink() and path.resolve().parent == batch:
                    names.append(name)
        return names

    def _run(self, job_id):
        job = self.get(job_id)
        options = core.parser().parse_args([])
        options.from_ref, options.to_ref = job["from"], job["to"]
        options.jira_status_scope = job["statusScope"]
        options.jira_config, options.runtime_root = self.config_path, self.runtime_root
        options.project_config, options.project_config_sha256 = str(self.project_path), self.project_sha256
        batch, result, error = None, None, None
        try:
            result = self.generator(options, self.tools, jira_loader=self.loader,
                                    progress=lambda stage: self._progress(job_id, stage))
            batch = Path(result["batch"])
        except core.ReleaseError as exc:
            error, batch = str(exc), getattr(exc, "batch", None)
        except Exception:
            # 未知异常不回传原始对象，避免把凭据或内部响应带到浏览器。
            error = "生成遇到内部异常，请检查本机运行环境和配置"
        try:
            files = self._files(batch)
        except (OSError, ValueError, TypeError, core.ReleaseError):
            files = []
        with self.lock:
            self.jobs[job_id].update(status="failed" if error else "done", stage="生成失败" if error else "生成完成",
                                     result=result, error=error, batch=str(batch) if batch else None, files=files)
            self.active = None

    def get(self, job_id):
        with self.lock:
            if job_id not in self.jobs:
                raise WebError("任务不存在或已从页面历史中移除")
            return copy.deepcopy(self.jobs[job_id])

    def state(self):
        with self.lock:
            return {"latest": self.get(self.latest) if self.latest else None}

    def artifact(self, job_id, name):
        job = self.get(job_id)
        if name not in job["files"] or not job["batch"]:
            raise WebError("文件不属于该生成任务")
        batch = core.physical_path(job["batch"])
        path = core.physical_path(batch / name)
        if batch.parent != (self.tools / "release").resolve() or path.is_symlink() or path.resolve().parent != batch:
            raise WebError("文件路径无效")
        return path

    def close(self):
        with self.lock:
            self.stopping = True
            worker = self.worker
        if worker is not None:
            worker.join()


class ReleaseHandler(BaseHTTPRequestHandler):
    server_version = "ReleaseNotes"

    def log_message(self, format, *args):
        pass

    def _send(self, status, payload, mime="application/json; charset=utf-8", filename=None):
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8") if isinstance(payload, dict) else payload
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
        if filename:
            self.send_header("Content-Disposition", "attachment; filename*=UTF-8''" + quote(filename, safe=""))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _allowed(self, token=False):
        port = self.server.server_port
        hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}
        if self.headers.get("Host") not in hosts or self.headers.get("Origin") not in {None, *("http://" + h for h in hosts)}:
            self._send(403, {"error": "仅允许本机页面访问"})
            return False
        if token and not secrets.compare_digest(self.headers.get("X-Release-Token", "").encode("utf-8"),
                                                self.server.service.token.encode("ascii")):
            self._send(403, {"error": "页面会话已失效，请刷新"})
            return False
        return True

    def do_GET(self):
        if not self._allowed():
            return
        path = urlsplit(self.path)
        if path.query or path.fragment:
            self._send(404, {"error": "地址不存在"})
            return
        static = {"/": ("index.html", "text/html; charset=utf-8"), "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                  "/style.css": ("style.css", "text/css; charset=utf-8")}
        try:
            if path.path in static:
                name, mime = static[path.path]
                data = (self.server.service.tools / "web" / name).read_bytes()
                if name == "index.html":
                    project = self.server.service.project_config
                    values = {"__SESSION_TOKEN__": self.server.service.token, "__PROJECT_NAME__": project["webTitle"],
                              "__DEFAULT_SCOPE__": project["defaultScope"], "__FROM_PLACEHOLDER__": project["fromPlaceholder"],
                              "__TO_PLACEHOLDER__": project["toPlaceholder"], "__ACCEPTED_STATUSES__": "、".join(project["acceptedJiraStatuses"])}
                    # 一次替换并转义配置文本，避免 HTML 注入或配置值触发二次占位符替换。
                    data = re.sub("|".join(values), lambda match: html.escape(values[match[0]], quote=True),
                                  data.decode("utf-8")).encode("utf-8")
                self._send(200, data, mime)
                return
            if not self._allowed(token=True):
                return
            if path.path == "/api/state":
                self._send(200, self.server.service.state())
                return
            parts = path.path.strip("/").split("/")
            if len(parts) == 3 and parts[:2] == ["api", "jobs"]:
                self._send(200, self.server.service.get(parts[2]))
                return
            if len(parts) == 5 and parts[:2] == ["api", "jobs"] and parts[3] == "files":
                name = unquote(parts[4])
                file = self.server.service.artifact(parts[2], name)
                self._send(200, file.read_bytes(), "text/plain; charset=utf-8", filename=name)
                return
        except (OSError, ValueError, core.ReleaseError):
            self._send(404, {"error": "任务或文件不存在"})
            return
        self._send(404, {"error": "地址不存在"})

    def do_POST(self):
        if not self._allowed(token=True):
            return
        if self.path != "/api/jobs":
            self._send(404, {"error": "地址不存在"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096 or self.headers.get("Transfer-Encoding") or self.headers.get_content_type() != "application/json":
                self._send(400, {"error": "请求格式无效"})
                return
            self.connection.settimeout(10)
            value = json.loads(self.rfile.read(length).decode("utf-8"))
            self._send(202, self.server.service.start(value))
        except BusyError as exc:
            self._send(409, {"error": str(exc)})
        except WebError as exc:
            self._send(400, {"error": str(exc)})
        except (ValueError, OSError):
            self._send(400, {"error": "请求格式无效"})


def create_server(service, port=8765):
    server = ThreadingHTTPServer(("127.0.0.1", port), ReleaseHandler)
    server.daemon_threads = True
    server.service = service
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description="启动本机 Release Notes 页面，仅监听 127.0.0.1；日常只输入两个版本")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--jira-config", type=Path, default=None)
    parser.add_argument("--project-config", type=Path, help="项目展示/状态配置；默认工具目录 release-project.json")
    parser.add_argument("--runtime-root", default=str(Path.home() / ".ecw-release-tools"))
    args = parser.parse_args(argv)
    if not 0 <= args.port <= 65535:
        parser.error("端口必须为 0—65535")
    service = server = None
    try:
        core.require(sys.version_info >= (3, 10), "需要 Python 3.10 或更新版本")
        config = core.physical_path(args.jira_config or core.default_jira_config())
        runner = core.Runner(core.TOOLS.parent.parent)
        base, _, _, node_info = core.baseline(core.TOOLS, runner)
        _, runtime, receipt = core.runtime_paths(args.runtime_root, core.TOOLS.parent.parent, base)
        core.verify_runtime(runtime, receipt, base, node_info)
        loader = cached_jira_loader(config, prompt=sys.stdin.isatty())
        service = ReleaseService(core.TOOLS, args.runtime_root, config, loader, project_config=args.project_config)
        server = create_server(service, args.port)
        print(f"打开浏览器访问 http://127.0.0.1:{server.server_port}/")
        print("输入基线和目标版本即可生成；按 Ctrl+C 停止，正在生成的任务将先完成。", flush=True)
        server.serve_forever()
    except KeyboardInterrupt:
        print("正在关闭网页服务，等待当前生成结束。")
    except EOFError:
        print("认证输入已结束，请重新启动或提供认证环境变量", file=sys.stderr)
        return 1
    except (OSError, ValueError, core.ReleaseError, core.jira_client.JiraError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        if server:
            server.server_close()
        if service:
            service.close()
    return 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
