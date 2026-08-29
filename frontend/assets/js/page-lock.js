/* CEREVIA — lock screen */

import { api, applyMoodTint, applyTheme, currentThemePreference, el, icon, LOGO, session, storage, toast, $, KEY } from './core.js';

applyTheme(currentThemePreference());
applyMoodTint(storage.get(KEY.mood));

/* Ambience */
document.body.prepend(el('div', { class: 'weather-field', 'aria-hidden': 'true' }));
document.body.prepend(el('div', { class: 'breath-ribbon', 'aria-hidden': 'true' }));

$('#brandMark').innerHTML = LOGO;

/* Value props */
const POINTS = [
    ['shield', 'Everything is stored in a local file. No account, no cloud, no telemetry.'],
    ['mood',   'Six honest moods and an intensity, not a five-star rating of your day.'],
    ['heart',  'A companion that listens, and knows when to point you at a real person.'],
    ['breathe','Breathing and grounding exercises for the moments that need them.'],
];

$('#lockPoints').innerHTML = POINTS
    .map(([name, text]) => `<li>${icon(name)}<span>${text}</span></li>`)
    .join('');

/* PIN dots mirror the length of the entry without revealing the value. */
const pinInput = $('#pin');
const dots = $('#pinDots');

function paintDots() {
    const filled = pinInput.value.length;
    const total = Math.max(4, filled);
    dots.innerHTML = Array.from({ length: total }, (_, index) =>
        `<span class="pin-dot" data-filled="${index < filled}"></span>`).join('');
}

pinInput.addEventListener('input', paintDots);
paintDots();

/* Sign in */
const help = $('#pinHelp');
const signInBtn = $('#signInBtn');

$('#signInForm').addEventListener('submit', async (event) => {
    event.preventDefault();
    const pin = pinInput.value.trim();

    if (!pin) {
        help.textContent = 'Enter your PIN to continue.';
        pinInput.focus();
        return;
    }

    signInBtn.disabled = true;
    signInBtn.textContent = 'Checking…';

    try {
        const result = await api('/api/auth/login', { method: 'POST', body: { pin } });
        if (!result.success) throw new Error(result.error || 'That PIN does not match.');

        session.open();
        help.textContent = 'Welcome back.';
        window.location.href = 'dashboard.html';
    } catch (error) {
        const tooMany = error.status === 429;
        help.textContent = tooMany
            ? 'Too many attempts. Wait a moment and try again.'
            : (error.status === 0
                ? 'The CEREVIA server is not running. Start it with ./cerevia and try again.'
                : 'That PIN does not match.');
        pinInput.select();
        signInBtn.disabled = false;
        signInBtn.textContent = 'Unlock';
    }
});

/* Recovery */
const forgotToggle = $('#forgotToggle');
const forgotPanel = $('#forgotPanel');
let questionLoaded = false;

forgotToggle.addEventListener('click', async () => {
    const open = forgotPanel.dataset.open === 'true';
    forgotPanel.dataset.open = open ? 'false' : 'true';
    forgotToggle.setAttribute('aria-expanded', String(!open));

    if (open || questionLoaded) return;

    try {
        const { question } = await api('/api/auth/question');
        $('#securityQuestion').textContent = question;
        questionLoaded = true;
    } catch {
        $('#securityQuestion').textContent = 'Could not load your recovery question — is the server running?';
    }
});

$('#resetBtn').addEventListener('click', async () => {
    const answer = $('#securityAnswer').value.trim();
    const newPin = $('#newPin').value.trim();
    const resetHelp = $('#resetHelp');

    if (!answer) { resetHelp.textContent = 'Answer the recovery question first.'; return; }
    if (newPin.length < 4) { resetHelp.textContent = 'Pick a PIN of at least 4 characters.'; return; }

    try {
        const result = await api('/api/auth/reset', { method: 'POST', body: { answer, newPin } });
        if (!result.success) throw new Error('Reset failed.');
        resetHelp.textContent = 'PIN updated. Sign in with it now.';
        toast('PIN reset', 'good');
        $('#securityAnswer').value = '';
        $('#newPin').value = '';
        pinInput.focus();
    } catch (error) {
        resetHelp.textContent = error.status === 401
            ? 'That answer does not match the one on file.'
            : 'Could not reset the PIN. Is the server running?';
    }
});

pinInput.focus();
