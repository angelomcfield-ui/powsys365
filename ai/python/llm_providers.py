"""
ai/python/llm_providers.py
==========================

Unified LLM provider interface for POWSYS365 AI module.

Supports multiple backends:
* DeepSeek (deepseek-chat, deepseek-reasoner)
* Kimi / Moonshot (moonshot-v1-8k, moonshot-v1-32k)
* OpenAI GPT-4 (gpt-4-turbo, gpt-4o)
* Anthropic Claude (claude-3-opus, claude-3-sonnet)

Features:
- Consistent async and sync APIs
- Automatic retry with exponential backoff
- Rate limiting per provider
- Streaming response support
- Structured JSON output via response schemas
"""

from __future__ import annotations

import asyncio
import functools
import json
import logging
import os
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, AsyncIterator, Callable, Iterator, Protocol, TypeVar

import httpx

logger = logging.getLogger(__name__)

F = TypeVar("F", bound=Callable[..., Any])


# =========================================================================
# Data models
# =========================================================================


@dataclass
class Message:
    """A single message in a conversation."""

    role: str  # "system", "user", "assistant", "tool"
    content: str
    name: str = ""


@dataclass
class LLMRequest:
    """Request configuration for an LLM query."""

    messages: list[Message]
    model: str = ""
    temperature: float = 0.7
    max_tokens: int = 4096
    top_p: float = 1.0
    stream: bool = False
    json_schema: dict[str, Any] | None = None
    tools: list[dict[str, Any]] | None = None
    tool_choice: str | None = None


@dataclass
class LLMResponse:
    """Response from an LLM provider."""

    content: str = ""
    finish_reason: str = ""
    model: str = ""
    prompt_tokens: int = 0
    completion_tokens: int = 0
    total_tokens: int = 0
    latency_ms: float = 0.0
    success: bool = False
    error_message: str = ""
    tool_calls: list[dict[str, Any]] = field(default_factory=list)
    raw_response: dict[str, Any] = field(default_factory=dict)


# =========================================================================
# Retry decorator
# =========================================================================


def _retry_with_backoff(
    max_retries: int = 3,
    base_delay: float = 1.0,
    max_delay: float = 60.0,
    exceptions: tuple[type[BaseException], ...] = (
        httpx.HTTPStatusError,
        httpx.ConnectError,
        httpx.TimeoutException,
        httpx.NetworkError,
    ),
) -> Callable[[F], F]:
    """Decorator factory: retry a function with exponential backoff."""

    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> Any:
            delay = base_delay
            for attempt in range(1, max_retries + 1):
                try:
                    return func(*args, **kwargs)
                except exceptions as exc:
                    status_code = getattr(exc, "response", None)
                    status_code = (
                        status_code.status_code if status_code else 0
                    )
                    is_retryable = status_code in (
                        429, 500, 502, 503, 504
                    ) or status_code == 0

                    if not is_retryable or attempt == max_retries:
                        raise

                    logger.warning(
                        "[%s] Attempt %d/%d failed (HTTP %s): %s. "
                        "Retrying in %.1f s...",
                        func.__name__,
                        attempt,
                        max_retries,
                        status_code,
                        exc,
                        delay,
                    )
                    time.sleep(delay)
                    delay = min(delay * 2.0, max_delay)
            return None  # pragma: no cover

        return wrapper  # type: ignore[return-value]

    return decorator


# =========================================================================
# Base provider
# =========================================================================


class LLMProvider(ABC):
    """Abstract base class for LLM providers.

    All concrete providers must implement :meth:`chat` and
    :meth:`complete`.  Streaming variants have default implementations
    that call the sync methods.
    """

    def __init__(
        self,
        api_key: str | None = None,
        model: str = "",
        base_url: str = "",
        timeout: float = 60.0,
        max_retries: int = 3,
        rate_limit_rpm: int = 60,
    ) -> None:
        self.api_key = api_key or ""
        self.model = model
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.max_retries = max_retries
        self._rate_limit_rpm = rate_limit_rpm
        self._last_request_time: float = 0.0
        self._min_interval: float = 60.0 / max(rate_limit_rpm, 1)
        self._headers: dict[str, str] = {}
        self._setup_headers()

    @abstractmethod
    def _setup_headers(self) -> None:
        """Configure provider-specific HTTP headers (Auth, Content-Type, etc.)."""

    def _apply_rate_limit(self) -> None:
        """Block until the rate limit interval has elapsed."""
        elapsed = time.monotonic() - self._last_request_time
        if elapsed < self._min_interval:
            sleep_time = self._min_interval - elapsed
            logger.debug("Rate limit: sleeping %.2f s", sleep_time)
            time.sleep(sleep_time)
        self._last_request_time = time.monotonic()

    # -- Sync API --

    @abstractmethod
    def chat(self, request: LLMRequest) -> LLMResponse:
        """Send a chat request and return the response."""

    def complete(self, prompt: str, **kwargs: Any) -> LLMResponse:
        """Single-turn completion (convenience wrapper around :meth:`chat`)."""
        req = LLMRequest(
            messages=[Message(role="user", content=prompt)],
            **kwargs,
        )
        if not req.model:
            req.model = self.model
        return self.chat(req)

    # -- Streaming --

    def stream(self, request: LLMRequest) -> Iterator[str]:
        """Stream response chunks as an iterator of strings.

        Default implementation: call chat and yield the full content.
        Override for native streaming support.
        """
        request.stream = True
        resp = self.chat(request)
        if resp.success:
            yield resp.content

    async def astream(self, request: LLMRequest) -> AsyncIterator[str]:
        """Async streaming variant."""
        for chunk in self.stream(request):
            yield chunk

    # -- Utility --

    def _build_messages(self, request: LLMRequest) -> list[dict[str, str]]:
        """Convert Message dataclasses to provider-native dict format."""
        return [
            {"role": m.role, "content": m.content}
            for m in request.messages
            if m.role != "system" or self._supports_system_role()
        ]

    def _supports_system_role(self) -> bool:
        """Whether the provider supports explicit system messages."""
        return True

    def _parse_response(self, raw: dict[str, Any], latency_ms: float) -> LLMResponse:
        """Default parser for OpenAI-compatible response format."""
        resp = LLMResponse(latency_ms=latency_ms)
        resp.raw_response = raw

        if "error" in raw:
            resp.error_message = raw["error"].get("message", "Unknown API error")
            resp.success = False
            return resp

        resp.model = raw.get("model", "")

        choices = raw.get("choices", [])
        if choices:
            choice = choices[0]
            msg = choice.get("message", {})
            resp.content = msg.get("content", "")
            resp.finish_reason = choice.get("finish_reason", "")

            # Tool calls
            if "tool_calls" in msg:
                resp.tool_calls = msg["tool_calls"]

        usage = raw.get("usage", {})
        resp.prompt_tokens = usage.get("prompt_tokens", 0)
        resp.completion_tokens = usage.get("completion_tokens", 0)
        resp.total_tokens = usage.get("total_tokens", 0)
        resp.success = bool(resp.content) or bool(resp.tool_calls)

        return resp

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__} model={self.model}>"


# =========================================================================
# DeepSeek Provider
# =========================================================================


class DeepSeekProvider(LLMProvider):
    """DeepSeek AI API provider.

    Environment variable: ``DEEPSEEK_API_KEY``
    Models: ``deepseek-chat``, ``deepseek-reasoner``
    """

    DEFAULT_URL = "https://api.deepseek.com/v1"

    def __init__(
        self,
        api_key: str | None = None,
        model: str = "deepseek-chat",
        base_url: str = "",
        **kwargs: Any,
    ) -> None:
        api_key = api_key or os.environ.get("DEEPSEEK_API_KEY", "")
        super().__init__(
            api_key=api_key,
            model=model,
            base_url=base_url or self.DEFAULT_URL,
            rate_limit_rpm=kwargs.pop("rate_limit_rpm", 60),
            **kwargs,
        )

    def _setup_headers(self) -> None:
        self._headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }

    @_retry_with_backoff(max_retries=3, base_delay=1.0)
    def chat(self, request: LLMRequest) -> LLMResponse:
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body: dict[str, Any] = {
            "model": request.model,
            "messages": self._build_messages(request),
            "temperature": request.temperature,
            "max_tokens": request.max_tokens,
            "top_p": request.top_p,
        }
        if request.stream:
            body["stream"] = True
        if request.json_schema:
            body["response_format"] = {
                "type": "json_schema",
                "json_schema": request.json_schema,
            }
        if request.tools:
            body["tools"] = request.tools
        if request.tool_choice:
            body["tool_choice"] = request.tool_choice

        t0 = time.perf_counter()
        try:
            with httpx.Client(timeout=self.timeout) as client:
                response = client.post(
                    f"{self.base_url}/chat/completions",
                    headers=self._headers,
                    json=body,
                )
                response.raise_for_status()
                raw = response.json()
        except httpx.HTTPStatusError as exc:
            logger.error("DeepSeek API error: %s - %s", exc.response.status_code, exc.response.text)
            return LLMResponse(
                success=False,
                error_message=f"HTTP {exc.response.status_code}: {exc.response.text}",
            )
        except httpx.ConnectError as exc:
            return LLMResponse(success=False, error_message=f"Connection error: {exc}")

        latency_ms = (time.perf_counter() - t0) * 1000.0
        return self._parse_response(raw, latency_ms)

    def stream(self, request: LLMRequest) -> Iterator[str]:
        """Native SSE streaming for DeepSeek."""
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body = {
            "model": request.model,
            "messages": self._build_messages(request),
            "temperature": request.temperature,
            "max_tokens": request.max_tokens,
            "stream": True,
        }

        with httpx.Client(timeout=self.timeout) as client:
            with client.stream(
                "POST",
                f"{self.base_url}/chat/completions",
                headers=self._headers,
                json=body,
            ) as response:
                response.raise_for_status()
                for line in response.iter_lines():
                    line = line.strip()
                    if line.startswith("data: "):
                        data = line[6:]
                        if data == "[DONE]":
                            break
                        try:
                            chunk = json.loads(data)
                            delta = chunk.get("choices", [{}])[0].get("delta", {})
                            content = delta.get("content", "")
                            if content:
                                yield content
                        except (json.JSONDecodeError, IndexError, KeyError):
                            continue


# =========================================================================
# Kimi / Moonshot Provider
# =========================================================================


class KimiProvider(LLMProvider):
    """Kimi (Moonshot AI) API provider.

    Environment variable: ``KIMI_API_KEY``
    Models: ``moonshot-v1-8k``, ``moonshot-v1-32k``, ``moonshot-v1-128k``
    """

    DEFAULT_URL = "https://api.moonshot.cn/v1"

    def __init__(
        self,
        api_key: str | None = None,
        model: str = "moonshot-v1-8k",
        base_url: str = "",
        **kwargs: Any,
    ) -> None:
        api_key = api_key or os.environ.get("KIMI_API_KEY", "")
        super().__init__(
            api_key=api_key,
            model=model,
            base_url=base_url or self.DEFAULT_URL,
            rate_limit_rpm=kwargs.pop("rate_limit_rpm", 30),
            **kwargs,
        )

    def _setup_headers(self) -> None:
        self._headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }

    @_retry_with_backoff(max_retries=3, base_delay=1.5)
    def chat(self, request: LLMRequest) -> LLMResponse:
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body = {
            "model": request.model,
            "messages": self._build_messages(request),
            "temperature": request.temperature,
            "max_tokens": request.max_tokens,
        }

        t0 = time.perf_counter()
        try:
            with httpx.Client(timeout=self.timeout) as client:
                response = client.post(
                    f"{self.base_url}/chat/completions",
                    headers=self._headers,
                    json=body,
                )
                response.raise_for_status()
                raw = response.json()
        except httpx.HTTPStatusError as exc:
            return LLMResponse(
                success=False,
                error_message=f"HTTP {exc.response.status_code}: {exc.response.text}",
            )

        latency_ms = (time.perf_counter() - t0) * 1000.0
        return self._parse_response(raw, latency_ms)


# =========================================================================
# OpenAI GPT Provider
# =========================================================================


class GPTProvider(LLMProvider):
    """OpenAI GPT-4 API provider.

    Environment variable: ``OPENAI_API_KEY``
    Models: ``gpt-4-turbo``, ``gpt-4o``, ``gpt-4o-mini``
    """

    DEFAULT_URL = "https://api.openai.com/v1"

    def __init__(
        self,
        api_key: str | None = None,
        model: str = "gpt-4-turbo-preview",
        base_url: str = "",
        **kwargs: Any,
    ) -> None:
        api_key = api_key or os.environ.get("OPENAI_API_KEY", "")
        super().__init__(
            api_key=api_key,
            model=model,
            base_url=base_url or self.DEFAULT_URL,
            rate_limit_rpm=kwargs.pop("rate_limit_rpm", 40),
            **kwargs,
        )

    def _setup_headers(self) -> None:
        self._headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }
        if self.model.startswith("o1") or self.model.startswith("o3"):
            # o1/o3 models don't support temperature
            pass

    @_retry_with_backoff(max_retries=3, base_delay=1.0)
    def chat(self, request: LLMRequest) -> LLMResponse:
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body: dict[str, Any] = {
            "model": request.model,
            "messages": self._build_messages(request),
            "max_tokens": request.max_tokens,
            "top_p": request.top_p,
        }

        # o1/o3 models use different parameter names
        if self.model.startswith("o1") or self.model.startswith("o3"):
            body["reasoning_effort"] = "medium"
            del body["top_p"]
        else:
            body["temperature"] = request.temperature

        if request.json_schema:
            body["response_format"] = {"type": "json_object"}
        if request.tools:
            body["tools"] = request.tools
        if request.tool_choice:
            body["tool_choice"] = request.tool_choice

        t0 = time.perf_counter()
        try:
            with httpx.Client(timeout=self.timeout) as client:
                response = client.post(
                    f"{self.base_url}/chat/completions",
                    headers=self._headers,
                    json=body,
                )
                response.raise_for_status()
                raw = response.json()
        except httpx.HTTPStatusError as exc:
            return LLMResponse(
                success=False,
                error_message=f"HTTP {exc.response.status_code}: {exc.response.text}",
            )

        latency_ms = (time.perf_counter() - t0) * 1000.0
        return self._parse_response(raw, latency_ms)

    def stream(self, request: LLMRequest) -> Iterator[str]:
        """Native SSE streaming for OpenAI."""
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body = {
            "model": request.model,
            "messages": self._build_messages(request),
            "temperature": request.temperature,
            "max_tokens": request.max_tokens,
            "stream": True,
        }

        with httpx.Client(timeout=self.timeout) as client:
            with client.stream(
                "POST",
                f"{self.base_url}/chat/completions",
                headers=self._headers,
                json=body,
            ) as response:
                response.raise_for_status()
                for line in response.iter_lines():
                    line = line.strip()
                    if line.startswith("data: "):
                        data = line[6:]
                        if data == "[DONE]":
                            break
                        try:
                            chunk = json.loads(data)
                            delta = chunk.get("choices", [{}])[0].get("delta", {})
                            content = delta.get("content", "")
                            if content:
                                yield content
                        except (json.JSONDecodeError, IndexError, KeyError):
                            continue


# =========================================================================
# Anthropic Claude Provider
# =========================================================================


class ClaudeProvider(LLMProvider):
    """Anthropic Claude API provider.

    Environment variable: ``ANTHROPIC_API_KEY``
    Models: ``claude-3-opus-20240229``, ``claude-3-sonnet-20240229``,
            ``claude-3-haiku-20240307``, ``claude-3-5-sonnet-20241022``
    """

    DEFAULT_URL = "https://api.anthropic.com/v1"
    API_VERSION = "2023-06-01"

    def __init__(
        self,
        api_key: str | None = None,
        model: str = "claude-3-opus-20240229",
        base_url: str = "",
        **kwargs: Any,
    ) -> None:
        api_key = api_key or os.environ.get("ANTHROPIC_API_KEY", "")
        super().__init__(
            api_key=api_key,
            model=model,
            base_url=base_url or self.DEFAULT_URL,
            rate_limit_rpm=kwargs.pop("rate_limit_rpm", 40),
            **kwargs,
        )

    def _setup_headers(self) -> None:
        self._headers = {
            "x-api-key": self.api_key,
            "anthropic-version": self.API_VERSION,
            "Content-Type": "application/json",
        }

    def _build_messages(self, request: LLMRequest) -> list[dict[str, str]]:
        """Claude uses 'system' as top-level, not in message list."""
        messages = []
        for m in request.messages:
            if m.role == "system":
                continue
            messages.append({"role": m.role, "content": m.content})
        return messages

    def _extract_system_message(self, request: LLMRequest) -> str:
        """Extract system message content from request."""
        for m in request.messages:
            if m.role == "system":
                return m.content
        return ""

    @_retry_with_backoff(max_retries=3, base_delay=1.0)
    def chat(self, request: LLMRequest) -> LLMResponse:
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body: dict[str, Any] = {
            "model": request.model,
            "max_tokens": request.max_tokens,
            "temperature": request.temperature,
            "messages": self._build_messages(request),
        }

        system_msg = self._extract_system_message(request)
        if system_msg:
            body["system"] = system_msg

        if request.tools:
            body["tools"] = request.tools
        if request.tool_choice:
            body["tool_choice"] = {"type": request.tool_choice}

        t0 = time.perf_counter()
        try:
            with httpx.Client(timeout=self.timeout) as client:
                response = client.post(
                    f"{self.base_url}/messages",
                    headers=self._headers,
                    json=body,
                )
                response.raise_for_status()
                raw = response.json()
        except httpx.HTTPStatusError as exc:
            return LLMResponse(
                success=False,
                error_message=f"HTTP {exc.response.status_code}: {exc.response.text}",
            )

        latency_ms = (time.perf_counter() - t0) * 1000.0
        return self._parse_claude_response(raw, latency_ms)

    def _parse_claude_response(self, raw: dict[str, Any], latency_ms: float) -> LLMResponse:
        """Parse Anthropic-specific response format."""
        resp = LLMResponse(latency_ms=latency_ms)
        resp.raw_response = raw

        if "error" in raw:
            resp.error_message = raw["error"].get("message", "Unknown API error")
            resp.success = False
            return resp

        resp.model = raw.get("model", "")
        resp.finish_reason = raw.get("stop_reason", "")

        usage = raw.get("usage", {})
        resp.prompt_tokens = usage.get("input_tokens", 0)
        resp.completion_tokens = usage.get("output_tokens", 0)
        resp.total_tokens = resp.prompt_tokens + resp.completion_tokens

        # Content blocks
        content_parts: list[str] = []
        for block in raw.get("content", []):
            if block.get("type") == "text":
                content_parts.append(block.get("text", ""))
            elif block.get("type") == "tool_use":
                resp.tool_calls.append({
                    "id": block.get("id", ""),
                    "name": block.get("name", ""),
                    "input": block.get("input", {}),
                })

        resp.content = "\n".join(content_parts)
        resp.success = bool(resp.content) or bool(resp.tool_calls)
        return resp

    def stream(self, request: LLMRequest) -> Iterator[str]:
        """Native SSE streaming for Claude."""
        self._apply_rate_limit()
        if not request.model:
            request.model = self.model

        body: dict[str, Any] = {
            "model": request.model,
            "max_tokens": request.max_tokens,
            "temperature": request.temperature,
            "messages": self._build_messages(request),
            "stream": True,
        }
        system_msg = self._extract_system_message(request)
        if system_msg:
            body["system"] = system_msg

        with httpx.Client(timeout=self.timeout) as client:
            with client.stream(
                "POST",
                f"{self.base_url}/messages",
                headers=self._headers,
                json=body,
            ) as response:
                response.raise_for_status()
                for line in response.iter_lines():
                    line = line.strip()
                    if line.startswith("data: "):
                        data = line[6:]
                        if data == "[DONE]":
                            break
                        try:
                            event = json.loads(data)
                            etype = event.get("type", "")
                            if etype == "content_block_delta":
                                delta = event.get("delta", {})
                                if delta.get("type") == "text_delta":
                                    yield delta.get("text", "")
                        except (json.JSONDecodeError, KeyError):
                            continue


# =========================================================================
# Factory
# =========================================================================


def create_provider(
    provider_name: str,
    api_key: str | None = None,
    model: str | None = None,
    **kwargs: Any,
) -> LLMProvider:
    """Factory function to create an LLM provider by name.

    Parameters
    ----------
    provider_name:
        One of ``"deepseek"``, ``"kimi"``, ``"gpt"``, ``"claude"``.
    api_key:
        API key (defaults to environment variable).
    model:
        Model name (provider-specific default if not given).
    **kwargs:
        Additional arguments passed to the provider constructor.

    Returns
    -------
    LLMProvider
        Configured provider instance.

    Raises
    ------
    ValueError: If *provider_name* is not recognised.
    """
    name = provider_name.strip().lower()

    providers: dict[str, Callable[..., LLMProvider]] = {
        "deepseek": DeepSeekProvider,
        "kimi": KimiProvider,
        "gpt": GPTProvider,
        "gpt4": GPTProvider,
        "openai": GPTProvider,
        "claude": ClaudeProvider,
        "anthropic": ClaudeProvider,
    }

    if name not in providers:
        raise ValueError(
            f"Unknown provider '{provider_name}'. "
            f"Available: {', '.join(sorted(set(providers.keys())))}"
        )

    provider_cls = providers[name]
    args: dict[str, Any] = {}
    if api_key:
        args["api_key"] = api_key
    if model:
        args["model"] = model
    args.update(kwargs)

    return provider_cls(**args)


# =========================================================================
# Multi-provider fallback
# =========================================================================


class MultiProvider:
    """Chain-of-providers with automatic fallback.

    Attempts each provider in order until one succeeds.

    Example::

        multi = MultiProvider([
            ("deepseek", {"model": "deepseek-reasoner"}),
            ("gpt", {"model": "gpt-4o"}),
            ("claude", {"model": "claude-3-sonnet"}),
        ])
        resp = multi.chat(LLMRequest(messages=[Message("user", "Hello")]))
    """

    def __init__(
        self,
        providers: list[tuple[str, dict[str, Any]]],
    ) -> None:
        self.providers: list[LLMProvider] = []
        for name, kwargs in providers:
            try:
                self.providers.append(create_provider(name, **kwargs))
            except Exception as exc:
                logger.warning("Failed to initialise %s: %s", name, exc)

    def chat(self, request: LLMRequest) -> LLMResponse:
        """Send request to providers in order until one succeeds."""
        for provider in self.providers:
            try:
                logger.debug("Trying provider: %s", provider)
                resp = provider.chat(request)
                if resp.success:
                    return resp
                logger.warning("Provider %s returned error: %s", provider, resp.error_message)
            except Exception as exc:
                logger.warning("Provider %s failed: %s", provider, exc)

        return LLMResponse(
            success=False,
            error_message="All providers failed or are unavailable",
        )

    def complete(self, prompt: str, **kwargs: Any) -> LLMResponse:
        """Single-turn completion with fallback."""
        req = LLMRequest(
            messages=[Message(role="user", content=prompt)],
            **kwargs,
        )
        return self.chat(req)

    def __repr__(self) -> str:
        names = [p.__class__.__name__ for p in self.providers]
        return f"<MultiProvider providers={names}>"
