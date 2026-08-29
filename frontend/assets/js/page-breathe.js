/* CEREVIA — Breathing room */

import { api, bootPage, escapeHtml, icon, toast, $, $$ } from './core.js';

/* Each phase is [label, seconds, orb state]. A null-length phase is skipped. */
const TECHNIQUES = {
    box: {
        name: 'Box',
        meta: '4 in · 4 hold · 4 out · 4 hold',
        blurb: 'Even and predictable. Good when you need to think clearly again.',
        phases: [['Breathe in', 4, 'in'], ['Hold', 4, 'hold'], ['Breathe out', 4, 'out'], ['Hold', 4, 'holdout']],
    },
    '478': {
        name: '4-7-8',
        meta: '4 in · 7 hold · 8 out',
        blurb: 'The long exhale is the fastest way to down-shift. Four rounds is plenty at first.',
        phases: [['Breathe in', 4, 'in'], ['Hold', 7, 'hold'], ['Breathe out', 8, 'out']],
    },
    coherent: {
        name: 'Coherent',
        meta: '5.5 in · 5.5 out',
        blurb: 'No holds, just a steady rhythm. The easiest one to keep going for ten minutes.',
        phases: [['Breathe in', 5.5, 'in'], ['Breathe out', 5.5, 'out']],
    },
};

const GROUNDING = [
    [5, 'things you can see', 'Look around properly. Name them out loud if you can.'],
    [4, 'things you can feel', 'The chair, your feet on the floor, fabric, temperature.'],
    [3, 'things you can hear', 'Including the quiet ones underneath the obvious ones.'],
    [2, 'things you can smell', 'Or two smells you like, if there is nothing in the air.'],
    [1, 'thing you can taste', 'Or one thing you are glad about being here for.'],
];

let technique = 'box';
let timer = null;
let phaseIndex = 0;
let cycles = 0;
let sessionSeconds = 0;
let startedAt = 0;

const started = await bootPage({ active: 'breathe.html' });
if (started) {
    // Deep links from the companion ("breathe.html?technique=478") open the
    // exercise it just suggested.
    const requested = new URLSearchParams(window.location.search).get('technique');
    if (requested && TECHNIQUES[requested]) technique = requested;
    if (requested === 'grounding') {
        window.setTimeout(() => $('#grounding').scrollIntoView({ behavior: 'smooth', block: 'center' }), 300);
    }

    buildTechniques();
    buildGrounding();

    $('#startBtn').addEventListener('click', start);
    $('#stopBtn').addEventListener('click', () => stop(true));
    $('#resetSessions').addEventListener('click', resetTotals);
    $('#resetGrounding').addEventListener('click', buildGrounding);

    // Leaving mid-session should still bank the cycles already completed.
    window.addEventListener('beforeunload', () => { if (timer) logSession(); });

    await loadTotals();
}

/* --------------------------------------------------------------------------
   Technique picker
   -------------------------------------------------------------------------- */

function buildTechniques() {
    const row = $('#techniqueRow');
    row.innerHTML = Object.entries(TECHNIQUES).map(([key, data]) =>
        `<button class="chip" type="button" data-technique="${key}" aria-pressed="${key === technique}">${data.name}</button>`).join('');

    row.addEventListener('click', (event) => {
        const chip = event.target.closest('.chip');
        if (!chip) return;
        stop(false);
        technique = chip.dataset.technique;
        $$('#techniqueRow .chip').forEach((c) =>
            c.setAttribute('aria-pressed', String(c.dataset.technique === technique)));
        paintMeta();
    });

    paintMeta();
}

function paintMeta() {
    // The blurb lives under the picker, not inside the orb — the orb only ever
    // shows the phase word and the count, so it stays readable at every size.
    const data = TECHNIQUES[technique];
    $('#patternMeta').textContent = data.meta;
    $('#techniqueBlurb').textContent = data.blurb;
    $('#orbCount').textContent = timer ? '' : 'Press start when you are';
}

/* --------------------------------------------------------------------------
   The exercise
   -------------------------------------------------------------------------- */

function start() {
    if (timer) return;

    phaseIndex = 0;
    cycles = 0;
    startedAt = Date.now();
    $('#startBtn').disabled = true;
    $('#stopBtn').disabled = false;
    $('#cycleLabel').textContent = '0 cycles this session';

    runPhase();
}

function runPhase() {
    const phases = TECHNIQUES[technique].phases;
    const [label, seconds, state] = phases[phaseIndex];

    const orb = $('#orb');
    orb.dataset.phase = state;
    // The orb's own transition drives the visual, so it is told how long the
    // current phase lasts rather than being animated on a fixed 4s curve.
    orb.style.transitionDuration = `${seconds}s`;

    $('#orbWord').textContent = label;

    let remaining = Math.ceil(seconds);
    $('#orbCount').textContent = `${remaining}`;

    const countdown = window.setInterval(() => {
        remaining -= 1;
        if (remaining > 0) $('#orbCount').textContent = `${remaining}`;
    }, 1000);

    timer = window.setTimeout(() => {
        window.clearInterval(countdown);
        phaseIndex = (phaseIndex + 1) % phases.length;

        if (phaseIndex === 0) {
            cycles += 1;
            $('#cycleLabel').textContent = `${cycles} cycle${cycles === 1 ? '' : 's'} this session`;
        }
        runPhase();
    }, seconds * 1000);
}

async function stop(announce) {
    if (!timer) return;

    window.clearTimeout(timer);
    timer = null;
    sessionSeconds = Math.round((Date.now() - startedAt) / 1000);

    const orb = $('#orb');
    orb.dataset.phase = 'idle';
    orb.style.transitionDuration = '';
    $('#orbWord').textContent = cycles ? 'Well done' : 'Ready';
    paintMeta();

    $('#startBtn').disabled = false;
    $('#stopBtn').disabled = true;

    if (cycles > 0) {
        await logSession();
        if (announce) toast(`${cycles} cycle${cycles === 1 ? '' : 's'} logged`, 'good');
    }
    cycles = 0;
}

async function logSession() {
    try {
        await api('/api/breathing', {
            method: 'POST',
            body: { technique, cycles, seconds: sessionSeconds },
        });
        await loadTotals();
    } catch {
        /* Offline: the session simply is not banked. Not worth interrupting for. */
    }
}

async function loadTotals() {
    try {
        const totals = await api('/api/breathing');
        $('#totalCycles').textContent = totals.cycles || 0;
        // A short first session should not read as "0 minutes in total".
        const seconds = totals.seconds || 0;
        const spent = seconds < 60
            ? `${seconds} seconds`
            : `${Math.round(seconds / 60)} minute${Math.round(seconds / 60) === 1 ? '' : 's'}`;

        $('#totalSessions').textContent = totals.sessions
            ? `${totals.sessions} session${totals.sessions === 1 ? '' : 's'}, ${spent} in total.`
            : 'No sessions logged yet.';
    } catch {
        $('#totalSessions').textContent = 'Could not read your session history.';
    }
}

async function resetTotals() {
    if (!window.confirm('Reset your breathing session count to zero?')) return;
    try {
        await api('/api/breathing/reset', { method: 'POST' });
        await loadTotals();
        toast('Session count reset');
    } catch {
        toast('Could not reset the count', 'bad');
    }
}

/* --------------------------------------------------------------------------
   Grounding
   -------------------------------------------------------------------------- */

function buildGrounding() {
    const list = $('#grounding');
    list.innerHTML = GROUNDING.map(([count, sense, hint]) => `
        <li class="ground-item" data-done="false" tabindex="0" role="button" aria-pressed="false">
            <span class="ground-item__num">${count}</span>
            <span>
                <span class="ground-item__text">${escapeHtml(sense)}</span>
                <span class="ground-item__sense">${escapeHtml(hint)}</span>
            </span>
        </li>`).join('');

    $('#groundingNote').textContent = '';

    const toggle = (item) => {
        const done = item.dataset.done === 'true';
        item.dataset.done = String(!done);
        item.setAttribute('aria-pressed', String(!done));

        const finished = $$('.ground-item', list).filter((node) => node.dataset.done === 'true').length;
        $('#groundingNote').textContent = finished === GROUNDING.length
            ? 'All five. Notice whether anything shifted, even slightly.'
            : `${finished} of ${GROUNDING.length}`;
    };

    list.addEventListener('click', (event) => {
        const item = event.target.closest('.ground-item');
        if (item) toggle(item);
    });

    list.addEventListener('keydown', (event) => {
        if (event.key !== 'Enter' && event.key !== ' ') return;
        const item = event.target.closest('.ground-item');
        if (!item) return;
        event.preventDefault();
        toggle(item);
    });
}
