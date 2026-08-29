"""Lexicon-based sentiment scoring for the CEREVIA companion.

The previous implementation delegated to ``vaderSentiment``, which meant the
chatbot simply refused to start unless that package happened to be installed.
This module reimplements the parts of that approach the app actually needs --
valence lookup, negation, intensifiers, contrastive clauses and punctuation
emphasis -- against a lexicon that ships with the project, so the companion has
no third-party runtime dependencies at all.

The returned ``compound`` score keeps the familiar -1.0 .. +1.0 range so the
thresholds elsewhere in the app read the same as before.
"""

from __future__ import annotations

import json
import math
import os
import re
from functools import lru_cache
from typing import Dict, List

_DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
_LEXICON_PATH = os.path.join(_DATA_DIR, "lexicon.json")

# How far back a negation reaches. "I am not at all happy" still flips `happy`.
_NEGATION_WINDOW = 3

# Applied to a negated valence: negation weakens as well as flips, because
# "not happy" is closer to neutral than "sad" is.
_NEGATION_FACTOR = -0.74

_ALL_CAPS_BOOST = 0.733
_EXCLAMATION_BOOST = 0.292
_MAX_EMPHASIS_MARKS = 4

_TOKEN_RE = re.compile(r"[a-z0-9']+|[^\sa-z0-9']", re.IGNORECASE)


@lru_cache(maxsize=1)
def _lexicon() -> Dict[str, object]:
    with open(_LEXICON_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _tokenize(text: str) -> List[str]:
    return _TOKEN_RE.findall(text)


def _is_all_caps(token: str) -> bool:
    # A single capital letter or an acronym is not shouting.
    return len(token) > 2 and token.isupper() and token.isalpha()


def _emphasis_from_punctuation(text: str) -> float:
    exclamations = min(text.count("!"), _MAX_EMPHASIS_MARKS)
    boost = exclamations * _EXCLAMATION_BOOST

    # "why?!?" reads as more charged than a plain question.
    if "?" in text and "!" in text:
        boost += _EXCLAMATION_BOOST
    return boost


def _phrase_valence(lowered: str) -> float:
    """Sums the score of every multi-word expression present in the text."""
    total = 0.0
    for phrase, score in _lexicon()["phrases"].items():
        if phrase in lowered:
            total += score
    return total


def _score_tokens(tokens: List[str], has_shouting: bool) -> List[float]:
    words = _lexicon()["words"]
    boosters = _lexicon()["boosters"]
    negations = set(_lexicon()["negations"])

    scores: List[float] = []
    for index, raw in enumerate(tokens):
        token = raw.lower()
        if token not in words:
            continue

        valence = float(words[token])

        # Shouting a word amplifies whichever direction it already points.
        if _is_all_caps(raw) and has_shouting:
            valence += _ALL_CAPS_BOOST if valence > 0 else -_ALL_CAPS_BOOST

        # Intensifiers immediately before the word scale it; the effect fades
        # with distance so "very very tired" still stacks but "very" three
        # words back barely counts.
        for distance in range(1, _NEGATION_WINDOW + 1):
            previous_index = index - distance
            if previous_index < 0:
                break
            previous = tokens[previous_index].lower()
            if previous in boosters:
                modifier = float(boosters[previous]) * (1.0 - 0.2 * (distance - 1))
                valence += modifier if valence > 0 else -modifier

        # Negation anywhere in the preceding window flips the sign.
        window = [tokens[i].lower() for i in range(max(0, index - _NEGATION_WINDOW), index)]
        if any(word in negations for word in window):
            valence *= _NEGATION_FACTOR

        scores.append(valence)
    return scores


def polarity_scores(text: str) -> Dict[str, float]:
    """Returns ``{"compound", "positive", "negative", "neutral"}`` for ``text``."""
    if not text or not text.strip():
        return {"compound": 0.0, "positive": 0.0, "negative": 0.0, "neutral": 1.0}

    lowered = text.lower()
    tokens = _tokenize(text)
    words_only = [t for t in tokens if t.isalnum()]
    has_shouting = any(_is_all_caps(t) for t in words_only) and not all(
        _is_all_caps(t) for t in words_only if len(t) > 2
    )

    scores = _score_tokens(tokens, has_shouting)
    scores.append(_phrase_valence(lowered))

    # A contrastive conjunction usually means the second half is the real
    # message: "I had a good day but I feel awful" is not a positive statement.
    contrastive = _lexicon()["contrastive"]
    pivot = None
    for marker in contrastive:
        match = re.search(rf"\b{re.escape(marker)}\b", lowered)
        if match and (pivot is None or match.start() < pivot):
            pivot = match.start()

    if pivot is not None:
        before = text[:pivot]
        after = text[pivot:]
        before_scores = _score_tokens(_tokenize(before), has_shouting)
        after_scores = _score_tokens(_tokenize(after), has_shouting)
        before_scores.append(_phrase_valence(before.lower()))
        after_scores.append(_phrase_valence(after.lower()))
        scores = [s * 0.5 for s in before_scores] + [s * 1.5 for s in after_scores]

    total = sum(scores)
    if total > 0:
        total += _emphasis_from_punctuation(text)
    elif total < 0:
        total -= _emphasis_from_punctuation(text)

    positive = sum(s for s in scores if s > 0)
    negative = abs(sum(s for s in scores if s < 0))
    magnitude = positive + negative

    # Same normalisation curve VADER uses: squashes an unbounded sum into
    # (-1, 1) while keeping small differences meaningful near zero.
    compound = total / math.sqrt(total * total + 15.0) if total else 0.0
    compound = max(-1.0, min(1.0, compound))

    return {
        "compound": round(compound, 4),
        "positive": round(positive / magnitude, 4) if magnitude else 0.0,
        "negative": round(negative / magnitude, 4) if magnitude else 0.0,
        "neutral": 0.0 if magnitude else 1.0,
    }


def analyze_sentiment(text: str) -> float:
    """Backwards-compatible helper: returns just the compound score."""
    return polarity_scores(text)["compound"]
