/* CEREVIA — Toolkit */

import {
    api, applyMoodTint, bootPage, CHAT_BASE, escapeHtml, icon, moodGlyph, toast, $,
} from './core.js';

const CATEGORIES = [
    { key: 'articles',   title: 'Read',  kind: 'link', hint: 'Ten minutes each, and worth the ten minutes.' },
    { key: 'videos',     title: 'Watch', kind: 'link', hint: 'Short, calm, no shouting.' },
    { key: 'activities', title: 'Do',    kind: 'text', hint: 'Small enough to actually start.' },
    { key: 'songs',      title: 'Listen', kind: 'link', hint: 'For when words are too much.' },
];

const started = await bootPage({ active: 'eq.html' });
if (started) {
    $('#refreshBtn').addEventListener('click', () => load(true));
    await Promise.all([load(), loadTechniques()]);
}

async function load(announce = false) {
    try {
        const data = await api('/api/eq');

        const mood = data.latestMood || 'Neutral';
        applyMoodTint(mood);
        document.querySelectorAll('.card--tabbed').forEach((card) => { card.dataset.moodColour = mood; });

        $('#moodGlyph').innerHTML = moodGlyph(mood);
        $('#moodName').textContent = data.hasMood ? data.displayMood : 'No check-in yet';
        $('#moodMeta').textContent = data.hasMood
            ? `Intensity ${data.level}/10 · logged ${data.date}`
            : 'Showing the balanced starter set.';
        $('#moodExplain').textContent = data.hasMood
            ? `These were picked for a ${String(data.displayMood).toLowerCase()} state — the aim is to meet you where you are, not to talk you out of it.`
            : 'Log a mood on the check-in page and this whole section re-tunes to it.';

        if (data.error) toast(data.error, 'bad');
        renderResources(data.resources || {});
        if (announce) toast('Refreshed', 'good');
    } catch {
        $('#moodName').textContent = 'Server offline';
        $('#moodMeta').textContent = 'Start CEREVIA with ./cerevia and refresh.';
        renderResources({});
    }
}

function renderResources(resources) {
    $('#resourceInner').innerHTML = CATEGORIES.map((category) => {
        const items = Array.isArray(resources[category.key]) ? resources[category.key].filter(Boolean) : [];

        const body = items.length
            ? `<ul class="resource-list">${items.map((item, index) => {
                const label = escapeHtml(prettyLabel(item, category.kind));
                if (category.kind !== 'link') {
                    return `<li class="resource"><span class="resource__index">${index + 1}</span><span class="resource__text">${label}</span></li>`;
                }
                return `<li><a class="resource" href="${escapeHtml(item)}" target="_blank" rel="noopener noreferrer">
                            <span class="resource__index">${index + 1}</span>
                            <span class="resource__text">${label}</span>
                            <span class="resource__arrow">${icon('external')}</span>
                        </a></li>`;
            }).join('')}</ul>`
            : `<p class="field__hint">Nothing here for this mood yet. Add entries to <code>backend/eq_resources.json</code>.</p>`;

        return `<section class="card span-3">
                    <p class="card__label">${category.title}</p>
                    <p class="field__hint" style="margin:-8px 0 12px">${category.hint}</p>
                    ${body}
                </section>`;
    }).join('');
}

/** Turns a bare URL into something readable without hiding where it goes. */
function prettyLabel(value, kind) {
    if (kind !== 'link') return value;
    try {
        const url = new URL(value);
        const host = url.hostname.replace(/^www\./, '');
        if (host.includes('youtube') || host.includes('youtu.be')) return `YouTube · ${url.searchParams.get('v') || 'video'}`;
        const slug = url.pathname.split('/').filter(Boolean).pop() || '';
        const words = slug.replace(/[-_]/g, ' ').replace(/\.\w+$/, '');
        return words ? `${host} · ${words}` : host;
    } catch {
        return value;
    }
}

async function loadTechniques() {
    const chips = $('#techniqueChips');
    const detail = $('#techniqueDetail');

    try {
        const library = await api('/techniques', { base: CHAT_BASE, timeout: 4000 });
        const keys = Object.keys(library);

        if (!keys.length) throw new Error('empty');

        chips.innerHTML = keys.map((key, index) =>
            `<button class="chip" type="button" data-key="${key}" aria-pressed="${index === 0}">${escapeHtml(library[key].title)}</button>`).join('');

        const show = (key) => {
            const data = library[key];
            detail.innerHTML = `
                <p class="technique__kicker">${escapeHtml(data.subtitle || '')} · ${escapeHtml(data.duration || '')}</p>
                <p class="card__note" style="margin-bottom:10px">${escapeHtml(data.why || '')}</p>
                <ol class="technique__steps">${(data.steps || []).map((s) => `<li>${escapeHtml(s)}</li>`).join('')}</ol>`;
        };

        chips.addEventListener('click', (event) => {
            const chip = event.target.closest('.chip');
            if (!chip) return;
            chips.querySelectorAll('.chip').forEach((c) => c.setAttribute('aria-pressed', String(c === chip)));
            show(chip.dataset.key);
        });

        show(keys[0]);
    } catch {
        chips.innerHTML = '';
        detail.innerHTML = '<p class="field__hint">The companion service is not running, so its technique library is unavailable. Start it with <code>./cerevia</code>.</p>';
    }
}
