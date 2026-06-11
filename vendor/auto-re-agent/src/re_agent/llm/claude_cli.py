"""Claude Code CLI-backed LLM provider (uses the local subscription, no API key)."""
from __future__ import annotations

import subprocess
import uuid
from typing import Any

from re_agent.llm.protocol import Message


class ClaudeCLIProvider:
    """LLM provider backed by the local ``claude`` CLI in headless print mode.

    Shells ``claude -p --output-format text --model <model>`` and feeds the
    rendered prompt on stdin. Uses the user's Claude Code subscription/login,
    so no Anthropic API key is required. Mirrors :class:`CodexCLIProvider`.

    In ``-p`` mode any tool call that would need permission is skipped (headless
    can't prompt), so the model just returns text — a clean, side-effect-free
    completion.
    """

    def __init__(
        self,
        model: str = "sonnet",
        timeout_s: int = 1800,
        claude_bin: str = "claude",
        effort: str | None = None,
    ) -> None:
        # model may carry an effort suffix, e.g. "sonnet:medium" -> --effort medium
        if ":" in model and effort is None:
            model, effort = model.split(":", 1)
        self._model = model
        self._effort = effort
        self._timeout_s = timeout_s
        self._claude_bin = claude_bin
        self._conversations: dict[str, list[Message]] = {}

    def send(self, messages: list[Message], **kwargs: Any) -> str:
        prompt = self._render_messages(messages)
        model = kwargs.get("model", self._model)
        try:
            cmd = [
                self._claude_bin,
                "-p",
                "--output-format",
                "text",
                "--model",
                str(model),
            ]
            if self._effort:
                cmd += ["--effort", self._effort]
            proc = subprocess.run(
                cmd,
                input=prompt,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                errors="replace",
                timeout=self._timeout_s,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"claude -p timed out after {self._timeout_s}s") from exc
        except FileNotFoundError as exc:
            raise RuntimeError(f"claude CLI not found: {self._claude_bin}") from exc
        if proc.returncode != 0:
            raise RuntimeError(
                f"claude -p failed with exit code {proc.returncode}\n{proc.stderr or proc.stdout}"
            )
        return proc.stdout

    @property
    def supports_conversations(self) -> bool:
        return True

    def new_conversation(self, system: str) -> str:
        cid = uuid.uuid4().hex
        self._conversations[cid] = [Message(role="system", content=system)]
        return cid

    def resume(self, conversation_id: str, message: str) -> str:
        history = self._conversations.get(conversation_id)
        if history is None:
            raise KeyError(f"Unknown conversation ID: {conversation_id}")

        history.append(Message(role="user", content=message))
        response_text = self.send(list(history))
        history.append(Message(role="assistant", content=response_text))
        return response_text

    @staticmethod
    def _render_messages(messages: list[Message]) -> str:
        parts: list[str] = []
        for msg in messages:
            role = msg.role.upper()
            parts.append(f"[{role}]\n{msg.content.strip()}")
        return "\n\n".join(parts).strip()
