/* ==========================================================================
   CEREVIA — companion drawer
   --------------------------------------------------------------------------
   The chat surface. Differences from the previous widget:
     * conversation history survives closing the panel and reloading the page
     * a typing indicator, so a reply never appears out of nowhere
     * coping techniques render as structured cards rather than a wall of text
     * a distinct, unmissable treatment when the reply is a safety response
     * quick-reply chips for people who cannot find words
   ========================================================================== */

import { CHAT_BASE, api, el, escapeHtml, icon, LOGO, storage, toast, KEY } from './core.js';

const HISTORY_KEY = 'cerevia.chatHistory';
const MAX_HISTORY = 60;

const QUICK_REPLIES = [
    "I'm anxious",
    'Today was hard',
    "I can't sleep",
    'Something good happened',
    'Help me calm down',
];

let root = null;
let logNode = null;
let inputNode = null;
let sendNode = null;
let scrim = null;
let sending = false;

/* --------------------------------------------------------------------------
   Persistence
   -------------------------------------------------------------------------- */

function loadHistory() {
    try {
        const parsed = JSON.parse(storage.get(HISTORY_KEY, '[]'));
        return Array.isArray(parsed) ? parsed.slice(-MAX_HISTORY) : [];
    } catch {
        return [];
    }
}

function saveHistory(entries) {
    storage.set(HISTORY_KEY, JSON.stringify(entries.slice(-MAX_HISTORY)));
}

function pushHistory(entry) {
    const entries = loadHistory();
    entries.push(entry);
    saveHistory(entries);
}

function sessionId() {
    let id = storage.get(KEY.chat);
    if (!id) {
        id = `s-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
        storage.set(KEY.chat, id);
    }
    return id;
}

/* --------------------------------------------------------------------------
   Rendering
   -------------------------------------------------------------------------- */

function renderBubble({ role, text, risk }) {
    const isAlert = role === 'them' && (risk === 'crisis' || risk === 'high');
    const node = el('div', {
        class: `bubble bubble--${role === 'me' ? 'me' : 'them'}${isAlert ? ' bubble--alert' : ''}`,
        text,
    });
    logNode.append(node);
    return node;
}

function renderTechnique(technique) {
    if (!technique) return;

    const steps = (technique.steps || [])
        .map((step) => `<li>${escapeHtml(step)}</li>`)
        .join('');

    const node = el('div', {
        class: 'technique',
        html: `
            <p class="technique__kicker">Try this · ${escapeHtml(technique.duration || '2 minutes')}</p>
            <h3 class="technique__title">${escapeHtml(technique.title || 'A small exercise')}</h3>
            <p class="technique__why">${escapeHtml(technique.why || '')}</p>
            <ol class="technique__steps">${steps}</ol>
            <div class="technique__foot">
                <a class="btn btn--accent btn--sm" href="${escapeHtml(technique.page || 'breathe.html')}">Open the breathing room</a>
            </div>`,
    });
    logNode.append(node);
}

function renderActions(actions) {
    if (!actions || !actions.length) return;
    const row = el('div', { class: 'companion__quick' });
    actions.forEach((action) => {
        row.append(el('a', { class: 'chip', href: action.href, text: action.label }));
    });
    logNode.append(row);
}

function showTyping() {
    const node = el('div', {
        class: 'bubble bubble--them',
        html: '<span class="typing"><span></span><span></span><span></span></span>',
    });
    node.dataset.typing = 'true';
    logNode.append(node);
    scrollToEnd();
    return node;
}

function scrollToEnd() {
    window.requestAnimationFrame(() => {
        logNode.scrollTop = logNode.scrollHeight;
    });
}

function replayHistory() {
    const entries = loadHistory();
    if (!entries.length) {
        renderBubble({
            role: 'them',
            text: "I'm the CEREVIA companion. I run on this computer only — nothing you type here leaves your machine.\n\nHow are you doing right now?",
        });
        return;
    }
    entries.forEach((entry) => {
        renderBubble(entry);
        if (entry.technique) renderTechnique(entry.technique);
    });
    scrollToEnd();
}

/* --------------------------------------------------------------------------
   Sending
   -------------------------------------------------------------------------- */

async function send(rawText) {
    const text = (rawText ?? inputNode.value).trim();
    if (!text || sending) return;

    sending = true;
    sendNode.disabled = true;
    inputNode.value = '';
    inputNode.style.height = 'auto';

    renderBubble({ role: 'me', text });
    pushHistory({ role: 'me', text });
    scrollToEnd();

    const typing = showTyping();

    try {
        const reply = await api('/chat', {
            base: CHAT_BASE,
            method: 'POST',
            body: { message: text, sessionId: sessionId() },
            timeout: 12000,
        });

        typing.remove();

        const entry = {
            role: 'them',
            text: reply.response || 'I did not quite catch that. Could you say it another way?',
            risk: reply.risk,
            technique: reply.technique || null,
        };

        renderBubble(entry);
        renderTechnique(entry.technique);
        renderActions(reply.actions);
        pushHistory(entry);

        if (reply.risk === 'crisis' || reply.risk === 'high') {
            setState('Safety response — please read it all');
        } else {
            setState(reply.emotion ? `Heard: ${reply.emotion.replace('_', ' ')}` : 'Listening');
        }
    } catch (error) {
        typing.remove();
        renderBubble({
            role: 'them',
            text: "I can't reach the companion service right now. It runs separately from the rest of CEREVIA — starting it with ./cerevia will bring it back.\n\nEverything else in the app still works.",
        });
        setState('Companion offline');
    } finally {
        sending = false;
        sendNode.disabled = false;
        scrollToEnd();
        inputNode.focus();
    }
}

function setState(label) {
    const node = document.getElementById('companionState');
    if (node) node.textContent = label;
}

/* --------------------------------------------------------------------------
   Open / close
   -------------------------------------------------------------------------- */

export function openCompanion() {
    if (!root) return;
    root.dataset.open = 'true';
    scrim.dataset.open = 'true';
    root.setAttribute('aria-hidden', 'false');
    document.body.style.overflow = 'hidden';
    window.setTimeout(() => inputNode.focus(), 320);
    scrollToEnd();
}

export function closeCompanion() {
    if (!root) return;
    root.dataset.open = 'false';
    scrim.dataset.open = 'false';
    root.setAttribute('aria-hidden', 'true');
    document.body.style.overflow = '';
}

async function clearConversation() {
    storage.remove(HISTORY_KEY);
    logNode.innerHTML = '';
    try {
        await api('/reset', { base: CHAT_BASE, method: 'POST', body: { sessionId: sessionId() }, timeout: 4000 });
    } catch {
        /* The service may be down; the local history is cleared either way. */
    }
    storage.remove(KEY.chat);
    replayHistory();
    setState('Fresh start');
    toast('Conversation cleared');
}

/* --------------------------------------------------------------------------
   Mount
   -------------------------------------------------------------------------- */

/** Tells the user which engine is answering, and that safety stays local. */
async function reportEngine() {
    try {
        const health = await api('/health', { base: CHAT_BASE, timeout: 4000 });
        const llm = health.llm || {};
        if (llm.enabled && !llm.lastError) {
            setState(`${String(llm.model || llm.provider).split('-').slice(0, 2).join(' ')} · safety stays local`);
        } else if (llm.enabled) {
            setState('Local engine (model unreachable)');
        } else {
            setState('Local engine · fully offline');
        }
    } catch {
        setState('Companion offline');
    }
}

export function mountCompanion() {
    if (document.getElementById('companion')) return;

    const opener = el('button', {
        class: 'companion-open',
        type: 'button',
        id: 'companionOpen',
        'aria-controls': 'companion',
        html: `<span class="companion-open__pulse"></span> Talk it through`,
    });

    scrim = el('div', { class: 'scrim', id: 'companionScrim' });

    root = el('aside', {
        class: 'companion',
        id: 'companion',
        'aria-label': 'CEREVIA companion',
        'aria-hidden': 'true',
        html: `
            <header class="companion__head">
                <span class="companion__avatar">${LOGO}</span>
                <span>
                    <span class="companion__name">Companion</span>
                    <span class="companion__state" id="companionState">Listening</span>
                </span>
                <button class="btn btn--quiet companion__close" id="companionClear" type="button" title="Clear this conversation">${icon('trash')}</button>
                <button class="btn btn--quiet" id="companionClose" type="button" aria-label="Close companion">${icon('close')}</button>
            </header>
            <div class="companion__log" id="companionLog" role="log" aria-live="polite"></div>
            <div class="companion__quick" id="companionQuick"></div>
            <form class="companion__compose" id="companionForm">
                <textarea class="companion__input" id="companionInput" rows="1"
                          placeholder="Say anything. There's no wrong way in."
                          aria-label="Message the companion"></textarea>
                <button class="companion__send" id="companionSend" type="submit" aria-label="Send">${icon('send')}</button>
            </form>
            <p class="companion__disclaimer">A supportive listener, not a therapist. In an emergency, call your local services.</p>`,
    });

    document.body.append(opener, scrim, root);

    logNode = document.getElementById('companionLog');
    inputNode = document.getElementById('companionInput');
    sendNode = document.getElementById('companionSend');

    const quick = document.getElementById('companionQuick');
    QUICK_REPLIES.forEach((label) => {
        quick.append(el('button', {
            class: 'chip',
            type: 'button',
            text: label,
            onClick: () => send(label),
        }));
    });

    opener.addEventListener('click', openCompanion);
    scrim.addEventListener('click', closeCompanion);
    document.getElementById('companionClose').addEventListener('click', closeCompanion);
    document.getElementById('companionClear').addEventListener('click', clearConversation);

    document.getElementById('companionForm').addEventListener('submit', (event) => {
        event.preventDefault();
        send();
    });

    // Enter sends, Shift+Enter makes a new line — the convention people expect.
    inputNode.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' && !event.shiftKey) {
            event.preventDefault();
            send();
        }
    });

    inputNode.addEventListener('input', () => {
        inputNode.style.height = 'auto';
        inputNode.style.height = `${Math.min(inputNode.scrollHeight, 120)}px`;
    });

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape' && root.dataset.open === 'true') closeCompanion();
    });

    replayHistory();
    reportEngine();
}

export { send as sendToCompanion };
