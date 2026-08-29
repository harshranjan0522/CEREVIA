/* ==========================================================================
   CEREVIA — shared runtime
   --------------------------------------------------------------------------
   API client, theming, route guard, toasts, navigation and page chrome.
   Every page imports this once; nothing is duplicated per page any more.
   ========================================================================== */

import { Icons, icon, moodGlyph } from './icons.js';

/* --------------------------------------------------------------------------
   Config
   -------------------------------------------------------------------------- */

// The backend serves this page, so same-origin is the norm. Opening the files
// directly (file://) still works by falling back to the default port.
export const API_BASE = (() => {
    if (window.location.protocol === 'file:') return 'http://127.0.0.1:5000';
    return window.location.origin;
})();

// Live binding: starts at the conventional port and is corrected from
// /api/meta once the page boots, because the launcher moves the companion when
// 5001 is taken. Importers must read `CHAT_BASE` inside a function, not copy it
// at module scope, so they see the corrected value.
export let CHAT_BASE = (() => {
    const override = window.CEREVIA_CHAT_URL;
    if (override) return override;
    const host = window.location.protocol === 'file:' ? '127.0.0.1' : window.location.hostname;
    return `http://${host}:5001`;
})();

function setCompanionPort(port) {
    if (window.CEREVIA_CHAT_URL || !port) return;
    const host = window.location.protocol === 'file:' ? '127.0.0.1' : window.location.hostname;
    CHAT_BASE = `http://${host}:${port}`;
}

export const MOODS = ['Happy', 'Calm', 'Neutral', 'Anxious', 'Angry', 'Sad'];

export const MOOD_COPY = {
    Happy:   'Light, warm, going well',
    Calm:    'Settled and unhurried',
    Neutral: 'Even. Nothing much either way',
    Anxious: 'Wound up, mind ahead of you',
    Angry:   'Hot, something crossed a line',
    Sad:     'Heavy, low, tender',
};

const INTENSITY_WORDS = [
    '', 'barely there', 'faint', 'mild', 'noticeable', 'steady',
    'strong', 'loud', 'heavy', 'overwhelming', 'all-consuming',
];

export const intensityWord = (level) => INTENSITY_WORDS[Math.max(1, Math.min(10, Number(level) || 5))];

/* --------------------------------------------------------------------------
   Storage — namespaced, and never throws when storage is unavailable
   -------------------------------------------------------------------------- */

const KEY = {
    session: 'cerevia.session',
    theme: 'cerevia.theme',
    mood: 'cerevia.lastMood',
    tint: 'cerevia.moodTint',
    chat: 'cerevia.chatSession',
};

export const storage = {
    get(key, fallback = null) {
        try {
            const raw = window.localStorage.getItem(key);
            return raw === null ? fallback : raw;
        } catch {
            return fallback;
        }
    },
    set(key, value) {
        try { window.localStorage.setItem(key, value); } catch { /* private mode */ }
    },
    remove(key) {
        try { window.localStorage.removeItem(key); } catch { /* private mode */ }
    },
};

/* --------------------------------------------------------------------------
   HTTP
   -------------------------------------------------------------------------- */

export class ApiError extends Error {
    constructor(message, status, payload) {
        super(message);
        this.name = 'ApiError';
        this.status = status;
        this.payload = payload;
    }
}

export async function api(path, options = {}) {
    const { method = 'GET', body, timeout = 8000, base = API_BASE } = options;
    const controller = new AbortController();
    const timer = window.setTimeout(() => controller.abort(), timeout);

    try {
        const response = await fetch(`${base}${path}`, {
            method,
            headers: body ? { 'Content-Type': 'application/json' } : undefined,
            body: body ? JSON.stringify(body) : undefined,
            signal: controller.signal,
        });

        const text = await response.text();
        let payload = null;
        if (text) {
            try { payload = JSON.parse(text); } catch { payload = { raw: text }; }
        }

        if (!response.ok) {
            const message = (payload && (payload.error || payload.message)) || `Request failed (${response.status})`;
            throw new ApiError(message, response.status, payload);
        }
        return payload;
    } catch (error) {
        if (error.name === 'AbortError') {
            throw new ApiError('The server did not answer in time.', 0, null);
        }
        if (error instanceof ApiError) throw error;
        throw new ApiError('Could not reach the CEREVIA server.', 0, null);
    } finally {
        window.clearTimeout(timer);
    }
}

/* --------------------------------------------------------------------------
   Theme — light / dark / follow the system
   -------------------------------------------------------------------------- */

const systemDark = window.matchMedia('(prefers-color-scheme: dark)');

export function resolveTheme(preference) {
    if (preference === 'light' || preference === 'dark') return preference;
    return systemDark.matches ? 'dark' : 'light';
}

export function applyTheme(preference) {
    document.documentElement.dataset.theme = resolveTheme(preference);
    document.documentElement.dataset.themePreference = preference || 'system';
}

export function setTheme(preference) {
    storage.set(KEY.theme, preference);
    applyTheme(preference);
    document.dispatchEvent(new CustomEvent('cerevia:theme', { detail: { preference } }));
}

export const currentThemePreference = () => storage.get(KEY.theme, 'system');

systemDark.addEventListener('change', () => {
    if (currentThemePreference() === 'system') applyTheme('system');
});

window.addEventListener('storage', (event) => {
    if (event.key === KEY.theme) applyTheme(event.newValue || 'system');
    if (event.key === KEY.mood || event.key === KEY.tint) applyMoodTint(storage.get(KEY.mood));
});

/* --------------------------------------------------------------------------
   Mood tint — the app takes the colour of your last check-in
   -------------------------------------------------------------------------- */

/**
 * Ambient tinting is opt-in. By default the app chrome stays the logo's
 * lavender and mint; individual mood objects (glyphs, pips, spread bars, the
 * filter chips) always carry their own colour regardless, because there the
 * colour is the data.
 *
 * With tinting on, the whole interface takes the colour of the latest check-in.
 */
export const tintEnabled = () => storage.get(KEY.tint, 'off') === 'on';

export function setTinting(on) {
    storage.set(KEY.tint, on ? 'on' : 'off');
    applyMoodTint(storage.get(KEY.mood));
}

export function applyMoodTint(mood) {
    const known = MOODS.includes(mood) ? mood : null;
    if (known) storage.set(KEY.mood, known);

    if (known && tintEnabled()) {
        document.documentElement.setAttribute('data-mood-colour', known);
    } else {
        document.documentElement.removeAttribute('data-mood-colour');
    }
}

/* --------------------------------------------------------------------------
   Session
   -------------------------------------------------------------------------- */

export const session = {
    isActive: () => storage.get(KEY.session) === 'open',
    open() { storage.set(KEY.session, 'open'); },
    close() {
        storage.remove(KEY.session);
        storage.remove(KEY.chat);
    },
};

const PUBLIC_PAGES = ['', '/', '/index.html', 'index.html'];

function isPublicPage() {
    const path = window.location.pathname;
    const file = path.split('/').pop();
    return PUBLIC_PAGES.includes(path) || PUBLIC_PAGES.includes(file);
}

/** Redirects to the lock screen when a protected page is opened without a session. */
export function guardRoute() {
    if (isPublicPage() || session.isActive()) return true;
    window.location.replace('index.html');
    return false;
}

/* --------------------------------------------------------------------------
   Toasts
   -------------------------------------------------------------------------- */

let toastStack = null;

export function toast(message, tone = 'neutral', duration = 3200) {
    if (!toastStack) {
        toastStack = document.createElement('div');
        toastStack.className = 'toast-stack';
        toastStack.setAttribute('role', 'status');
        toastStack.setAttribute('aria-live', 'polite');
        document.body.appendChild(toastStack);
    }

    const node = document.createElement('div');
    node.className = 'toast';
    node.dataset.tone = tone;
    node.textContent = message;
    toastStack.appendChild(node);

    window.setTimeout(() => {
        node.dataset.leaving = 'true';
        window.setTimeout(() => node.remove(), 260);
    }, duration);
}

/* --------------------------------------------------------------------------
   Small helpers
   -------------------------------------------------------------------------- */

export const $  = (selector, scope = document) => scope.querySelector(selector);
export const $$ = (selector, scope = document) => Array.from(scope.querySelectorAll(selector));

export function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    Object.entries(props).forEach(([key, value]) => {
        if (value === null || value === undefined || value === false) return;
        if (key === 'class') node.className = value;
        else if (key === 'html') node.innerHTML = value;
        else if (key === 'text') node.textContent = value;
        else if (key.startsWith('on') && typeof value === 'function') {
            node.addEventListener(key.slice(2).toLowerCase(), value);
        } else if (key === 'dataset') {
            Object.entries(value).forEach(([dataKey, dataValue]) => { node.dataset[dataKey] = dataValue; });
        } else {
            node.setAttribute(key, value === true ? '' : value);
        }
    });
    (Array.isArray(children) ? children : [children])
        .filter(Boolean)
        .forEach((child) => node.append(child));
    return node;
}

/** Escapes text destined for innerHTML. Journal entries and chat messages are
 *  user-authored, so they are never interpolated raw. */
export function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, (char) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[char]));
}

export function relativeDay(isoDate) {
    if (!isoDate) return '';
    const parsed = new Date(isoDate.replace(' ', 'T'));
    if (Number.isNaN(parsed.getTime())) return isoDate;

    const startOfDay = (d) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
    const days = Math.round((startOfDay(new Date()) - startOfDay(parsed)) / 86400000);

    if (days === 0) return `Today, ${parsed.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}`;
    if (days === 1) return `Yesterday, ${parsed.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}`;
    if (days < 7) return parsed.toLocaleDateString([], { weekday: 'long', hour: 'numeric', minute: '2-digit' });
    return parsed.toLocaleDateString([], { day: 'numeric', month: 'short', year: 'numeric' });
}

export function greetingForHour(date = new Date()) {
    const hour = date.getHours();
    if (hour < 5)  return 'Still awake';
    if (hour < 12) return 'Good morning';
    if (hour < 17) return 'Good afternoon';
    if (hour < 21) return 'Good evening';
    return 'Winding down';
}

export function debounce(fn, wait = 220) {
    let timer = null;
    return (...args) => {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => fn(...args), wait);
    };
}

/* --------------------------------------------------------------------------
   Page chrome — ambience, rail navigation, service status
   -------------------------------------------------------------------------- */

// The project logo. Kept as markup in one place so every surface that shows the
// brand uses the same asset.
export const LOGO = '<img src="Mental Health Tracker LOGO.png" alt="CEREVIA">';

const NAV = [
    { href: 'dashboard.html', label: 'Today',    key: 'today' },
    { href: 'mood.html',      label: 'Check in', key: 'mood' },
    { href: 'journal.html',   label: 'Journal',  key: 'journal' },
    { href: 'breathe.html',   label: 'Breathe',  key: 'breathe' },
    { href: 'eq.html',        label: 'Toolkit',  key: 'toolkit' },
    { href: 'settings.html',  label: 'Settings', key: 'settings' },
];

function ambience() {
    if (!$('.weather-field')) {
        document.body.prepend(el('div', { class: 'weather-field', 'aria-hidden': 'true' }));
    }
    if (!$('.breath-ribbon')) {
        document.body.prepend(el('div', { class: 'breath-ribbon', 'aria-hidden': 'true' }));
    }
}

function buildRail(activeHref) {
    const links = NAV.map((item) => {
        const active = activeHref === item.href;
        return `<a class="rail__link" href="${item.href}" ${active ? 'aria-current="page"' : ''}>
                    ${icon(item.key)}<span>${item.label}</span>
                </a>`;
    }).join('');

    return `
        <nav class="rail" aria-label="Primary">
            <a class="rail__brand" href="dashboard.html">
                <span class="rail__mark">${LOGO}</span>
                <span>
                    <span class="rail__word">CEREVIA</span>
                    <span class="rail__sub">inner weather</span>
                </span>
            </a>
            <div class="rail__nav">${links}</div>
            <div class="rail__spacer"></div>
            <div class="rail__foot">
                <p class="rail__status"><span class="status-dot" data-state="pending" id="statusBackend"></span> <span id="statusBackendText">Checking server…</span></p>
                <p class="rail__status"><span class="status-dot" data-state="pending" id="statusChat"></span> <span id="statusChatText">Checking companion…</span></p>
                <button class="btn btn--quiet" id="signOut" type="button">${icon('exit')} Sign out</button>
            </div>
        </nav>`;
}

async function reportStatus() {
    const setState = (dotId, textId, state, label) => {
        const dot = document.getElementById(dotId);
        const text = document.getElementById(textId);
        if (dot) dot.dataset.state = state;
        if (text) text.textContent = label;
    };

    try {
        await api('/api/health', { timeout: 3000 });
        setState('statusBackend', 'statusBackendText', 'up', 'Server connected');
    } catch {
        setState('statusBackend', 'statusBackendText', 'down', 'Server offline');
    }

    try {
        await api('/health', { base: CHAT_BASE, timeout: 3000 });
        setState('statusChat', 'statusChatText', 'up', 'Companion ready');
    } catch {
        setState('statusChat', 'statusChatText', 'down', 'Companion offline');
    }
}

/**
 * Boots a signed-in page: guards the route, paints the chrome, restores the
 * theme and mood tint, and mounts the companion drawer.
 */
export async function bootPage({ active, requireAuth = true } = {}) {
    applyTheme(currentThemePreference());
    applyMoodTint(storage.get(KEY.mood));

    if (requireAuth && !guardRoute()) return false;

    ambience();

    // Learn where the companion is listening before anything tries to talk to it.
    try {
        const meta = await api('/api/meta', { timeout: 3000 });
        setCompanionPort(meta.companionPort);
    } catch { /* offline: the default port is the best guess available */ }

    const shell = $('.shell');
    if (shell && !$('.rail')) {
        shell.insertAdjacentHTML('afterbegin', buildRail(active));
        const signOut = document.getElementById('signOut');
        if (signOut) {
            signOut.addEventListener('click', () => {
                session.close();
                window.location.href = 'index.html';
            });
        }
    }

    if (!$('.skip-link')) {
        document.body.prepend(el('a', { class: 'skip-link', href: '#main', text: 'Skip to content' }));
    }

    const { mountCompanion } = await import('./companion.js');
    mountCompanion();

    reportStatus();
    return true;
}

export { Icons, icon, moodGlyph, KEY };
