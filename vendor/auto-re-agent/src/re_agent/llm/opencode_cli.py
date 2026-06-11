"""opencode CLI-backed LLM provider (uses opencode's configured providers, e.g. Ollama Cloud)."""
from __future__ import annotations

import json
import subprocess
import uuid
from typing import Any

from re_agent.llm.protocol import Message


class OpenCodeCLIProvider:
    """LLM provider backed by the local ``opencode`` CLI in headless run mode.

    Shells ``opencode run -m <provider/model> --variant <v> --format json`` and
    feeds the rendered prompt on stdin (proven to read stdin, so no Windows
    command-line length limit on big decompile prompts). Uses opencode's own
    configured credentials (e.g. ``ollama-cloud``), so no API key flows through
    re-agent. Mirrors :class:`ClaudeCLIProvider`.

    The ``--variant`` flag is opencode's provider-specific reasoning-effort knob
    (e.g. ``max``/``high``/``minimal``). Because Ollama model slugs already use
    ``:`` for tags (``deepseek-v3.1:671b``), the variant is carried as an ``@``
    suffix on the model string instead: ``ollama-cloud/deepseek-v4-pro@max``.

    ``--format json`` emits one JSON event per line; the assistant text lives in
    events with top-level ``"type": "text"`` (``part.text``). Tool/step events
    are ignored, so the result is a clean text completion.
    """

    def __init__(
        self,
        model: str = "ollama-cloud/deepseek-v4-pro",
        timeout_s: int = 1800,
        opencode_bin: str = "opencode",
        variant: str | None = None,
    ) -> None:
        # model may carry a variant suffix, e.g. "ollama-cloud/deepseek-v4-pro@max".
        # Split on the last "@" only (provider/model slugs contain "/" and ":",
        # never "@"), so the variant separator can't collide with an Ollama tag.
        if "@" in model and variant is None:
            model, variant = model.rsplit("@", 1)
        self._model = model
        self._variant = variant
        self._timeout_s = timeout_s
        self._opencode_bin = opencode_bin
        self._conversations: dict[str, list[Message]] = {}

    def send(self, messages: list[Message], **kwargs: Any) -> str:
        prompt = self._render_messages(messages)
        model = kwargs.get("model", self._model)
        try:
            cmd = [
                self._opencode_bin,
                "run",
                "-m",
                str(model),
                "--format",
                "json",
                "--dangerously-skip-permissions",
                "--pure",
            ]
            if self._variant:
                cmd += ["--variant", self._variant]
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
            raise RuntimeError(f"opencode run timed out after {self._timeout_s}s") from exc
        except FileNotFoundError as exc:
            raise RuntimeError(f"opencode CLI not found: {self._opencode_bin}") from exc
        if proc.returncode != 0:
            raise RuntimeError(
                f"opencode run failed with exit code {proc.returncode}\n{proc.stderr or proc.stdout}"
            )
        return self._extract_text(proc.stdout)

    @staticmethod
    def _extract_text(raw: str) -> str:
        """Concatenate the text parts from opencode's --format json event stream.

        Each line is one JSON event; assistant output is in events whose
        top-level ``type`` is ``"text"`` (``part.text``). Non-JSON or non-text
        lines are skipped, so tool/step/reasoning events don't leak in.
        """
        chunks: list[str] = []
        for line in raw.splitlines():
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if event.get("type") != "text":
                continue
            part = event.get("part") or {}
            text = part.get("text")
            if text:
                chunks.append(text)
        return "".join(chunks)

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
