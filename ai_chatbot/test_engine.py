"""Classification tests for the companion's language understanding.

Run with:  python3 ai_chatbot/test_engine.py

These lock in the behaviours that were previously broken -- substring matches,
negation, and safety detection -- so a future edit to the vocabulary cannot
quietly reintroduce them.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from emotion_engine import analyze  # noqa: E402

# (message, expected emotion or None, expected risk)
CASES = [
    # --- safety --------------------------------------------------------------
    ("I want to kill myself",                        "crisis",   "crisis"),
    ("i've been thinking about ending it all",       "crisis",   "crisis"),
    ("I keep thinking about hurting myself",         "crisis",   "crisis"),
    ("I feel like a burden to everyone",             "deep_sad", "high"),
    ("nobody would miss me if I was gone",           "deep_sad", "high"),
    # ...but ordinary figures of speech are not a crisis
    ("I am dying to see that film",                  None,       "none"),
    ("that presentation killed me, so embarrassing", None,       "none"),

    # --- the substring bugs that used to break this --------------------------
    ("I am not happy this week",                     "mild_sad", "none"),
    ("nothing is going right, I think about it",     None,       "none"),
    ("I was worried about money all day",            "anxious",  "none"),
    ("my laptop battery is low",                     "neutral",  "none"),
    ("let me know what you think",                   "neutral",  "none"),
    ("can you help me calm down",                    "help_request", "none"),

    # --- contractions: apostrophe present, missing, or spelled out -----------
    ("I cant sleep",                                 "exhausted",   "none"),
    ("I cannot sleep",                               "exhausted",   "none"),
    ("I cannot stop thinking about it",              "anxious",     "none"),
    ("im a burden to everyone",                      "deep_sad",    "high"),
    ("I dont want to live",                          "crisis",      "crisis"),

    # --- negation ------------------------------------------------------------
    ("I don't feel anxious anymore",                 None,       "none"),
    ("I am not sad at all, actually great",          "excited",  "none"),

    # --- greetings only when they are the whole message ----------------------
    ("hey",                                          "greeting", "none"),
    ("hi there",                                     "greeting", "none"),
    ("hey, my boss shouted at me and I'm furious",    "angry",    "none"),
    ("good night",                                   "farewell", "none"),

    # --- core emotions -------------------------------------------------------
    ("I'm really anxious about my exam tomorrow",     "anxious",     "none"),
    ("everything is piling up and I can't cope",      "overwhelmed", "none"),
    ("I am exhausted, I haven't slept in days",       "exhausted",   "none"),
    ("I feel so lonely lately",                       "lonely",      "none"),
    ("I got the job! I can't wait to start",          "excited",     "none"),
    ("I'm grateful for my friends today",             "grateful",    "none"),
    ("I feel calm today",                             "calm",        "none"),
    ("I don't know what to do, I'm torn between two", "confused",    "none"),
    ("who are you?",                                  "about_bot",   "none"),
    ("thank you so much",                             "thanks",      "none"),
]

TOPIC_CASES = [
    ("I'm anxious about my exam tomorrow", "study"),
    ("my boss keeps piling on work",       "work"),
    ("we broke up last week",              "relationship"),
    ("I can't sleep at 3am",               "sleep"),
    ("rent is due and I'm broke",          "money"),
]


def main() -> int:
    failures = []

    for message, expected_emotion, expected_risk in CASES:
        result = analyze(message)
        if result.risk != expected_risk:
            failures.append(f"risk  {message!r}: expected {expected_risk}, got {result.risk}")
        if expected_emotion is not None and result.emotion != expected_emotion:
            failures.append(f"emo   {message!r}: expected {expected_emotion}, got {result.emotion}")

    for message, expected_topic in TOPIC_CASES:
        result = analyze(message)
        if expected_topic not in result.topics:
            failures.append(f"topic {message!r}: expected {expected_topic}, got {result.topics}")

    total = len(CASES) + len(TOPIC_CASES)
    if failures:
        print(f"FAILED {len(failures)} of {total} checks:\n")
        for failure in failures:
            print("  -", failure)
        return 1

    print(f"All {total} companion classification checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
