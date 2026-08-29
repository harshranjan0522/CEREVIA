"""Per-session conversation memory.

The original chatbot was stateless: every message was scored in isolation, so
it could greet you five times in a row and never notice you had said the same
painful thing three times. This module keeps a small, bounded, in-memory record
of each conversation so replies can build on what came before.

Nothing is written to disk and nothing leaves the machine. Sessions expire
after a period of inactivity.
"""

from __future__ import annotations

import threading
import time
import uuid
from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Dict, List, Optional

SESSION_TTL_SECONDS = 60 * 60 * 4      # four hours of inactivity
MAX_SESSIONS = 200
HISTORY_LIMIT = 40


@dataclass
class Turn:
    role: str                # "user" or "companion"
    text: str
    emotion: str = "neutral"
    risk: str = "none"
    at: float = field(default_factory=time.time)


@dataclass
class Session:
    id: str
    created_at: float = field(default_factory=time.time)
    last_seen: float = field(default_factory=time.time)
    turns: Deque[Turn] = field(default_factory=lambda: deque(maxlen=HISTORY_LIMIT))

    # Phrases already used, so the companion does not repeat itself verbatim.
    recent_fragments: Deque[str] = field(default_factory=lambda: deque(maxlen=24))

    # Techniques already offered, so it does not push the same exercise twice.
    offered_techniques: List[str] = field(default_factory=list)

    topics_seen: Dict[str, int] = field(default_factory=dict)
    emotion_counts: Dict[str, int] = field(default_factory=dict)
    crisis_flagged: bool = False
    escalation_sent: bool = False
    awaiting: Optional[str] = None   # e.g. "technique_offer" — what a yes/no answers

    @property
    def user_turns(self) -> int:
        return sum(1 for turn in self.turns if turn.role == "user")

    @property
    def is_new(self) -> bool:
        return self.user_turns <= 1

    def last_user_emotion(self) -> Optional[str]:
        for turn in reversed(self.turns):
            if turn.role == "user":
                return turn.emotion
        return None

    def recent_user_emotions(self, count: int = 3) -> List[str]:
        emotions = [t.emotion for t in self.turns if t.role == "user"]
        return emotions[-count:]

    def negative_streak(self) -> int:
        """How many of the most recent user turns in a row were distressed."""
        negative = {"deep_sad", "mild_sad", "anxious", "angry",
                    "overwhelmed", "exhausted", "lonely", "crisis"}
        streak = 0
        for turn in reversed(self.turns):
            if turn.role != "user":
                continue
            if turn.emotion in negative:
                streak += 1
            else:
                break
        return streak

    def record_user(self, text: str, emotion: str, risk: str, topics: List[str]) -> None:
        self.turns.append(Turn("user", text, emotion, risk))
        self.emotion_counts[emotion] = self.emotion_counts.get(emotion, 0) + 1
        for topic in topics:
            self.topics_seen[topic] = self.topics_seen.get(topic, 0) + 1
        if risk in {"crisis", "high"}:
            self.crisis_flagged = True
        self.last_seen = time.time()

    def record_companion(self, text: str, fragments: List[str]) -> None:
        self.turns.append(Turn("companion", text))
        for fragment in fragments:
            self.recent_fragments.append(fragment)
        self.last_seen = time.time()

    def dominant_topic(self) -> Optional[str]:
        if not self.topics_seen:
            return None
        return max(self.topics_seen.items(), key=lambda item: item[1])[0]

    def summary(self) -> dict:
        return {
            "id": self.id,
            "turns": self.user_turns,
            "emotions": dict(self.emotion_counts),
            "topics": dict(self.topics_seen),
            "negativeStreak": self.negative_streak(),
            "crisisFlagged": self.crisis_flagged,
        }


class SessionStore:
    """Thread-safe session registry with TTL and a hard size cap."""

    def __init__(self) -> None:
        self._sessions: Dict[str, Session] = {}
        self._lock = threading.Lock()

    def get(self, session_id: Optional[str]) -> Session:
        now = time.time()
        with self._lock:
            self._expire(now)

            if session_id and session_id in self._sessions:
                session = self._sessions[session_id]
                session.last_seen = now
                return session

            # An unknown id from the client is honoured rather than replaced, so
            # a page reload keeps its conversation as long as the server is up.
            new_id = session_id if session_id and len(session_id) <= 64 else uuid.uuid4().hex
            session = Session(id=new_id)
            self._sessions[new_id] = session

            if len(self._sessions) > MAX_SESSIONS:
                oldest = min(self._sessions.values(), key=lambda s: s.last_seen)
                self._sessions.pop(oldest.id, None)
            return session

    def reset(self, session_id: Optional[str]) -> None:
        if not session_id:
            return
        with self._lock:
            self._sessions.pop(session_id, None)

    def count(self) -> int:
        with self._lock:
            self._expire(time.time())
            return len(self._sessions)

    def _expire(self, now: float) -> None:
        stale = [sid for sid, s in self._sessions.items() if now - s.last_seen > SESSION_TTL_SECONDS]
        for sid in stale:
            self._sessions.pop(sid, None)


store = SessionStore()
