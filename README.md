<div align="center">

# CEREVIA

**A private mental-health companion that runs entirely on your own machine.**

Check in with how you actually feel · write it out · slow your breathing down · talk it through

`C++ backend` · `SQLite` · `zero-dependency Python companion` · `vanilla-JS frontend`

</div>

---

## Contents

- [CEREVIA](#cerevia)
  - [Contents](#contents)
  - [What it is](#what-it-is)
  - [Run it](#run-it)
  - [The `./cerevia` command](#the-cerevia-command)
  - [What is inside](#what-is-inside)
  - [The screens](#the-screens)
  - [How the companion works](#how-the-companion-works)
- [Turning on Gemini](#turning-on-gemini)
    - [Bugs this version fixes](#bugs-this-version-fixes)
  - [How the numbers are worked out](#how-the-numbers-are-worked-out)
  - [Architecture](#architecture)
  - [API reference](#api-reference)
    - [Meta](#meta)
    - [Authentication](#authentication)
    - [Profile](#profile)
    - [Moods](#moods)
    - [Journal](#journal)
    - [Insights](#insights)
    - [Breathing](#breathing)
    - [Companion (port 5001)](#companion-port-5001)
  - [Data model](#data-model)
  - [Security and privacy](#security-and-privacy)
  - [Testing](#testing)
  - [Configuration](#configuration)
  - [Troubleshooting](#troubleshooting)
  - [Design notes](#design-notes)
  - [Limits, honestly](#limits-honestly)
  - [If you are struggling right now](#if-you-are-struggling-right-now)

---

## What it is

CEREVIA is a mood tracker, journal, breathing room and supportive chatbot in one
local application. There is no account, no server to sign up for and no network
traffic beyond your own loopback interface. The database is a single SQLite file
in `database/`; journal entries are encrypted before they are written to it.

It is built as a teaching-scale full-stack project — a hand-written C++ HTTP
server, a from-scratch sentiment engine, and a frontend with no build step — but
it is meant to be genuinely usable, not a demo.

**It is not a medical service.** It cannot diagnose or treat anything. Its
safety layer is designed to get you to a real person quickly, not to replace one.

---

## Run it

You need a C++17 compiler, `make`, Python 3.8+ and `curl`. Nothing else — no
`npm install`, no `pip install`, no database server.

```bash
git clone <this repo> && cd CEREVIA
./cerevia
```

That single command will:

1. compile the C++ backend (CMake if you have it, plain `make` otherwise),
2. create and migrate `database/mental_health.db` if it does not exist,
3. start the Python companion service,
4. start the backend, which serves both the JSON API **and** the frontend,
5. wait until both answer a health check,
6. open your browser at the right URL.

The first-run PIN is **`1234`**. Change it in Settings.

Press <kbd>Ctrl</kbd>+<kbd>C</kbd> to stop everything.

> **On macOS**, port 5000 is taken by AirPlay Receiver. `./cerevia` detects this
> and moves to the next free port automatically, printing the URL it chose. To
> keep 5000, turn off *System Settings → General → AirDrop & Handoff → AirPlay
> Receiver*.

Not sure your machine is ready? `./cerevia doctor` checks and tells you.

---

## The `./cerevia` command

| Command | What it does |
| --- | --- |
| `./cerevia` | Build, start everything, open the browser |
| `./cerevia start` | The same, without opening a browser |
| `./cerevia stop` | Stop both services |
| `./cerevia restart` | Stop, then start |
| `./cerevia status` | What is running, on which port, and how big the database is |
| `./cerevia logs` | Follow both service logs |
| `./cerevia build` | Compile the backend only |
| `./cerevia test` | Run the full end-to-end suite |
| `./cerevia reset` | Delete the database and start clean (asks for confirmation) |
| `./cerevia doctor` | Check the machine has everything it needs |

`make`, `make run`, `make test`, `make clean` forward to the same script.

---

## What is inside

```
CEREVIA/
├── cerevia                     the launcher — one command runs everything
├── Makefile                    thin wrapper around ./cerevia
├── scripts/test.sh             end-to-end suite (77 checks)
│
├── backend/                    C++17 · the API and the static file server
│   ├── main.cpp                entry point, port resolution
│   ├── ApiServer.{h,cpp}       routing, static files, login throttling
│   ├── Http.{h,cpp}            request parsing, responses, MIME, path safety
│   ├── Net.h                   Winsock / BSD socket shim
│   ├── Database.{h,cpp}        connection, schema, migrations, user profile
│   ├── MoodTracker.{h,cpp}     check-ins, analytics, suggestions, EQ matching
│   ├── JournalManager.{h,cpp}  entries, search, encryption at rest
│   ├── Encryption.{h,cpp}      SHA-256, salted hashing, journal cipher
│   ├── Paths.{h,cpp}           finds the project root from anywhere
│   ├── Sql.h                   RAII + null-safe wrapper over the SQLite C API
│   ├── suggestion.json         daily suggestions per mood
│   └── eq_resources.json       reading / watching / doing / listening per mood
│
├── ai_chatbot/                 Python 3.8+ · the companion, no dependencies
│   ├── app.py                  entry point
│   ├── server.py               stdlib HTTP server
│   ├── sentiment_model.py      lexicon sentiment analyser
│   ├── emotion_engine.py       intent, emotion, risk and topic detection
│   ├── response_generator.py   composes replies from session context
│   ├── conversation.py         per-session memory
│   ├── llm.py                  optional Gemini backing, with local fallback
│   ├── test_engine.py          39 classification tests
│   └── data/                   lexicon.json · templates.json · techniques.json
│
├── frontend/                   no build step, ES modules straight to the browser
│   ├── index.html              lock screen
│   ├── dashboard.html          Today
│   ├── mood.html               Check in
│   ├── journal.html            Journal
│   ├── breathe.html            Breathing room
│   ├── eq.html                 Toolkit
│   ├── settings.html           Settings
│   └── assets/
│       ├── css/cerevia.css     the whole design system, one file
│       └── js/                 core · companion · sky · icons · one per page
│
└── database/
    ├── schema.sql              reference schema (applied automatically)
    ├── mental_health.db        your data — git-ignored
    └── journal.key             the journal encryption key — git-ignored
```

---

## The screens

**Today** — a wellbeing gauge, your check-in streak, journal totals, a fortnight
of mood drawn as a horizon line, the spread of moods, a suggestion tuned to how
you last felt, and a care banner that only appears when a distress pattern shows up.

**Check in** — six moods, an intensity from 1 to 10, an optional note and tags.
Choosing a mood tints the whole interface with that mood's colour; the tint
follows you across every page until your next check-in.

**Journal** — a rotating writing prompt, a live word count, an unsaved draft kept
locally, full-text search across everything you have written, and per-entry
deletion. Entries are encrypted before they reach the database.

**Breathing room** — three patterns (box, 4-7-8, coherent) driving an orb that
expands and contracts in time with the phase, plus an interactive 5-4-3-2-1
grounding exercise. Completed cycles are saved to the database.

**Toolkit** — articles, videos, activities and music matched to your most recent
mood, alongside the companion's full library of coping techniques.

**Settings** — display name, emergency contact, PIN, recovery question, theme
(light / dark / follow the system), a full JSON export, and a delete-everything button.

**The companion** — a drawer available from every screen. See below.

---

## How the companion works

The companion runs as a separate Python service on Python's standard library
alone. There is no model download, no API key, and nothing leaves your machine.

A message goes through four stages:

**1. Sentiment** (`sentiment_model.py`) — a lexicon of ~230 scored words and ~50
multi-word expressions, with negation (`not happy` ≠ `happy`), intensifiers
(`very`, `slightly`), contrastive clauses (`I had a good day **but** I feel
awful` weights the second half), capitalisation and punctuation emphasis. The
result is a compound score in −1 … +1.

**2. Classification** (`emotion_engine.py`) — a priority ladder, every match
anchored to word boundaries:

```
crisis  →  high risk  →  explicit emotion words  →  conversational intent  →  sentiment
```

Emotions: `deep_sad`, `mild_sad`, `anxious`, `angry`, `overwhelmed`,
`exhausted`, `lonely`, `confused`, `calm`, `grateful`, `happy`, `excited`,
`neutral`. Topics detected alongside: work, study, family, relationship,
friends, health, sleep, money, self-image, loss, future.

**3. Memory** (`conversation.py`) — each session remembers its turns, the
emotions seen, the topics raised, which phrases have already been said and which
techniques have already been offered. Sessions live in memory only and expire
after four hours.

**4. Composition** (`response_generator.py`) — a reply is built from a
*reflection* + a *validation* + an *invitation*, never repeating a fragment the
session has recently heard. The conversation then escalates:

| Consecutive distressed turns | What the companion does |
| --- | --- |
| 1 | Listens. Reflects back what it heard, asks one question. |
| 2 | Offers a specific coping technique with steps, matched to the emotion. |
| 3 | Names the pattern out loud and asks whether a real person knows. |

Risk is handled before any of that. A crisis statement produces an unmissable
safety response with helplines for several regions, the emergency contact you
saved in Settings, and a grounding exercise — every time, with no exceptions.

### Bugs this version fixes

The previous companion matched keywords with `word in text`, so any substring
counted:

| Message | Was classified as | Now |
| --- | --- | --- |
| "I am not happy **thi**s week" | `greeting` (`hi` inside `this`) | `mild_sad` |
| "I'm worried a**but** money" | mixed-emotion branch (`but`) | `anxious` |
| "**they** left" | `greeting` (`hey` inside `they`) | as written |
| "my laptop battery is low" | `mild_sad` | `neutral` |
| "can you help me calm down" | `calm` | `help_request` |
| "I dont want to live" | matched only with the apostrophe | crisis, either spelling |

It also could not start at all unless `flask`, `flask-cors` and `vaderSentiment`
happened to be installed, and `response_generator.py` opened its templates at a
path that only resolved when the process was started from the wrong directory —
so the import worked or the file opened, but never both.

---

## Turning on Gemini

The companion works fully offline with the built-in engine. You can optionally
back the *conversational* replies with Google's Gemini, which makes it markedly
better at reflecting your actual words back to you.

**Safety never goes to the model.** The order is fixed and cannot be configured
away:

```
your message
     │
     ▼
 local analysis  ──── crisis or high risk? ────▶  fixed local safety response
     │                                             (the model is never called)
     ▼ no
 Gemini writes the reply
     │
     ▼
 local re-check of the model's own words  ──── trips? ───▶  local reply instead
     │
     ▼
 shown to you
```

Any failure — no network, a bad key, a rate limit, a timeout, a truncated
generation — falls back to the local engine. The chat never dies, and it never
shows you a half-written response.

### Setting it up

1. Get a free key at [aistudio.google.com/apikey](https://aistudio.google.com/apikey).
2. Put it in a `.env` file in the project root:

   ```dotenv
   CEREVIA_LLM_PROVIDER=gemini
   CEREVIA_LLM_KEY=your-key-here
   CEREVIA_LLM_MODEL=gemini-3.5-flash   # optional
   ```

   `.env` is git-ignored. `gemini_api=` is also accepted as the key name.
3. `./cerevia` — the launcher loads `.env` and the startup log says which engine
   is active. The companion's header shows it too.

To switch it off again, set `CEREVIA_LLM_PROVIDER=none` or remove the key.

### What this costs you in privacy

This is the one place CEREVIA talks to the internet, and you should decide about
it deliberately:

- **What is sent:** the text of your chat messages, the last ~10 turns of that
  conversation, the detected emotion and topic, and your most recent mood
  check-in (mood name, intensity, date).
- **What is never sent:** your journal entries, your PIN, your recovery answer,
  your full mood history, and any message that trips the safety detector.
- Google's free tier may use submitted data to improve its products. If that is
  not acceptable for you, leave the model off — the local engine is the default
  for exactly this reason.

### Free-tier limits

The free tier is limited per minute. When Google returns a 429 the client reads
the retry window it names, stops calling for that long, and answers locally in
the meantime — so hitting the limit costs you nothing but a slightly plainer
reply. `curl 127.0.0.1:5001/health` on the companion port reports
`rateLimited` and `cooldownSeconds`.

### Other providers

`llm.py` is one small module with a single `generate()` entry point. Pointing it
at Ollama (fully local, keeps the privacy promise intact), Groq or OpenRouter is
a matter of adding one function beside `_call_gemini`.

---

## How the numbers are worked out

**Wellbeing score (0–100).** Each mood has a valence on a pleasant/unpleasant
axis, and intensity scales it:

```
score = 50 + valence × (level ÷ 10) × 50

Happy +1.00   Calm +0.85   Neutral +0.10
Anxious −0.55  Angry −0.70   Sad −0.90
```

So *happy at 10* is 100, *sad at 10* is 5, and *slightly sad* sits much closer to
even than *overwhelmingly sad*. The dashboard averages this over the last 14
days, falling back to your all-time average if that window is empty.

The previous formula was `(level × weight + 10) × 5`, which put *sad at level 1*
at 45 and *neutral at level 1* at 52 — a difference of seven points between
"a bit sad" and "fine".

**Streak.** Distinct calendar days with at least one check-in, counted backwards
from today. A streak survives if your most recent check-in was today *or*
yesterday.

**The fortnight chart.** One point per calendar day. Days with no check-in are
drawn as gaps with a dashed bridge, never as zeroes — a break in the habit should
not look like a collapse in mood.

**Care banner.** Raised when the last three check-ins are all Sad, Angry or
Anxious; marked severe when at least two of them were intensity 7 or above.

---

## Architecture

```
                         ┌──────────────────────────────┐
   your browser  ────▶   │  backend  (C++17)  :5000     │
                         │                              │
                         │  /            frontend files │
                         │  /api/*       JSON API       │──▶  SQLite
                         └──────────────────────────────┘     (one local file)
          │                              ▲
          │  POST /chat                  │  reads your latest mood
          ▼                              │  to personalise replies
   ┌──────────────────────────────┐      │
   │  companion (Python)  :5001   │──────┘
   │  stdlib only, no deps        │
   └──────────────────────────────┘
```

The backend serves the frontend from the same origin, so there is no CORS dance
for the main app and no third static server to run. The companion is separate
because it is the one part you might want to restart, swap out or extend without
recompiling anything.

Both bind to `127.0.0.1` only. Neither accepts a connection from another machine.

---

## API reference

Base URL `http://127.0.0.1:<port>/api`. Everything is JSON.

### Meta
| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/health` | Liveness and database status |
| `GET` | `/meta` | Version, mood vocabulary, journal prompts, companion port |

### Authentication
| Method | Path | Body | Notes |
| --- | --- | --- | --- |
| `POST` | `/auth/login` | `{pin}` | Throttles after 5 failures |
| `POST` | `/auth/pin` | `{pin}` | Change the PIN (4–12 characters) |
| `GET` | `/auth/question` | | The recovery question |
| `POST` | `/auth/verify` | `{answer}` | Case- and space-insensitive |
| `POST` | `/auth/reset` | `{answer, newPin}` | Requires the recovery answer |

### Profile
| Method | Path | Body |
| --- | --- | --- |
| `GET` | `/profile` | |
| `POST` | `/profile` | `{displayName?, emergencyContact?, securityQuestion?, securityAnswer?, theme?, reminderTime?}` |

### Moods
| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/mood?days=&limit=` | Newest first |
| `POST` | `/mood` | `{mood, level, note?, tags?}` — rejects unknown moods and levels outside 1–10 |
| `DELETE` | `/mood/{id}` | |
| `POST` | `/mood/reset` | Returns how many rows were deleted |

### Journal
| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/journal?q=&limit=` | `q` searches the decrypted text |
| `POST` | `/journal` | `{text, mood?, prompt?}` |
| `GET` | `/journal/stats` | Count, total words, days written |
| `GET` | `/journal/prompt` | A random writing prompt |
| `DELETE` | `/journal/{id}` | |
| `POST` | `/journal/reset` | |

### Insights
| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/stats/summary?days=14` | Score, streak, trend, distribution, latest, totals |
| `GET` | `/suggestion` | Mood-matched suggestion; becomes urgent during a crisis pattern |
| `GET` | `/eq` | Toolkit resources for the latest mood |
| `GET` | `/crisis` | Distress-pattern state and your emergency contact |

### Breathing
| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/breathing` | Session, cycle and second totals |
| `POST` | `/breathing` | `{technique, cycles, seconds}` |
| `POST` | `/breathing/reset` | |

### Companion (port 5001)
| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/health` | |
| `GET` | `/techniques` | The full technique library |
| `POST` | `/chat` | `{message, sessionId?}` → `{response, emotion, risk, technique, actions, sessionId}` |
| `POST` | `/reset` | `{sessionId}` — forget a conversation |

The v1 routes (`POST /login`, `GET /mood/all`, `GET /stats/crisis`, …) still
resolve, so anything built against the old API keeps working.

---

## Data model

Five tables, created and migrated automatically on start-up. The full definition
with comments is in [`database/schema.sql`](database/schema.sql).

| Table | Holds |
| --- | --- |
| `users` | The single local user: hashed PIN, hashed recovery answer, emergency contact, display name |
| `moods` | One row per check-in: mood, level, note, tags, timestamps |
| `journals` | Encrypted entry text, prompt, word count, timestamps |
| `breathing_sessions` | Technique, cycles, seconds |
| `settings` | Free-form key/value preferences |

Migrations are additive and idempotent: on start-up the backend inspects each
table with `PRAGMA table_info` and adds any missing column. A database created by
version 1 upgrades in place, and its plaintext PIN and recovery answer are hashed
the first time version 2 opens it.

Timestamps are stored in **local** time, so a check-in at 11pm belongs to that
day in every chart and streak calculation.

> **Nothing is ever deleted automatically.** The previous version capped `moods`
> and `journals` at seven rows each and silently deleted anything older on every
> write, which quietly destroyed your journal and made "most frequent mood"
> meaningless. Rows now leave only when you ask.

---

## Security and privacy

**What is true:**

- Both services bind to `127.0.0.1`. Nothing is reachable from your network.
- No telemetry, no analytics, no outbound requests of any kind.
- Your PIN and recovery answer are stored as `salt$digest` — SHA-256, a random
  16-byte salt, 4096 iterations. Neither is recoverable from the database file.
- Journal bodies are encrypted before insertion as `v2:<nonce>:<ciphertext>`,
  using a SHA-256 keystream in counter mode with a fresh random nonce per entry.
  Opening the `.db` file in a text editor shows ciphertext.
- The key lives in `database/journal.key`, generated on first run, git-ignored.
  Override it with `CEREVIA_JOURNAL_KEY`.
- Login attempts back off for 60 seconds after five failures.
- Static file serving refuses any path containing `..`.
- All SQL uses bound parameters.

**What is not true, and you should know it:**

- The journal cipher is a hand-rolled construction, not an audited library, and
  it is **not authenticated** — it stops casual reading of the database file, and
  that is all it is for. It will not stop someone determined who has your disk.
- The key sits next to the database. Anyone who can read one can read the other.
  Full-disk encryption is what actually protects this data at rest.
- A 4-digit PIN gates the UI, not the API. Anything already running on your
  machine can call `127.0.0.1:5000` directly. This is a single-user local app,
  and the threat model is "someone glances at my screen or opens my files",
  not "someone has code execution on my laptop".

---

## Testing

```bash
./cerevia test
```

Starts a throwaway stack on spare ports against a temporary database, walks
every flow the UI depends on, and leaves your real data untouched. **83 checks**
across:

- build, boot, and both health endpoints
- static serving, extension-less URLs, 404s, path-traversal refusal
- authentication: wrong PIN, correct PIN, malformed JSON, case-insensitive
  recovery, PIN length rules, and *proof the PIN is hashed on disk*
- mood validation, storage, ordering, deletion and reset
- streak, trend length, distribution, crisis detection and its clearing
- journal round-trip, search hit and miss, *proof the text is encrypted on disk*,
  and proof the journal is no longer truncated at seven rows
- breathing session logging and reset
- profile persistence and the emergency contact reaching the crisis response
- the legacy v1 routes
- companion classification, crisis escalation, helpline content, and the
  technique-offer ladder
- when a model is configured: that safety never reaches it, that ordinary
  messages do, that a truncated generation never leaks raw JSON into the chat,
  and that a rate limit degrades to the local engine instead of failing

Language classification alone can be run on its own:

```bash
python3 ai_chatbot/test_engine.py     # 39 checks
```

---

## Configuration

| Variable | Default | Meaning |
| --- | --- | --- |
| `CEREVIA_PORT` | `5000` | Backend port (auto-advances if taken) |
| `CEREVIA_CHAT_PORT` | `5001` | Companion port |
| `CEREVIA_ROOT` | auto-detected | Project root, if you move the binary |
| `CEREVIA_DB` | `database/mental_health.db` | Database file path |
| `CEREVIA_JOURNAL_KEY` | generated | Journal encryption key |
| `CEREVIA_BACKEND_URL` | `http://127.0.0.1:5000` | Where the companion looks for mood context |
| `CEREVIA_LLM_PROVIDER` | auto | `gemini` to use a model, `none` to force the local engine |
| `CEREVIA_LLM_KEY` | — | Gemini API key (`gemini_api=` in `.env` also works) |
| `CEREVIA_LLM_MODEL` | `gemini-3.5-flash` | Which Gemini model to call |
| `CEREVIA_LLM_TIMEOUT` | `12` | Seconds before giving up and answering locally |

```bash
CEREVIA_PORT=8080 CEREVIA_CHAT_PORT=8081 ./cerevia
```

Content is data, not code — edit it without recompiling:

- `backend/suggestion.json` — daily suggestions per mood
- `backend/eq_resources.json` — toolkit links per mood
- `ai_chatbot/data/templates.json` — the companion's phrasing
- `ai_chatbot/data/techniques.json` — coping exercises
- `ai_chatbot/data/lexicon.json` — sentiment word scores

---

## Troubleshooting

**"Port 5000 is taken"** — on macOS that is AirPlay Receiver. `./cerevia` moves
to the next free port on its own; the URL it chose is in the output. To reclaim
5000, turn AirPlay Receiver off in System Settings.

**"The CEREVIA server is not running"** in the browser — run `./cerevia status`.
If the server is down, `./cerevia logs` will say why.

**The companion says it is offline** — the rest of the app is unaffected. Check
`.run/logs/companion.log`; the most common cause is Python older than 3.8.

**Build fails** — `./cerevia doctor` names what is missing. On macOS install the
command line tools with `xcode-select --install`; on Debian/Ubuntu install
`build-essential`.

**I forgot my PIN** — answer the recovery question on the lock screen. If you
also forgot that, `./cerevia reset` clears the database and restores PIN `1234`
(it deletes your data, and asks first).

**I want to start over** — `./cerevia reset`.

---

## Design notes

The interface is built from the project logo: a deep navy heartbeat line drawn
across a mint and a lavender hemisphere. Those three colours are sampled
straight out of the PNG and everything else is derived from them.

| Role | Light | Dark | Where it comes from |
| --- | --- | --- | --- |
| Brand mint | `#84cec2` | `#96d8cd` | Left hemisphere of the logo |
| Brand lavender | `#b6abe0` | `#c4baea` | Right hemisphere of the logo |
| Brand navy | `#2a435c` | — | The heartbeat line |
| Accent (text) | `#7363ae` | `#b6abe0` | The lavender, darkened until it reads at 4.7:1 |
| Ink on pastel fills | `#17232f` | `#12181d` | The navy — exactly as the logo uses it |
| Paper | `#f0f2f5` | `#0c1013` | Neutral grey tinted with the navy |

- **Two tokens per mood.** Each mood has a pastel *fill* and a darkened *text*
  variant of the same hue, so a label or a chart stroke is always readable.
  Every pairing in the app clears WCAG AA; the tightest is 4.87:1.
- **Navy on pastel, not white on saturated.** Buttons and chips follow the
  logo's own logic — dark ink over a soft fill.
- **Mood tinting is opt-in.** By default the chrome stays mint and lavender and
  colour is reserved for mood *data* — glyphs, intensity pips, the spread bars,
  the trend line. Turn it on in Settings and the whole interface takes the
  colour of your latest check-in instead.
- **A horizon, not a bar chart.** The fortnight view has a dotted line at "even".
  Above it is a better-than-even day. Gaps stay gaps.
- **Drawn faces, not emoji.** Emoji render differently on every platform and
  carry a tone this app does not want. The six mood glyphs are stroke SVGs that
  inherit the mood colour.
- **A breath ribbon.** A hairline at the top of every screen that expands and
  contracts at a resting breathing pace — ambient, never demanding.
- **Typography.** Fraunces for display, Inter for the interface, with full system
  fallbacks so the app never waits on a font to become usable.
- **Accessibility.** Every control is reachable by keyboard with a visible focus
  ring, toasts announce through `aria-live`, the grounding exercise responds to
  Enter and Space, and `prefers-reduced-motion` stops the ambient animation —
  while keeping the breathing orb moving, because there the motion *is* the
  exercise.
- **No build step.** ES modules load straight into the browser. Edit a file,
  refresh, done.

---

## Limits, honestly

- **CEREVIA is not therapy** and does not pretend to be. The companion is a
  structured listener with a safety layer, not a clinician.
- **Out of the box the companion has no language model.** It is a lexicon and a
  rule ladder, so it will occasionally misread sarcasm, mixed feelings and
  unusual phrasing. It is predictable and private, which is the trade being
  made. [Turning on Gemini](#turning-on-gemini) fixes most of that at the cost
  of sending your chat messages to Google — your call, and it is off by default.
- **Single user, single machine.** There is no sync, no multi-profile support and
  no cloud backup. Use *Settings → Export* if you want a copy.
- Helpline numbers are correct for India, the UK, the US and Canada; elsewhere
  the companion points at findahelpline.com.

---

## If you are struggling right now

Please talk to a person, not a program.

- **India** — Tele-MANAS `14416` (free, 24/7) · AASRA `+91-9820466726`
- **UK & Ireland** — Samaritans `116 123`
- **US & Canada** — 988 Suicide & Crisis Lifeline (call or text `988`)
- **Anywhere** — [findahelpline.com](https://findahelpline.com)

If you are in immediate danger, call your local emergency number.

---

<div align="center">

Built by **Harsh**

</div>
