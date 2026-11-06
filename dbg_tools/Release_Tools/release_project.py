"""项目展示与 JIRA 状态配置；CLI、网页及生成结果使用同一份定义。"""
import json
from pathlib import Path


def load(path=None):
    path = Path(path) if path is not None else Path(__file__).resolve().parent / "release-project.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"项目配置读取失败: {path.name}") from error
    fields = {"schemaVersion", "defaultScope", "webTitle", "fromPlaceholder", "toPlaceholder", "acceptedJiraStatuses"}
    if not isinstance(data, dict) or set(data) != fields or type(data["schemaVersion"]) is not int or data["schemaVersion"] != 1:
        raise ValueError("项目配置必须包含 schemaVersion=1 及约定的全部字段，不接受额外字段")
    for name in ("defaultScope", "webTitle", "fromPlaceholder", "toPlaceholder"):
        value = data[name]
        if (not isinstance(value, str) or not value.strip() or len(value) > 256 or
                any(ord(c) < 32 or ord(c) == 127 for c in value)):
            raise ValueError(f"项目配置 {name} 必须为非空单行文本，最多 256 字符")
        data[name] = value.strip()
    states = data["acceptedJiraStatuses"]
    if not isinstance(states, list) or not 1 <= len(states) <= 32:
        raise ValueError("项目配置 acceptedJiraStatuses 必须包含 1—32 个状态")
    if any(not isinstance(value, str) or not value.strip() or len(value) > 64 or
           any(ord(c) < 32 or ord(c) == 127 for c in value) for value in states):
        raise ValueError("项目配置的状态必须为非空单行文本，最多 64 字符")
    data["acceptedJiraStatuses"] = [value.strip() for value in states]
    if len(set(data["acceptedJiraStatuses"])) != len(states):
        raise ValueError("项目配置的状态名称不能重复")
    return data
