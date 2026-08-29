"""Optional large-language-model backing for the CEREVIA companion.

The companion works entirely offline with the lexicon engine. When a provider is
configured it is used for the *conversational* reply only — everything about
safety stays local:

    1. `emotion_engine.analyze()` runs first, on this machine, every time.
    2. If it detects crisis or high risk, the deterministic local safety text is
       returned and the model is never called. A network failure, a rate limit
       or a badly-judged generation can therefore never suppress a safety
       response.
    3. Only for ordinary messages is the model asked for a reply.
    4. The model's own output is then re-checked with the same local detector,
       and discarded if it trips.
    5. Any error, timeout or empty result falls back to the local composer, so
       the chat never dies.

Configure with environment variables (or a .env file in the project root):

    CEREVIA_LLM_PROVIDER=gemini
    CEREVIA_LLM_KEY=<your key>          # or `gemini_api=` in .env
    CEREVIA_LLM_MODEL=gemini-3.5-flash  # optional
"""

from __future__ import annotations

import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from typing import Dict, List, Optional

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)

DEFAULT_MODEL = "gemini-3.5-flash"
REQUEST_TIMEOUT = float(os.environ.get("CEREVIA_LLM_TIMEOUT", "12"))

# Remembered so /health and the UI can say which brain is answering.
_state = {"lastError": None, "calls": 0, "failures": 0, "lastLatencyMs": 0, "cooldownUntil": 0}
_lock = threading.Lock()

# The free tier is limited per minute. When it says no, stop asking until the
# window it names has passed — a rate-limited retry storm just makes every reply
# slow, and the local engine is already a good answer.
DEFAULT_COOLDOWN_SECONDS = 45


# --------------------------------------------------------------------------
# Configuration
# --------------------------------------------------------------------------

def load_env(path: Optional[str] = None) -> None:
    """Reads a .env file into os.environ without overwriting real env vars."""
    path = path or os.path.join(_ROOT, ".env")
    if not os.path.isfile(path):
        return
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                name, value = line.split("=", 1)
                name = name.strip()
                value = value.strip().strip('"').strip("'")
                if name and name not in os.environ:
                    os.environ[name] = value
    except OSError as error:
        sys.stderr.write(f"[chat] could not read {path}: {error}\n")


def api_key() -> Optional[str]:
    # CEREVIA_LLM_KEY is the documented name; the others are accepted so an
    # existing .env written for another tool still works.
    for name in ("CEREVIA_LLM_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY", "gemini_api"):
        value = os.environ.get(name)
        if value:
            return value.strip()
    return None


def provider() -> str:
    explicit = (os.environ.get("CEREVIA_LLM_PROVIDER") or "").strip().lower()
    if explicit in {"none", "off", "local"}:
        return "local"
    if explicit:
        return explicit
    # A key present with no provider named means "use it".
    return "gemini" if api_key() else "local"


def model_name() -> str:
    return (os.environ.get("CEREVIA_LLM_MODEL") or DEFAULT_MODEL).strip()


def available() -> bool:
    return provider() == "gemini" and bool(api_key())


def cooling_down() -> bool:
    with _lock:
        return time.time() < _state["cooldownUntil"]


def status() -> dict:
    with _lock:
        remaining = max(0, int(_state["cooldownUntil"] - time.time()))
    return {
        "provider": provider(),
        "model": model_name() if available() else None,
        "enabled": available(),
        "rateLimited": remaining > 0,
        "cooldownSeconds": remaining,
        **{k: v for k, v in _state.items() if k != "cooldownUntil"},
    }


# --------------------------------------------------------------------------
# Prompting
# --------------------------------------------------------------------------

SYSTEM_PROMPT = """You are the companion inside CEREVIA, a private mental-health app that runs on the user's own computer.

WHO YOU ARE
- A warm, grounded, plain-spoken listener. Not a therapist, not a doctor, not a coach.
- You never diagnose, never name conditions, never suggest medication or dosages.
- You never claim to be human. If asked, say plainly that you are a program.

HOW YOU TALK
- 2 to 4 sentences. Short. No bullet points, no headings, no markdown, no emoji.
- Reflect back the specific thing they said, in their own terms, before anything else.
- Ask at most ONE question, and only when it genuinely helps them keep going.
- Plain modern English. Contractions are fine.

WHAT TO AVOID
- Opening with "I'm sorry to hear that" or "That sounds really tough" — they are worn out.
- Toxic positivity, silver linings, or telling them what they should feel.
- Advice stacks. One idea at a time, if any.
- Repeating a phrasing you have already used in this conversation.
- Rushing to fix. The first reply after someone opens up should mostly be listening.

USING THE CONTEXT YOU ARE GIVEN
- `detectedEmotion` and `topics` come from a local analysis of their message. Treat them
  as a strong hint, not gospel — the words the person actually wrote matter more.
- `recentMood` is their most recent mood check-in inside the app. Only mention it if it
  genuinely adds something.
- `turn` tells you how deep into the conversation you are. Early turns listen; later
  turns may gently offer something concrete.

OFFERING A TECHNIQUE
- You may set `technique` to ONE key from the list you are given, or null.
- Offer one only from the second distressed message onwards, and only if it fits.
- Never offer a technique in the same breath as dismissing what they said.

SAFETY
- Messages containing self-harm or suicidal intent are intercepted before they reach you
  and answered by a fixed local safety response, so you will not normally see them.
- If one reaches you anyway, do not counsel. Say clearly that this needs a real person now,
  and name a helpline. Do nothing else.

Reply as JSON matching the schema. `reply` is what the person reads."""


TECHNIQUE_KEYS = [
    "box_breathing", "four_seven_eight", "grounding_54321", "name_it_to_tame_it",
    "smallest_next_step", "thought_check", "self_compassion_break",
    "physical_reset", "one_good_thing", "wind_down",
]

RESPONSE_SCHEMA = {
    "type": "OBJECT",
    "properties": {
        "reply": {"type": "STRING"},
        "technique": {"type": "STRING"},
        "emotion": {"type": "STRING"},
    },
    "required": ["reply"],
}


def _history(session, limit: int = 10) -> List[dict]:
    """The last few turns, in the shape the Gemini API expects."""
    turns = [t for t in session.turns][-limit:]
    contents = []
    for turn in turns:
        contents.append({
            "role": "user" if turn.role == "user" else "model",
            "parts": [{"text": turn.text}],
        })
    return contents


def _context_block(analysis, session, context: dict) -> str:
    latest = (context or {}).get("latestMood") or {}
    payload = {
        "detectedEmotion": analysis.emotion,
        "sentiment": round(analysis.sentiment, 2),
        "topics": analysis.topics,
        "turn": session.user_turns,
        "consecutiveDistressedTurns": session.negative_streak(),
        "techniquesAlreadyOffered": session.offered_techniques,
        "availableTechniques": TECHNIQUE_KEYS,
    }
    if latest.get("hasMood"):
        payload["recentMood"] = {
            "mood": latest.get("mood"),
            "intensity": latest.get("level"),
            "when": latest.get("date"),
        }
    if (context or {}).get("displayName") and context["displayName"] != "friend":
        payload["theirName"] = context["displayName"]
    return json.dumps(payload, ensure_ascii=False)


# --------------------------------------------------------------------------
# Generation
# --------------------------------------------------------------------------

def _call_gemini(contents: List[dict], system: str) -> Optional[dict]:
    key = api_key()
    if not key:
        return None

    url = (f"https://generativelanguage.googleapis.com/v1beta/models/"
           f"{model_name()}:generateContent?key={key}")

    body = {
        "systemInstruction": {"parts": [{"text": system}]},
        "contents": contents,
        "generationConfig": {
            "temperature": 0.9,
            "topP": 0.95,
            "maxOutputTokens": 1024,
            "responseMimeType": "application/json",
            "responseSchema": RESPONSE_SCHEMA,
            # Gemini 3.x spends "thinking" tokens out of the same budget. Left
            # on, ~600 of them went on a two-sentence reply, truncating the JSON
            # mid-string. This is a short empathic turn, not a reasoning task.
            "thinkingConfig": {"thinkingBudget": 0},
        },
        # The app has its own safety layer and needs to be able to discuss
        # distress frankly; blocking mid-conversation would be worse than
        # unhelpful. Self-harm content never reaches this call.
        "safetySettings": [
            {"category": c, "threshold": "BLOCK_ONLY_HIGH"}
            for c in ("HARM_CATEGORY_HARASSMENT", "HARM_CATEGORY_HATE_SPEECH",
                      "HARM_CATEGORY_SEXUALLY_EXPLICIT", "HARM_CATEGORY_DANGEROUS_CONTENT")
        ],
    }

    request = urllib.request.Request(
        url,
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )

    started = time.time()
    with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT) as response:
        data = json.loads(response.read().decode("utf-8"))

    with _lock:
        _state["lastLatencyMs"] = int((time.time() - started) * 1000)

    candidates = data.get("candidates") or []
    if not candidates:
        return None

    # Anything other than a clean stop means the payload is partial — a
    # half-written JSON object, or a blocked generation. Fall back rather than
    # showing the user the wreckage.
    finish = candidates[0].get("finishReason")
    if finish and finish != "STOP":
        raise ValueError(f"finishReason={finish}")

    parts = candidates[0].get("content", {}).get("parts") or []
    if not parts:
        return None

    text = (parts[0].get("text") or "").strip()
    try:
        return json.loads(text)
    except ValueError:
        # Never surface raw JSON to the user. If it did not parse, it is
        # truncated output, not a plain-text reply.
        if text.startswith("{") or text.startswith("["):
            raise ValueError("truncated JSON from the model")
        return {"reply": text}


def generate(analysis, session, context: Optional[dict] = None) -> Optional[dict]:
    """Returns ``{"reply", "technique"}`` from the model, or None to fall back."""
    if not available():
        return None

    # Still rate limited from a previous refusal — answer locally without
    # spending a request.
    if cooling_down():
        return None

    contents = _history(session)
    # The context rides on the newest user turn so the model sees it in place.
    if contents and contents[-1]["role"] == "user":
        contents[-1]["parts"].append({"text": f"[local analysis, not written by the user]\n{_context_block(analysis, session, context or {})}"})
    else:
        contents.append({"role": "user", "parts": [{"text": analysis.text}]})

    with _lock:
        _state["calls"] += 1

    try:
        result = _call_gemini(contents, SYSTEM_PROMPT)
    except urllib.error.HTTPError as error:
        payload = error.read().decode("utf-8", "replace")
        if error.code == 429:
            _start_cooldown(_retry_delay(payload))
            _fail(f"rate limited; using the local engine for {DEFAULT_COOLDOWN_SECONDS}s")
        else:
            _fail(f"HTTP {error.code}: {payload[:200]}")
        return None
    except (urllib.error.URLError, OSError, TimeoutError) as error:
        _fail(f"network: {error}")
        return None
    except ValueError as error:
        _fail(str(error))
        return None
    except Exception as error:                       # noqa: BLE001 - never break chat
        _fail(f"unexpected: {error}")
        return None

    if not result or not str(result.get("reply", "")).strip():
        _fail("empty response")
        return None

    reply = str(result["reply"]).strip()

    # Re-run the local detector over the model's own words. If the generated
    # text itself reads as crisis content, throw it away and let the caller
    # fall back to the vetted local path.
    from emotion_engine import analyze as local_analyze
    if local_analyze(reply).risk != "none":
        _fail("model output tripped the local safety check")
        return None

    technique = result.get("technique")
    if technique not in TECHNIQUE_KEYS:
        technique = None

    with _lock:
        _state["lastError"] = None

    return {"reply": reply, "technique": technique}


def _retry_delay(payload: str) -> float:
    """Reads the retryDelay Google returns with a 429, when it sends one."""
    try:
        for detail in json.loads(payload).get("error", {}).get("details", []):
            delay = detail.get("retryDelay")
            if isinstance(delay, str) and delay.endswith("s"):
                return float(delay[:-1])
    except (ValueError, TypeError, KeyError):
        pass
    return DEFAULT_COOLDOWN_SECONDS


def _start_cooldown(seconds: float) -> None:
    with _lock:
        _state["cooldownUntil"] = time.time() + max(5.0, min(seconds, 300.0))


def _fail(message: str) -> None:
    with _lock:
        _state["failures"] += 1
        _state["lastError"] = message
    sys.stderr.write(f"[chat] llm fallback — {message}\n")
