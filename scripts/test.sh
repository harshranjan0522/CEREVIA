#!/usr/bin/env bash
#
# CEREVIA end-to-end test suite.
#
# Starts a throwaway stack on spare ports against a temporary database, walks
# every API flow the UI depends on, then runs the companion's classification
# tests. Leaves the developer's real database untouched.

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${TEST_PORT:-5310}"
CHAT_PORT="${TEST_CHAT_PORT:-5311}"
TMP="$(mktemp -d)"
API="http://127.0.0.1:$PORT/api"
CHAT="http://127.0.0.1:$CHAT_PORT"

PASS=0
FAIL=0
FAILURES=()

if [[ -t 1 ]]; then
    GREEN=$'\033[32m'; RED=$'\033[31m'; DIM=$'\033[2m'; BOLD=$'\033[1m'; RESET=$'\033[0m'
else
    GREEN=''; RED=''; DIM=''; BOLD=''; RESET=''
fi

cleanup() {
    # `wait` after the kill keeps bash from printing its own "Terminated" line
    # for each background job once the script is already finished.
    for pid in "${BACKEND_PID:-}" "${CHAT_PID:-}"; do
        [[ -n "$pid" ]] || continue
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    rm -rf "$TMP"
}
trap cleanup EXIT

section() { printf '\n%s\n' "${BOLD}$*${RESET}"; }

check() {
    local label="$1" actual="$2" expected="$3"
    if [[ "$actual" == "$expected" ]]; then
        printf '  %s %s\n' "${GREEN}✓${RESET}" "$label"
        PASS=$((PASS + 1))
    else
        printf '  %s %s %s\n' "${RED}✗${RESET}" "$label" "${DIM}(expected '$expected', got '$actual')${RESET}"
        FAIL=$((FAIL + 1))
        FAILURES+=("$label")
    fi
}

check_contains() {
    local label="$1" haystack="$2" needle="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        printf '  %s %s\n' "${GREEN}✓${RESET}" "$label"
        PASS=$((PASS + 1))
    else
        printf '  %s %s %s\n' "${RED}✗${RESET}" "$label" "${DIM}(no '$needle' in response)${RESET}"
        FAIL=$((FAIL + 1))
        FAILURES+=("$label")
    fi
}

jq_get() { python3 -c "import json,sys;d=json.load(sys.stdin);print(d$1)" 2>/dev/null || echo "PARSE_ERROR"; }

status_of() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

# ---------------------------------------------------------------------------
printf '\n%s\n' "${BOLD}CEREVIA test suite${RESET}"
printf '%s\n' "${DIM}temporary database: $TMP/test.db${RESET}"

section "Build"
if command -v cmake >/dev/null 2>&1; then
    cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    cmake --build backend/build --config Release --parallel >/dev/null 2>&1
else
    make -C backend >/dev/null 2>&1
fi
BIN=""
for candidate in backend/build/backend backend/build/Release/backend; do
    [[ -x "$candidate" ]] && BIN="$ROOT/$candidate" && break
done
[[ -n "$BIN" ]] || { printf '  %s backend did not build\n' "${RED}✗${RESET}"; exit 1; }
printf '  %s backend compiles\n' "${GREEN}✓${RESET}"
PASS=$((PASS + 1))

# ---------------------------------------------------------------------------
section "Boot"
CEREVIA_ROOT="$ROOT" CEREVIA_DB="$TMP/test.db" CEREVIA_PORT="$PORT" \
    "$BIN" >"$TMP/backend.log" 2>&1 &
BACKEND_PID=$!

CEREVIA_CHAT_PORT="$CHAT_PORT" CEREVIA_BACKEND_URL="http://127.0.0.1:$PORT" \
    python3 "$ROOT/ai_chatbot/app.py" >"$TMP/chat.log" 2>&1 &
CHAT_PID=$!

for _ in $(seq 1 60); do curl -fsS -m 1 "$API/health" >/dev/null 2>&1 && break; sleep 0.25; done
for _ in $(seq 1 40); do curl -fsS -m 1 "$CHAT/health" >/dev/null 2>&1 && break; sleep 0.25; done

check "server answers /api/health" "$(curl -s "$API/health" | jq_get "['status']")" "ok"
check "database connected"        "$(curl -s "$API/health" | jq_get "['database']")" "connected"
check "companion answers /health" "$(curl -s "$CHAT/health" | jq_get "['status']")" "ok"

# ---------------------------------------------------------------------------
section "Static frontend"
check "GET / serves the lock screen"  "$(status_of "http://127.0.0.1:$PORT/")" "200"
check "stylesheet is served"          "$(status_of "http://127.0.0.1:$PORT/assets/css/cerevia.css")" "200"
check "core module is served"         "$(status_of "http://127.0.0.1:$PORT/assets/js/core.js")" "200"
check "extension-less URL resolves"   "$(status_of "http://127.0.0.1:$PORT/dashboard")" "200"
check "missing file 404s"             "$(status_of "http://127.0.0.1:$PORT/nope.html")" "404"
check "path traversal is refused"     "$(status_of "http://127.0.0.1:$PORT/../backend/Database.cpp")" "404"

# ---------------------------------------------------------------------------
section "Authentication"
check "wrong PIN is rejected"  "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"pin":"9999"}' "$API/auth/login" | jq_get "['success']")" "False"
check "default PIN works"      "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"pin":"1234"}' "$API/auth/login" | jq_get "['success']")" "True"
check "malformed JSON 400s"    "$(status_of -X POST -H 'Content-Type: application/json' -d '{oops' "$API/auth/login")" "400"
check "recovery question set"  "$(curl -s "$API/auth/question" | jq_get "['question']" | head -c 4)" "What"
check "wrong answer rejected"  "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"answer":"red"}' "$API/auth/verify" | jq_get "['verified']")" "False"
check "answer ignores case"    "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"answer":" BLUE "}' "$API/auth/verify" | jq_get "['verified']")" "True"
check "PIN is hashed on disk"  "$(python3 -c "
import sqlite3,sys
row = sqlite3.connect('$TMP/test.db').execute('select pin from users where id=1').fetchone()
print('hashed' if row and '\$' in row[0] and row[0] != '1234' else 'plaintext')
")" "hashed"
check "PIN change then login"  "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"pin":"4321"}' "$API/auth/pin" >/dev/null; curl -s -X POST -H 'Content-Type: application/json' -d '{"pin":"4321"}' "$API/auth/login" | jq_get "['success']")" "True"
check "short PIN refused"      "$(status_of -X POST -H 'Content-Type: application/json' -d '{"pin":"12"}' "$API/auth/pin")" "400"
curl -s -X POST -H 'Content-Type: application/json' -d '{"pin":"1234"}' "$API/auth/pin" >/dev/null

# ---------------------------------------------------------------------------
section "Mood check-ins"
MOOD_JSON="$(curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"mood":"Happy","level":8,"note":"went for a long walk","tags":["exercise","weather"]}' "$API/mood")"
check "check-in is stored"        "$(printf '%s' "$MOOD_JSON" | jq_get "['mood']")" "Happy"
check "wellbeing score computed"  "$(printf '%s' "$MOOD_JSON" | jq_get "['score']")" "90"
check "note is kept"              "$(printf '%s' "$MOOD_JSON" | jq_get "['note']")" "went for a long walk"
check "tags are kept"             "$(printf '%s' "$MOOD_JSON" | jq_get "['tags']")" "exercise,weather"
MOOD_ID="$(printf '%s' "$MOOD_JSON" | jq_get "['id']")"

check "lower-case mood accepted"  "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"mood":"calm","level":6}' "$API/mood" | jq_get "['mood']")" "Calm"
check_contains "unknown mood refused" "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"mood":"Sleepy","level":5}' "$API/mood")" "Unknown mood"
check "out-of-range level refused" "$(status_of -X POST -H 'Content-Type: application/json' -d '{"mood":"Sad","level":99}' "$API/mood")" "400"
check "missing mood refused"       "$(status_of -X POST -H 'Content-Type: application/json' -d '{"level":5}' "$API/mood")" "400"

check "history lists newest first" "$(curl -s "$API/mood?limit=5" | jq_get "[0]['mood']")" "Calm"
check "delete removes one"         "$(curl -s -X DELETE "$API/mood/$MOOD_ID" | jq_get "['success']")" "True"
check "deleting again 404s"        "$(status_of -X DELETE "$API/mood/$MOOD_ID")" "404"

# ---------------------------------------------------------------------------
section "Insights"
curl -s -X POST -H 'Content-Type: application/json' -d '{"mood":"Sad","level":8}'   "$API/mood" >/dev/null
curl -s -X POST -H 'Content-Type: application/json' -d '{"mood":"Angry","level":9}' "$API/mood" >/dev/null
curl -s -X POST -H 'Content-Type: application/json' -d '{"mood":"Sad","level":7}'   "$API/mood" >/dev/null

SUMMARY="$(curl -s "$API/stats/summary?days=14")"
check "summary reports a streak"  "$(printf '%s' "$SUMMARY" | jq_get "['streakDays']")" "1"
check "trend covers 14 days"      "$(printf '%s' "$SUMMARY" | jq_get "['trend'].__len__()")" "14"
check "today has data"            "$(printf '%s' "$SUMMARY" | jq_get "['trend'][-1]['hasData']")" "True"
check "distribution counts moods" "$(printf '%s' "$SUMMARY" | jq_get "['distribution']['Sad']")" "2"
check "latest mood surfaced"      "$(printf '%s' "$SUMMARY" | jq_get "['latest']['mood']")" "Sad"

check "crisis pattern detected"   "$(curl -s "$API/crisis" | jq_get "['crisis']")" "True"
check "crisis marked severe"      "$(curl -s "$API/crisis" | jq_get "['severe']")" "True"
check "suggestion turns urgent"   "$(curl -s "$API/suggestion" | jq_get "['urgent']")" "True"
check "toolkit matches the mood"  "$(curl -s "$API/eq" | jq_get "['latestMood']")" "Sad"
check_contains "toolkit has resources" "$(curl -s "$API/eq")" "activities"

curl -s -X POST "$API/mood/reset" >/dev/null
check "reset clears the history"  "$(curl -s "$API/mood" | jq_get ".__len__()")" "0"
check "crisis clears with it"     "$(curl -s "$API/crisis" | jq_get "['crisis']")" "False"

# ---------------------------------------------------------------------------
section "Journal"
SECRET="a sentence nobody else should be able to read"
ENTRY="$(curl -s -X POST -H 'Content-Type: application/json' \
    -d "{\"text\":\"$SECRET\",\"prompt\":\"What is on your mind?\"}" "$API/journal")"
check "entry is stored"       "$(printf '%s' "$ENTRY" | jq_get "['text']")" "$SECRET"
check "word count computed"   "$(printf '%s' "$ENTRY" | jq_get "['wordCount']")" "9"
ENTRY_ID="$(printf '%s' "$ENTRY" | jq_get "['id']")"

check "empty entry refused"   "$(status_of -X POST -H 'Content-Type: application/json' -d '{"text":"   "}' "$API/journal")" "400"
check "entry is encrypted on disk" "$(python3 -c "
import sqlite3
row = sqlite3.connect('$TMP/test.db').execute('select text from journals order by id desc limit 1').fetchone()
print('encrypted' if row and '$SECRET' not in row[0] and row[0].startswith('v2:') else 'PLAINTEXT')
")" "encrypted"
check "search finds it"       "$(curl -s "$API/journal?q=nobody" | jq_get ".__len__()")" "1"
check "search can miss"       "$(curl -s "$API/journal?q=zzzqqq" | jq_get ".__len__()")" "0"
check "stats count entries"   "$(curl -s "$API/journal/stats" | jq_get "['count']")" "1"
check_contains "prompts are served" "$(curl -s "$API/journal/prompt")" "prompt"

# The old build capped the journal at seven rows and silently deleted the rest.
for i in $(seq 1 9); do
    curl -s -X POST -H 'Content-Type: application/json' -d "{\"text\":\"entry number $i\"}" "$API/journal" >/dev/null
done
check "journal is not truncated" "$(curl -s "$API/journal/stats" | jq_get "['count']")" "10"

check "delete one entry"      "$(curl -s -X DELETE "$API/journal/$ENTRY_ID" | jq_get "['success']")" "True"
curl -s -X POST "$API/journal/reset" >/dev/null
check "reset clears journal"  "$(curl -s "$API/journal/stats" | jq_get "['count']")" "0"

# ---------------------------------------------------------------------------
section "Breathing"
check "session is logged"     "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"technique":"box","cycles":4,"seconds":64}' "$API/breathing" | jq_get "['success']")" "True"
check "cycles accumulate"     "$(curl -s "$API/breathing" | jq_get "['cycles']")" "4"
check "empty session refused" "$(status_of -X POST -H 'Content-Type: application/json' -d '{"cycles":0}' "$API/breathing")" "400"
curl -s -X POST "$API/breathing/reset" >/dev/null
check "reset zeroes it"       "$(curl -s "$API/breathing" | jq_get "['sessions']")" "0"

# ---------------------------------------------------------------------------
section "Profile"
curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"displayName":"Harsh","emergencyContact":"14416"}' "$API/profile" >/dev/null
check "display name persists"     "$(curl -s "$API/profile" | jq_get "['displayName']")" "Harsh"
check "emergency contact persists" "$(curl -s "$API/profile" | jq_get "['emergencyContact']")" "14416"
check_contains "crisis uses the contact" "$(curl -s "$API/crisis")" "14416"

curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"securityQuestion":"First pet?","securityAnswer":"Rex"}' "$API/profile" >/dev/null
check "new recovery answer works" "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"answer":"rex"}' "$API/auth/verify" | jq_get "['verified']")" "True"
check "old recovery answer fails" "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"answer":"blue"}' "$API/auth/verify" | jq_get "['verified']")" "False"

# ---------------------------------------------------------------------------
section "Legacy v1 routes"
check "POST /login still works"    "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"pin":"1234"}' "http://127.0.0.1:$PORT/login" | jq_get "['success']")" "True"
check "GET /mood/all still works"  "$(status_of "http://127.0.0.1:$PORT/mood/all")" "200"
check "GET /emergency/contact"     "$(curl -s "http://127.0.0.1:$PORT/emergency/contact" | jq_get "['contact']")" "14416"

# ---------------------------------------------------------------------------
section "Companion"
# Messages here are plain prose, so a printf-built body is safe and avoids
# quoting a JSON literal through two levels of shell.
ask() {
    curl -s -X POST -H 'Content-Type: application/json' \
        -d "{\"message\":\"$1\",\"sessionId\":\"$2\"}" \
        "$CHAT/chat"
}

check "greeting is recognised"        "$(ask 'hello' t1 | jq_get "['emotion']")" "greeting"
check "anxiety is recognised"         "$(ask 'I am really anxious about my exam' t2 | jq_get "['emotion']")" "anxious"
check "negation is respected"         "$(ask 'I am not happy this week' t3 | jq_get "['emotion']")" "mild_sad"
check "crisis is escalated"           "$(ask 'I want to kill myself' t4 | jq_get "['risk']")" "crisis"
check_contains "crisis reply gives a helpline" "$(ask 'I want to end my life' t5)" "14416"
check "figure of speech is not a crisis" "$(ask 'I am dying to see that film' t6 | jq_get "['risk']")" "none"
check "session id is returned"        "$(ask 'hi' t7 | jq_get "['sessionId']")" "t7"
check "empty message is handled"      "$(curl -s -X POST -H 'Content-Type: application/json' -d '{"message":"  "}' "$CHAT/chat" | jq_get "['emotion']")" "neutral"
check_contains "technique library is served" "$(curl -s "$CHAT/techniques")" "grounding_54321"

# A distressed conversation should offer help rather than repeat itself.
ask 'today was really hard' conv1 >/dev/null
ask 'I feel overwhelmed by everything' conv1 >/dev/null
THIRD="$(ask 'I still cannot cope with any of it' conv1)"
check_contains "repeated distress escalates" "$THIRD" "technique"
check "companion sees the backend"    "$(curl -s "$CHAT/health" | jq_get "['backendReachable']")" "True"

section "Companion language model (optional)"
LLM_ENABLED="$(curl -s "$CHAT/health" | jq_get "['llm']['enabled']")"
if [[ "$LLM_ENABLED" == "True" ]]; then
    check "model is configured"        "$(curl -s "$CHAT/health" | jq_get "['llm']['provider']")" "gemini"
    check "safety never reaches it"    "$(ask 'I want to kill myself' llm1 | jq_get "['source']")" "local"
    check "high risk never reaches it" "$(ask 'nobody would miss me' llm2 | jq_get "['source']")" "local"
    LLM_REPLY="$(ask 'I have been dreading work all week' llm3)"
    LLM_SOURCE="$(printf '%s' "$LLM_REPLY" | jq_get "['source']")"
    RATE_LIMITED="$(curl -s "$CHAT/health" | jq_get "['llm']['rateLimited']")"
    if [[ "$LLM_SOURCE" == gemini* ]]; then
        printf '  %s ordinary message uses the model\n' "${GREEN}✓${RESET}"; PASS=$((PASS+1))
    elif [[ "$RATE_LIMITED" == "True" ]]; then
        # The free tier is limited per minute; falling back to the local engine
        # is the designed behaviour, not a failure.
        printf '  %s rate limited — fell back to the local engine, as designed\n' "${GREEN}✓${RESET}"; PASS=$((PASS+1))
    else
        printf '  %s ordinary message used %s\n' "${RED}✗${RESET}" "$LLM_SOURCE"; FAIL=$((FAIL+1)); FAILURES+=("model not used")
    fi
    # A truncated generation used to leak raw JSON into the chat bubble.
    if [[ "$(printf '%s' "$LLM_REPLY" | jq_get "['response']")" == "{"* ]]; then
        printf '  %s model reply leaked raw JSON\n' "${RED}✗${RESET}"; FAIL=$((FAIL+1)); FAILURES+=("raw JSON leak")
    else
        printf '  %s reply is prose, not raw JSON\n' "${GREEN}✓${RESET}"; PASS=$((PASS+1))
    fi
    LLM_ERR="$(curl -s "$CHAT/health" | jq_get "['llm']['lastError']")"
    if [[ "$LLM_ERR" == "None" || "$LLM_ERR" == *"rate limited"* ]]; then
        printf '  %s no unexpected model errors\n' "${GREEN}✓${RESET}"; PASS=$((PASS+1))
    else
        printf '  %s model error: %s\n' "${RED}✗${RESET}" "${LLM_ERR:0:90}"; FAIL=$((FAIL+1)); FAILURES+=("model error")
    fi
else
    printf '  %s no model configured — local engine only (this is a valid setup)\n' "${DIM}·${RESET}"
fi

section "Companion language tests"
if python3 "$ROOT/ai_chatbot/test_engine.py" >"$TMP/engine.log" 2>&1; then
    printf '  %s %s\n' "${GREEN}✓${RESET}" "$(tail -1 "$TMP/engine.log")"
    PASS=$((PASS + 1))
else
    printf '  %s companion classification tests failed:\n' "${RED}✗${RESET}"
    sed 's/^/      /' "$TMP/engine.log"
    FAIL=$((FAIL + 1))
    FAILURES+=("companion classification")
fi

# ---------------------------------------------------------------------------
printf '\n%s\n' "${BOLD}────────────────────────────────────────${RESET}"
if (( FAIL == 0 )); then
    printf '%s\n\n' "${GREEN}All $PASS checks passed.${RESET}"
    exit 0
fi
printf '%s\n' "${RED}$FAIL failed${RESET}, $PASS passed."
for failure in "${FAILURES[@]}"; do printf '  · %s\n' "$failure"; done
printf '\n'
exit 1
