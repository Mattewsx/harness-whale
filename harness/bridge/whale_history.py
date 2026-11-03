"""Read a visible main session's title, last terminal state and final public reply."""

import hashlib
import io
import json
import re
from dataclasses import dataclass

import zstandard


@dataclass
class SessionHistory:
    title: str = ""
    state: str = "idle"
    elapsed: int = 0
    reply: str = ""


def assistant_text(data):
    message = data.get("message") or {}
    blocks = message.get("content")
    if data.get("interrupted") or message.get("role") != "assistant" or not isinstance(blocks, list):
        return ""
    if any(not isinstance(b, dict) or b.get("type") == "tool-call" for b in blocks):
        return ""
    text = "\n".join(b["text"] for b in blocks if b.get("type") == "text" and isinstance(b.get("text"), str))
    return re.sub(r"[\x00-\x09\x0b-\x1f\x7f]", " ", text.replace("\r\n", "\n").replace("\r", "\n")).strip()


def journal_path(root, key):
    if not re.fullmatch(r"[\w.-]{1,255}", key, re.ASCII) or key in (".", ".."):
        return None
    paths = list(root.glob(f"*/{key}/session.v4.jsonl.zstd"))
    return paths[0] if len(paths) == 1 else None


def reason_kind(reason):
    return reason.get("kind") if isinstance(reason, dict) else reason


def read_session_history(path, key):
    try:
        with path.open("rb") as source, zstandard.ZstdDecompressor().stream_reader(
            source, read_across_frames=True
        ) as decoded, io.BufferedReader(decoded) as lines:
            header = json.loads(lines.readline())
            if (header.get("type") != "session" or header.get("version") != 4 or header.get("id") != key
                    or header.get("origin") == "subagent" or header.get("delegationDepth", 0) != 0):
                return None
            history = SessionHistory()
            inherited = header.get("isSeeded", False)
            candidate_turn = start_turn = started_at = None
            candidate = ""
            for line in lines:
                event = json.loads(line)
                kind = event["type"]
                if kind == "session/end-seed":
                    inherited = False
                if inherited:
                    continue
                data = event.get("data", {})
                if kind == "session/title" and isinstance(data.get("title"), str):
                    history.title = data["title"].strip() or history.title
                elif kind == "turn/start":
                    start_turn, started_at = data.get("turn"), event.get("time")
                    history.state, history.elapsed, candidate_turn = "idle", 0, None
                elif kind == "assistant/message":
                    candidate_turn, candidate = data.get("turn"), assistant_text(data)
                elif kind == "turn/end":
                    turn = data.get("turn")
                    reason = data.get("reason")
                    end = reason_kind(reason)
                    cause = reason_kind(reason.get("reason")) if isinstance(reason, dict) else None
                    if end == "completed" and turn is not None and turn == candidate_turn and candidate:
                        history.reply = candidate
                    if end in ("cancelled", "canceled", "stopped") or (end == "aborted" and cause == "user"):
                        history.state = "stopped"
                    elif end == "completed" and not data.get("error"):
                        history.state = "done"
                    else:
                        history.state = "error"
                    ended_at = event.get("time")
                    if turn == start_turn and isinstance(started_at, int) and isinstance(ended_at, int):
                        history.elapsed = max(0, ended_at - started_at) // 1000
                    candidate_turn = None
            return history
    except (OSError, ValueError, TypeError, KeyError, AttributeError, zstandard.ZstdError):
        # A journal can be in the middle of a write. A later poll retries it.
        return None


def reply_pages(text):
    raw = text.encode()
    pages = []
    start = 0
    while start < len(raw):
        end = min(start + 384, len(raw))
        while end < len(raw) and raw[end] & 0xC0 == 0x80:
            end -= 1
        pages.append(raw[start:end])
        start = end
    return "history-" + hashlib.sha256(raw).hexdigest(), pages
