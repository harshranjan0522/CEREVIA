"""Intent, emotion, risk and topic detection for the CEREVIA companion.

The previous version matched keywords with ``word in text``, so any substring
counted. "I am not happy **thi**s week" was classified as a greeting because
"hi" appears inside "this"; "I'm worried a**but** money" tripped the mixed
emotion branch. Every match here is anchored to word boundaries, and multi-word
phrases are matched as phrases.

Detection runs as an explicit priority ladder -- safety first, then explicit
emotional content, then sentiment, then conversational intents -- so a message
that mentions self-harm is never answered as small talk.
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from functools import lru_cache
from typing import Dict, List, Optional, Tuple

_DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")


# --- Risk -------------------------------------------------------------------
# Split into two tiers. CRISIS means an explicit statement of intent to end
# one's life or cause serious self-harm. HIGH_RISK covers passive ideation and
# hopelessness, which needs a gentler but still safety-forward response.

CRISIS_PHRASES = [
    "kill myself", "killing myself", "end my life", "ending my life", "take my own life",
    "commit suicide", "suicide", "suicidal", "want to die", "wanna die", "wish i was dead",
    "wish i were dead", "better off dead", "ending it all", "end it all",
    "hang myself", "shoot myself", "poison myself", "overdose", "od on",
    "jump off", "jump in front", "slit my wrists", "cut myself", "cutting myself",
    "self harm", "self-harm", "hurt myself", "hurting myself", "harm myself",
    "harming myself", "burn myself", "burning myself", "injure myself",
    "not be here anymore", "not want to be here", "don't want to be alive",
    "dont want to be alive", "don't want to live", "dont want to live",
    "no reason to live", "nothing to live for", "goodbye forever",
]

HIGH_RISK_PHRASES = [
    "life is not worth living", "life isn't worth living", "life isnt worth living",
    "i can't go on", "i cant go on", "can't go on anymore", "cant go on anymore",
    "i'm done with life", "im done with life", "done with everything",
    "everyone would be better without me", "nobody would miss me",
    "no one would miss me", "i am a burden", "i'm a burden", "im a burden",
    "like a burden", "such a burden", "burden to everyone", "burden on everyone",
    "better off without me",
    "disappear forever", "want to disappear", "stop existing",
    "give up on life", "nothing matters anymore", "there's no point",
    "theres no point", "no point anymore", "what's the point of living",
]

# --- Emotions ---------------------------------------------------------------

EMOTION_TERMS: Dict[str, List[str]] = {
    "deep_sad": [
        "hopeless", "worthless", "meaningless", "empty", "numb", "broken", "shattered",
        "devastated", "depressed", "depression", "despair", "grief", "grieving",
        "hate myself", "hating myself", "feel useless", "useless", "i give up",
        "pointless", "no point", "nothing matters", "why bother",
        "no one cares", "nobody cares", "feel invisible", "dead inside",
        "drowning in", "unbearable", "can't stop crying", "cant stop crying",
        "sobbing", "cried all", "falling apart", "lost everything",
    ],
    "mild_sad": [
        # Deliberately phrase-based rather than single generic words: a bare
        # "down" or "low" matches "calm down" and "low battery".
        "sad", "unhappy", "feeling down", "feel down", "felt down", "feeling low",
        "feel low", "feeling blue", "upset", "disappointed", "hurt", "crying",
        "cried", "tearful", "i miss", "missing him", "missing her", "missing them",
        "lonely", "left out", "rejected", "gloomy", "heartbroken", "let down",
        "bummed", "down in the dumps", "failing at", "not good enough",
        "letting everyone down", "letting people down", "can't get anything right",
        "nothing i do", "not enough", "messed up", "screwed up",
    ],
    "anxious": [
        "anxious", "anxiety", "nervous", "panic", "panicking", "panic attack",
        "worried", "worry", "worrying", "scared", "afraid", "fear", "terrified",
        "overthinking", "overthink", "restless", "uneasy", "tense", "on edge",
        "heart racing", "chest tight", "can't breathe", "cant breathe", "shaking",
        "paranoid", "dread", "what if", "catastrophis", "catastrophiz",
        "mind racing", "can't relax", "cant relax", "spiralling", "spiraling",
        "can't stop thinking", "keep thinking about", "keeps going round",
        "going over it", "going round in my head", "in my head all",
        "playing on my mind", "on my mind all", "obsessing", "ruminating",
        "can't switch off", "can't get it out of my head", "worst case",
        "worst-case", "wide awake", "up all night", "keep replaying",
        "dreading", "nervous about", "butterflies", "knot in my stomach",
    ],
    "angry": [
        "angry", "furious", "rage", "raging", "enraged", "mad at", "pissed",
        "annoyed", "irritated", "frustrated", "frustrating", "fed up", "resentful",
        "bitter", "hate them", "unfair", "betrayed", "disrespected", "sick of",
    ],
    "overwhelmed": [
        "overwhelmed", "too much", "can't cope", "cant cope", "can't handle",
        "cant handle", "drowning", "buried", "no time", "everything at once",
        "piling up", "spread thin", "at my limit", "breaking point", "swamped",
        "piling on", "piling things on", "keeps piling", "too many things",
        "can't say no", "back to back", "no let up", "juggling", "stretched thin",
        "one thing after another", "never stops", "don't know where to start",
    ],
    "exhausted": [
        "exhausted", "drained", "burnt out", "burned out", "burnout", "worn out",
        "tired", "no energy", "running on empty", "can't sleep", "cant sleep",
        "insomnia", "sleepless", "haven't slept", "havent slept", "wiped out",
    ],
    "lonely": [
        "lonely", "loneliness", "no friends", "no one to talk", "nobody to talk",
        "isolated", "left out", "on my own", "by myself all", "abandoned",
        "no one understands", "nobody understands", "disconnected",
    ],
    "calm": [
        "calm", "peaceful", "relaxed", "at ease", "settled", "grounded", "centred",
        "centered", "content", "steady", "rested", "relieved", "lighter",
    ],
    "grateful": [
        "grateful", "thankful", "blessed", "appreciate", "appreciative",
        "means a lot", "lucky to have", "counting my blessings",
    ],
    "happy": [
        "happy", "glad", "cheerful", "good day", "went well", "feeling good",
        "smiling", "pleased", "positive", "hopeful", "optimistic", "motivated",
        "proud", "confident", "accomplished",
    ],
    "excited": [
        "excited", "thrilled", "can't wait", "cant wait", "over the moon",
        "ecstatic", "buzzing", "amazing news", "best day", "dream come true",
        "so happy", "overjoyed", "delighted",
    ],
    "confused": [
        "confused", "don't know", "dont know", "not sure", "unsure", "lost",
        "no idea", "can't decide", "cant decide", "torn between", "mixed feelings",
        "conflicted", "don't understand", "dont understand",
    ],
}

# --- Conversational intents -------------------------------------------------

INTENT_TERMS: Dict[str, List[str]] = {
    "greeting": [
        "hello", "hi", "hii", "hiii", "hey", "heyy", "helloo", "hlo", "hlw",
        "yo", "hiya", "howdy", "hola", "namaste", "good morning", "good evening",
        "good afternoon", "hi there", "hey there", "what's up", "whats up",
        "wassup", "sup", "greetings", "hello again",
    ],
    "farewell": [
        "bye", "byebye", "bye bye", "goodbye", "good bye", "see you", "see ya",
        "cya", "talk later", "talk to you later", "ttyl", "catch you later",
        "gotta go", "got to go", "gtg", "logging off", "signing off", "farewell",
        "good night", "goodnight", "gn", "peace out", "i'm leaving", "im leaving",
    ],
    "thanks": [
        "thank you", "thanks", "thankyou", "thx", "ty", "appreciate it",
        "that helps", "that helped", "grateful for this", "much appreciated",
    ],
    "about_bot": [
        "who are you", "what are you", "are you a bot", "are you real",
        "are you human", "are you an ai", "what can you do", "how do you work",
        "your name", "who made you",
    ],
    "affirm": ["yes", "yeah", "yep", "yup", "sure", "okay", "ok", "alright", "please do", "go on", "definitely"],
    "deny": ["no", "nope", "nah", "not really", "not now", "rather not", "no thanks", "maybe later"],
    "help_request": [
        "help me", "i need help", "what should i do", "what do i do", "any advice",
        "can you help", "give me advice", "how do i cope", "how do i deal",
        "calm me down", "help me calm", "i need to calm", "calm down",
        "help me relax", "talk me through", "what can i do",
    ],
}

# --- Topics -----------------------------------------------------------------

TOPIC_TERMS: Dict[str, List[str]] = {
    "work": ["work", "job", "boss", "manager", "colleague", "coworker", "office",
             "career", "promotion", "fired", "laid off", "interview", "deadline",
             "shift", "overtime", "workload", "meeting"],
    "study": ["exam", "exams", "test", "grades", "grade", "school", "college",
              "university", "class", "classes", "assignment", "homework", "thesis",
              "semester", "professor", "teacher", "study", "studying", "results"],
    "family": ["mom", "mum", "mother", "dad", "father", "parents", "brother",
               "sister", "sibling", "family", "grandma", "grandpa", "son", "daughter",
               "aunt", "uncle", "cousin", "home life"],
    "relationship": ["boyfriend", "girlfriend", "partner", "husband", "wife",
                     "spouse", "breakup", "break up", "broke up", "divorce",
                     "relationship", "dating", "ex", "crush", "marriage"],
    "friends": ["friend", "friends", "friendship", "best friend", "mate", "group chat"],
    "health": ["sick", "illness", "diagnosis", "hospital", "doctor", "pain",
               "chronic", "medication", "meds", "therapy", "therapist", "surgery",
               "symptoms", "recovery"],
    "sleep": ["sleep", "sleeping", "insomnia", "nightmare", "nightmares", "awake",
              "tired", "bed", "rest", "napping", "3am", "can't sleep", "cant sleep"],
    "money": ["money", "rent", "bills", "debt", "loan", "salary", "broke",
              "afford", "expenses", "finances", "financial", "savings"],
    "self_image": ["ugly", "fat", "hate my body", "my body", "not good enough",
                   "compare myself", "comparing myself", "insecure", "self esteem",
                   "self-esteem", "confidence", "failure", "loser"],
    "loss": ["died", "death", "passed away", "funeral", "grief", "grieving",
             "lost my", "miss him", "miss her", "miss them", "anniversary of"],
    "future": ["future", "what if", "next year", "my life is going", "purpose",
               "direction", "where i'm going", "where im going"],
}


@lru_cache(maxsize=1)
def _templates() -> dict:
    with open(os.path.join(_DATA_DIR, "templates.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


def _compile(terms: List[str]) -> List[Tuple[str, re.Pattern]]:
    """Builds word-boundary patterns, handling multi-word phrases correctly."""
    compiled = []
    for term in terms:
        escaped = re.escape(term).replace(r"\ ", r"\s+")

        # Apostrophes are optional, so a term written "can't sleep" also matches
        # the very common "cant sleep". (re.escape stopped escaping apostrophes
        # in Python 3.7, so this has to operate on the bare character.)
        escaped = escaped.replace("'", "'?")

        # The same pattern accepts the spelled-out "cannot".
        escaped = escaped.replace("can'?t", "can(?:no|')?t")
        compiled.append((term, re.compile(rf"(?<!\w){escaped}(?!\w)", re.IGNORECASE)))
    return compiled


@lru_cache(maxsize=None)
def _patterns_for(group: str, key: str) -> Tuple[Tuple[str, re.Pattern], ...]:
    source = {
        "emotion": EMOTION_TERMS,
        "intent": INTENT_TERMS,
        "topic": TOPIC_TERMS,
    }[group]
    return tuple(_compile(source[key]))


@lru_cache(maxsize=1)
def _crisis_patterns() -> Tuple[Tuple[str, re.Pattern], ...]:
    return tuple(_compile(CRISIS_PHRASES))


@lru_cache(maxsize=1)
def _high_risk_patterns() -> Tuple[Tuple[str, re.Pattern], ...]:
    return tuple(_compile(HIGH_RISK_PHRASES))


# Words that flip the meaning of an emotion term that follows them.
_NEGATORS = {
    "not", "no", "never", "cant", "can't", "cannot", "dont", "don't", "doesnt",
    "doesn't", "didnt", "didn't", "isnt", "isn't", "arent", "aren't", "wasnt",
    "wasn't", "werent", "weren't", "wont", "won't", "wouldnt", "wouldn't",
    "hardly", "barely", "stopped", "stop", "less", "nothing", "nobody",
}

# How many words before a match are checked for a negator.
_NEGATION_LOOKBACK = 3

_WORD_RE = re.compile(r"[a-z0-9']+", re.IGNORECASE)


def _is_negated(text: str, match_start: int) -> bool:
    """True when a negator sits just before the match.

    Without this, "I am not happy this week" scores as `happy`, because the
    word `happy` is genuinely present. Emotion terms are only counted when
    nothing immediately before them reverses their meaning.
    """
    preceding = _WORD_RE.findall(text[:match_start].lower())
    return any(word in _NEGATORS for word in preceding[-_NEGATION_LOOKBACK:])


def _matches(patterns, text: str) -> List[str]:
    hits = []
    for term, pattern in patterns:
        # A term that already contains its own negation ("can't sleep") must
        # not be filtered out by the negator sitting inside it.
        self_negating = any(word in _NEGATORS for word in _WORD_RE.findall(term.lower()))
        for match in pattern.finditer(text):
            if self_negating or not _is_negated(text, match.start()):
                hits.append(term)
                break
    return hits


@dataclass
class Analysis:
    """Everything the response generator needs to know about one message."""

    text: str
    emotion: str = "neutral"
    intent: Optional[str] = None
    risk: str = "none"           # none | high | crisis
    sentiment: float = 0.0
    topics: List[str] = field(default_factory=list)
    matched_terms: List[str] = field(default_factory=list)
    reflection: Optional[str] = None
    is_question: bool = False
    is_short: bool = False
    word_count: int = 0

    @property
    def is_negative(self) -> bool:
        return self.emotion in {
            "deep_sad", "mild_sad", "anxious", "angry",
            "overwhelmed", "exhausted", "lonely",
        } or self.risk != "none"

    @property
    def is_positive(self) -> bool:
        return self.emotion in {"happy", "excited", "grateful", "calm"}

    def to_dict(self) -> dict:
        return {
            "emotion": self.emotion,
            "intent": self.intent,
            "risk": self.risk,
            "sentiment": self.sentiment,
            "topics": self.topics,
            "matched": self.matched_terms[:6],
        }


# Pulls the object of a feeling out of the sentence so the reply can name it
# back to the user: "I'm anxious about my exam" -> "your exam".
_REFLECTION_PATTERNS = [
    re.compile(r"\b(?:i(?:'m| am)|i feel|feeling|im)\s+\w+\s+(?:about|because of|over|with|by)\s+(?P<object>[^.,!?;]{3,60})", re.IGNORECASE),
    re.compile(r"\b(?:worried|stressed|anxious|upset|angry|sad|excited|happy)\s+(?:about|over|because of)\s+(?P<object>[^.,!?;]{3,60})", re.IGNORECASE),
    re.compile(r"\bbecause\s+(?P<object>[^.,!?;]{3,60})", re.IGNORECASE),
]

_PRONOUN_SWAPS = [
    (re.compile(r"\bmy\b", re.IGNORECASE), "your"),
    (re.compile(r"\bmine\b", re.IGNORECASE), "yours"),
    (re.compile(r"\bme\b", re.IGNORECASE), "you"),
    (re.compile(r"\bi\s+am\b", re.IGNORECASE), "you are"),
    (re.compile(r"\bi'm\b", re.IGNORECASE), "you are"),
    (re.compile(r"\bi\b", re.IGNORECASE), "you"),
]


def _extract_reflection(text: str) -> Optional[str]:
    for pattern in _REFLECTION_PATTERNS:
        match = pattern.search(text)
        if not match:
            continue
        phrase = match.group("object").strip()
        if len(phrase.split()) > 12 or len(phrase) < 3:
            continue
        for swap, replacement in _PRONOUN_SWAPS:
            phrase = swap.sub(replacement, phrase)
        return phrase.strip().rstrip(".")
    return None


def detect_topics(text: str) -> List[str]:
    found = []
    for topic in TOPIC_TERMS:
        if _matches(_patterns_for("topic", topic), text):
            found.append(topic)
    return found


def _strip_matches(text: str, patterns) -> str:
    """Blanks out matched phrases so they cannot also register as emotion words.

    "can you help me calm down" is a request for help, not a report of calm and
    sadness; removing the request phrasing keeps the emotion pass honest.
    """
    cleaned = text
    for _, pattern in patterns:
        cleaned = pattern.sub(" ", cleaned)
    return cleaned


def detect_intent(text: str, word_count: int) -> Optional[str]:
    """Conversational intents only apply when they are the whole message.

    "hey" is a greeting; "hey, I have been struggling all week" is not, so
    short-form intents are only honoured for brief messages.
    """
    for intent in ("about_bot", "help_request", "thanks"):
        if _matches(_patterns_for("intent", intent), text):
            return intent

    if word_count <= 6:
        for intent in ("greeting", "farewell"):
            if _matches(_patterns_for("intent", intent), text):
                return intent
    if word_count <= 4:
        for intent in ("affirm", "deny"):
            if _matches(_patterns_for("intent", intent), text):
                return intent
    return None


def analyze(text: str, sentiment: Optional[float] = None) -> Analysis:
    """Full analysis of one user message."""
    from sentiment_model import analyze_sentiment  # local import keeps modules independent

    raw = (text or "").strip()
    result = Analysis(text=raw)
    if not raw:
        return result

    result.word_count = len(raw.split())
    result.is_short = result.word_count <= 3
    result.is_question = raw.rstrip().endswith("?")
    result.sentiment = analyze_sentiment(raw) if sentiment is None else sentiment
    result.topics = detect_topics(raw)
    result.reflection = _extract_reflection(raw)

    # 1. Safety always wins, regardless of anything else in the message.
    crisis_hits = _matches(_crisis_patterns(), raw)
    if crisis_hits:
        result.risk = "crisis"
        result.emotion = "crisis"
        result.matched_terms = crisis_hits
        return result

    high_risk_hits = _matches(_high_risk_patterns(), raw)
    if high_risk_hits:
        result.risk = "high"
        result.emotion = "deep_sad"
        result.matched_terms = high_risk_hits
        return result

    intent = detect_intent(raw, result.word_count)

    # A question about the companion itself is never an emotional disclosure.
    if intent == "about_bot":
        result.intent = intent
        result.emotion = intent
        return result

    # 2. Explicit emotional vocabulary, weighted so the strongest signal wins.
    # Request phrasing is removed first so "help me calm down" is not read as
    # a report of feeling calm and then low.
    emotion_text = raw
    for conversational in ("help_request", "thanks", "greeting", "farewell"):
        if _matches(_patterns_for("intent", conversational), raw):
            emotion_text = _strip_matches(emotion_text, _patterns_for("intent", conversational))

    scored: Dict[str, int] = {}
    matched: List[str] = []
    for emotion in EMOTION_TERMS:
        hits = _matches(_patterns_for("emotion", emotion), emotion_text)
        if hits:
            scored[emotion] = len(hits)
            matched.extend(hits)
    result.matched_terms = matched

    if scored:
        priority = [
            "deep_sad", "overwhelmed", "anxious", "angry", "lonely",
            "exhausted", "mild_sad", "confused", "excited", "grateful",
            "happy", "calm",
        ]
        best = max(scored.items(), key=lambda item: (item[1], -priority.index(item[0])))
        result.emotion = best[0]

        # Two or more distinct positive cues read as genuine excitement.
        if result.emotion == "happy" and scored.get("happy", 0) >= 2 and result.sentiment > 0.5:
            result.emotion = "excited"

        # A greeting bundled with real content stays content, but we remember
        # the greeting so the reply can acknowledge both.
        result.intent = intent if intent in {"thanks", "help_request", "about_bot"} else None
        return result

    # 3. Conversational intent, when there is no emotional content to respond to.
    if intent:
        result.intent = intent
        if intent in {"greeting", "farewell", "thanks", "about_bot", "help_request"}:
            result.emotion = intent
            return result

    # 4. Fall back to the sentiment score.
    score = result.sentiment
    if score <= -0.55:
        result.emotion = "deep_sad"
    elif score <= -0.2:
        result.emotion = "mild_sad"
    elif score >= 0.7:
        result.emotion = "excited"
    elif score >= 0.25:
        result.emotion = "happy"
    else:
        result.emotion = "neutral"
    return result


def detect_emotion(score: float, text: str) -> str:
    """Backwards-compatible entry point used by the original API."""
    return analyze(text, sentiment=score).emotion
