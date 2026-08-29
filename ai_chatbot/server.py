"""HTTP server for the CEREVIA companion.

Runs on the Python standard library alone. The original service required Flask,
flask-cors and vaderSentiment; if any of those were missing the chat feature
simply died with an import error and the UI showed "Unable to connect to AI
server". Removing the dependencies means `./cerevia` can start the companion on
a clean machine with no pip install step.

Endpoints
    GET  /health      liveness, plus how many sessions are live
    GET  /techniques  the full coping-technique library
    POST /chat        {message, sessionId?} -> {response, emotion, risk, ...}
    POST /reset       {sessionId} -> forget that conversation
"""

from __future__ import annotations

import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import emotion_engine                      # noqa: E402
import llm                                  # noqa: E402
import response_generator                  # noqa: E402
from conversation import store              # noqa: E402

llm.load_env()

PORT = int(os.environ.get("CEREVIA_CHAT_PORT", "5001"))
BACKEND_URL = os.environ.get("CEREVIA_BACKEND_URL", "http://127.0.0.1:5000")
MAX_MESSAGE_LENGTH = 4000

_context_cache = {"at": 0.0, "value": {}}
_context_lock = threading.Lock()
CONTEXT_TTL_SECONDS = 20


def backend_context() -> dict:
    """Latest mood + emergency contact from the C++ backend, if it is running.

    The companion works fine without this; it just cannot personalise. Failures
    are cached as an empty context so a stopped backend does not add latency to
    every single chat message.
    """
    now = time.time()
    with _context_lock:
        if now - _context_cache["at"] < CONTEXT_TTL_SECONDS:
            return _context_cache["value"]

    context: dict = {}
    try:
        with urllib.request.urlopen(f"{BACKEND_URL}/api/stats/summary?days=14", timeout=1.0) as response:
            summary = json.loads(response.read().decode("utf-8"))
            context["latestMood"] = summary.get("latest", {})
            context["streakDays"] = summary.get("streakDays", 0)
            context["score"] = summary.get("score", 0)
    except (urllib.error.URLError, OSError, ValueError, TimeoutError):
        pass

    try:
        with urllib.request.urlopen(f"{BACKEND_URL}/api/profile", timeout=1.0) as response:
            profile = json.loads(response.read().decode("utf-8"))
            context["emergencyContact"] = profile.get("emergencyContact")
            context["displayName"] = profile.get("displayName")
    except (urllib.error.URLError, OSError, ValueError, TimeoutError):
        pass

    with _context_lock:
        _context_cache["at"] = now
        _context_cache["value"] = context
    return context


def build_reply(message: str, session_id: Optional[str]) -> dict:
    session = store.get(session_id)

    text = (message or "").strip()
    if len(text) > MAX_MESSAGE_LENGTH:
        text = text[:MAX_MESSAGE_LENGTH]

    if not text:
        templates = response_generator._templates()   # noqa: SLF001 - same package
        line = templates["intents"]["empty"][0]
        return {
            "sessionId": session.id,
            "emotion": "neutral",
            "risk": "none",
            "response": line,
            "technique": None,
            "actions": [],
            "analysis": {"emotion": "neutral", "risk": "none", "sentiment": 0.0, "topics": []},
        }

    # Step 1 — always analyse locally first. Safety is decided here, on this
    # machine, before any network call is even considered.
    analysis = emotion_engine.analyze(text)
    session.record_user(text, analysis.emotion, analysis.risk, analysis.topics)

    context = backend_context()

    # Step 2 — the local composer always produces a usable reply. It is the
    # answer for anything risk-bearing, and the fallback for everything else.
    result = response_generator.generate(analysis, session, context)
    source = "local"

    # Step 3 — for ordinary messages, let the model write the reply instead.
    # The technique card, the action chips and the escalation logic stay local
    # so the UI behaves identically either way.
    if analysis.risk == "none" and llm.available():
        generated = llm.generate(analysis, session, context)
        if generated:
            result = dict(result, response=generated["reply"])
            source = f"{llm.provider()}:{llm.model_name()}"
            if generated.get("technique"):
                library = response_generator.techniques()
                data = library.get(generated["technique"])
                if data and generated["technique"] not in session.offered_techniques:
                    result["technique"] = dict(data, key=generated["technique"])
                    session.offered_techniques.append(generated["technique"])
                    # The local composer may have already suggested a different
                    # exercise; drop its chip so the card and the chip agree.
                    result["actions"] = [
                        a for a in result.get("actions", []) if a.get("kind") != "technique"
                    ] + [{"label": data["title"], "href": data.get("page", "breathe.html"), "kind": "technique"}]
            elif result.get("technique"):
                # The model chose not to offer one; do not leave a stray card.
                result["technique"] = None
                result["actions"] = [a for a in result.get("actions", []) if a.get("kind") != "technique"]

    session.record_companion(result["response"], result.get("fragments", []))

    return {
        "sessionId": session.id,
        "emotion": result["emotion"],
        "risk": result["risk"],
        "response": result["response"],
        "technique": result.get("technique"),
        "actions": result.get("actions", []),
        "analysis": analysis.to_dict(),
        "turn": session.user_turns,
        "source": source,
    }


class CompanionHandler(BaseHTTPRequestHandler):
    server_version = "CereviaCompanion/2.0"

    # ---- plumbing ---------------------------------------------------------

    def _cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")

    def _send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> Optional[dict]:
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            return None
        if length <= 0:
            return {}
        if length > 1_000_000:
            return None
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return None

    def log_message(self, fmt: str, *args) -> None:
        # Default logging writes a line per request to stderr; keep it terse.
        sys.stderr.write("[chat] %s\n" % (fmt % args))

    # ---- routes -----------------------------------------------------------

    def do_OPTIONS(self) -> None:  # noqa: N802 - required name
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]

        if path in ("/health", "/"):
            self._send_json(200, {
                "status": "ok",
                "service": "cerevia-companion",
                "sessions": store.count(),
                "backendReachable": bool(backend_context()),
                "llm": llm.status(),
            })
            return

        if path == "/techniques":
            self._send_json(200, response_generator.techniques())
            return

        self._send_json(404, {"error": f"No such endpoint: GET {path}"})

    def do_POST(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        payload = self._read_json()
        if payload is None:
            self._send_json(400, {"error": "Invalid JSON body."})
            return

        if path == "/chat":
            try:
                self._send_json(200, build_reply(payload.get("message", ""), payload.get("sessionId")))
            except Exception as error:                      # noqa: BLE001 - never drop a chat
                sys.stderr.write(f"[chat] reply failed: {error}\n")
                self._send_json(500, {
                    "error": "The companion hit a problem.",
                    "response": "Something went wrong on my side. Your message did not go anywhere else — try again?",
                    "emotion": "neutral",
                    "risk": "none",
                })
            return

        if path == "/reset":
            store.reset(payload.get("sessionId"))
            self._send_json(200, {"success": True})
            return

        self._send_json(404, {"error": f"No such endpoint: POST {path}"})


def main() -> int:
    # Warm the data files so a bad JSON edit fails at start-up, not mid-chat.
    response_generator.techniques()
    emotion_engine.analyze("hello")

    if llm.available():
        print(f"[chat] Language model: {llm.provider()} / {llm.model_name()}", flush=True)
        print("[chat] Safety detection still runs locally and is never delegated.", flush=True)
    else:
        print("[chat] Language model: off — using the built-in local engine.", flush=True)

    server = ThreadingHTTPServer(("127.0.0.1", PORT), CompanionHandler)
    print(f"[chat] CEREVIA companion listening on http://127.0.0.1:{PORT}", flush=True)
    print(f"[chat] Backend context from {BACKEND_URL}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("[chat] Shutting down.", flush=True)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
