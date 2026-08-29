"""Builds the companion's reply from an analysed message plus session context.

Where the original generator picked one random line from a flat list, this one
composes a reply out of three parts -- a reflection of what was heard, a
validation, and an invitation to continue -- and refuses to reuse a fragment it
has recently said. It also decides when to offer a coping technique, when to
point at another part of the app, and when to escalate.
"""

from __future__ import annotations

import json
import os
import random
from functools import lru_cache
from typing import Dict, List, Optional, Tuple

from conversation import Session

_DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

# Topic-specific follow-ups, used instead of the generic invitation when we know
# what the conversation is actually about.
TOPIC_QUESTIONS: Dict[str, List[str]] = {
    "work": [
        "Is it the workload itself, or the people around it?",
        "What would need to change at work for this to feel manageable?",
        "Does this follow you home, or does it stay at the desk?",
    ],
    "study": [
        "Is it the exam itself, or what you think it says about you?",
        "How much of this is the deadline and how much is the expectation?",
        "What would 'good enough' look like here?",
    ],
    "family": [
        "How long has it been like this with them?",
        "What do you wish they understood?",
        "Is there a version of this conversation you could actually have with them?",
    ],
    "relationship": [
        "What do you need from them that you're not getting?",
        "Have you been able to say any of this to them?",
        "What would you want it to look like instead?",
    ],
    "friends": [
        "Is this one friendship, or a wider feeling?",
        "What would you want a friend to do differently?",
    ],
    "health": [
        "How much of the worry is about the uncertainty rather than the illness?",
        "Do you have someone helping you carry the medical side of this?",
    ],
    "sleep": [
        "What's your head usually doing at the point you can't sleep?",
        "How many nights has it been like this?",
    ],
    "money": [
        "Is this an immediate problem or a slow-building one?",
        "What's the first thing you'd fix if you could?",
    ],
    "self_image": [
        "Whose voice does that thought sound like?",
        "Would you say that sentence to someone you love?",
    ],
    "loss": [
        "Do you want to tell me about them?",
        "Grief doesn't move in a line — where are you in it today?",
    ],
    "future": [
        "What's the fear underneath the uncertainty?",
        "What would you do if you knew it would work out?",
    ],
}


@lru_cache(maxsize=1)
def _templates() -> dict:
    with open(os.path.join(_DATA_DIR, "templates.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


@lru_cache(maxsize=1)
def techniques() -> dict:
    with open(os.path.join(_DATA_DIR, "techniques.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


def _pick(options: List[str], session: Optional[Session]) -> str:
    """Chooses a line the session has not heard recently."""
    if not options:
        return ""
    if session is None:
        return random.choice(options)

    unheard = [line for line in options if line not in session.recent_fragments]
    return random.choice(unheard if unheard else options)


def _technique_for(emotion: str, session: Session) -> Optional[Tuple[str, dict]]:
    """Finds a technique suited to the emotion that has not been offered yet."""
    candidates = [
        (key, data) for key, data in techniques().items()
        if emotion in data.get("for", []) and key not in session.offered_techniques
    ]
    if not candidates:
        candidates = [
            (key, data) for key, data in techniques().items()
            if emotion in data.get("for", [])
        ]
    if not candidates:
        return None

    # A technique that lists this emotion first is the better-matched one, so
    # pick from that group before falling back to the wider set.
    best_rank = min(data["for"].index(emotion) for _, data in candidates)
    preferred = [c for c in candidates if c[1]["for"].index(emotion) == best_rank]
    return random.choice(preferred)


def _safety_reply(risk: str, emergency_contact: Optional[str]) -> str:
    safety = _templates()["safety"]
    message = safety["crisis"] if risk == "crisis" else safety["high_risk"]
    if emergency_contact and emergency_contact.strip() and emergency_contact.strip() != "112":
        message += (
            f"\n\nYou also saved {emergency_contact.strip()} as your emergency contact in CEREVIA. "
            "This is exactly what you saved it for."
        )
    return message


def _acknowledge_mood(latest_mood: Optional[dict]) -> Optional[str]:
    """One line connecting the conversation to the user's last mood check-in."""
    if not latest_mood or not latest_mood.get("hasMood"):
        return None
    mood = latest_mood.get("mood")
    if not mood:
        return None
    return f"Your last check-in was {str(mood).lower()}, so this fits with where you've been."


def generate(analysis, session: Session, context: Optional[dict] = None) -> dict:
    """Returns ``{"response", "emotion", "risk", "technique", "actions", "fragments"}``."""
    context = context or {}
    templates = _templates()
    emotion = analysis.emotion
    fragments: List[str] = []
    actions: List[dict] = []
    technique: Optional[dict] = None

    # ---- Safety first ------------------------------------------------------
    if analysis.risk in {"crisis", "high"}:
        reply = _safety_reply(analysis.risk, context.get("emergencyContact"))
        grounding = techniques().get("grounding_54321")
        if grounding:
            technique = dict(grounding, key="grounding_54321")
            session.offered_techniques.append("grounding_54321")
        actions.append({"label": "Breathing & grounding", "href": "breathe.html", "kind": "technique"})
        return {
            "response": reply,
            "emotion": "crisis" if analysis.risk == "crisis" else "deep_sad",
            "risk": analysis.risk,
            "technique": technique,
            "actions": actions,
            "fragments": [],
        }

    # ---- Conversational intents -------------------------------------------
    if emotion in {"greeting", "farewell", "thanks", "about_bot", "help_request", "affirm", "deny"}:
        key = emotion
        if key == "greeting" and not session.is_new:
            key = "greeting_returning"
        line = _pick(templates["intents"].get(key, templates["intents"]["greeting"]), session)

        if key == "greeting":
            mood_line = _acknowledge_mood(context.get("latestMood"))
            if mood_line:
                line = f"{line}\n\n{mood_line}"

        # "yes" only means something if we asked a question worth answering.
        if key == "affirm" and session.awaiting == "technique_offer":
            offer = _technique_for(session.last_user_emotion() or "anxious", session)
            if offer:
                technique_key, data = offer
                technique = dict(data, key=technique_key)
                session.offered_techniques.append(technique_key)
                line = f"Good. Here's {data['title'].lower()} — {data['subtitle'].lower()}."
                actions.append({"label": "Open the breathing room", "href": data.get("page", "breathe.html"), "kind": "technique"})
        session.awaiting = None

        fragments.append(line)
        return {
            "response": line,
            "emotion": emotion,
            "risk": "none",
            "technique": technique,
            "actions": actions,
            "fragments": fragments,
        }

    # ---- Emotional reply ---------------------------------------------------
    bank = templates["emotions"].get(emotion, templates["emotions"]["neutral"])

    reflect = _pick(bank["reflect"], session)
    validate = _pick(bank["validate"], session)
    fragments.extend([reflect, validate])

    parts = [reflect]

    # Echo back the specific thing they named, so the reply is about *their*
    # situation rather than the emotion category.
    if analysis.reflection and len(analysis.reflection.split()) >= 2:
        parts.append(f"And it's about {analysis.reflection}.")

    parts.append(validate)

    # Choose the closing question: topic-aware when we know the subject.
    invite = None
    topic = analysis.topics[0] if analysis.topics else None
    if topic is None:
        # Only carry a topic over from earlier turns once it has come up more
        # than once, so one passing mention does not steer every later reply.
        carried = session.dominant_topic()
        if carried and session.topics_seen.get(carried, 0) >= 2:
            topic = carried
    if topic and topic in TOPIC_QUESTIONS and random.random() < 0.65:
        invite = _pick(TOPIC_QUESTIONS[topic], session)
    if not invite:
        invite = _pick(bank["invite"], session)

    streak = session.negative_streak()

    # ---- Escalation --------------------------------------------------------
    # Three distressed turns in a row is the point where "tell me more" stops
    # being enough and the companion should point outward.
    if analysis.is_negative and streak >= 3 and not session.escalation_sent:
        escalation = _pick(templates["escalation"]["repeated_low"], session)
        parts.append(escalation)
        fragments.append(escalation)
        session.escalation_sent = True
        actions.append({"label": "Log this as a check-in", "href": "mood.html"})
        actions.append({"label": "Write it out", "href": "journal.html"})
    else:
        parts.append(invite)
        fragments.append(invite)

    # ---- Technique offer ---------------------------------------------------
    # Offered from the second distressed turn onwards, so the first reply is
    # listening rather than problem-solving.
    should_offer = (
        analysis.is_negative
        and streak >= 2
        and emotion in {"anxious", "overwhelmed", "angry", "exhausted", "deep_sad", "mild_sad", "lonely", "confused"}
        and len(session.offered_techniques) < 3
    )
    if analysis.intent == "help_request":
        should_offer = True

    if should_offer:
        offer = _technique_for(emotion, session)
        if offer:
            technique_key, data = offer
            technique = dict(data, key=technique_key)
            session.offered_techniques.append(technique_key)
            actions.append({"label": data["title"], "href": data.get("page", "breathe.html"), "kind": "technique"})

    # ---- Gentle nudges toward the rest of the app --------------------------
    if analysis.is_positive and session.user_turns >= 2 and not actions:
        actions.append({"label": "Log this feeling", "href": "mood.html"})
    if emotion in {"deep_sad", "confused"} and session.user_turns >= 4 and len(actions) < 2:
        actions.append({"label": "Write it out", "href": "journal.html"})

    session.awaiting = "technique_offer" if technique else None

    reply = " ".join(part.strip() for part in parts if part and part.strip())
    return {
        "response": reply,
        "emotion": emotion,
        "risk": analysis.risk,
        "technique": technique,
        "actions": actions,
        "fragments": fragments,
    }


def generate_response(emotion: str) -> str:
    """Backwards-compatible entry point: one line for an emotion, no context."""
    templates = _templates()
    if emotion in templates["intents"]:
        return random.choice(templates["intents"][emotion])
    bank = templates["emotions"].get(emotion, templates["emotions"]["neutral"])
    return f"{random.choice(bank['reflect'])} {random.choice(bank['invite'])}"
