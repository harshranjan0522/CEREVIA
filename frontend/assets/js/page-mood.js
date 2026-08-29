/* CEREVIA — Check in */

import {
    api, applyMoodTint, bootPage, escapeHtml, icon, intensityWord, moodGlyph,
    relativeDay, toast, $, $$, MOODS, MOOD_COPY,
} from './core.js';
import { renderSpread } from './sky.js';

const MOOD_COLOURS = Object.fromEntries(MOODS.map((m) => [m, `var(--mood-${m.toLowerCase()})`]));

const TAGS = ['work', 'study', 'family', 'friends', 'health', 'sleep', 'money', 'alone', 'exercise', 'weather'];

let selectedMood = null;
let selectedTags = new Set();
let filter = 'all';
let entries = [];

const started = await bootPage({ active: 'mood.html' });
if (started) {
    buildDial();
    buildTags();
    buildFilters();
    wireIntensity();

    $('#saveMood').addEventListener('click', save);
    $('#clearAll').addEventListener('click', clearAll);

    await load();
}

/* --------------------------------------------------------------------------
   Composer
   -------------------------------------------------------------------------- */

function buildDial() {
    const dial = $('#dial');
    dial.innerHTML = MOODS.map((mood) => `
        <button class="dial__option" type="button" data-mood="${mood}" data-mood-colour="${mood}"
                aria-pressed="false" aria-label="${mood} — ${MOOD_COPY[mood]}">
            <span class="dial__glyph">${moodGlyph(mood)}</span>
            <span class="dial__name">${mood}</span>
        </button>`).join('');

    dial.addEventListener('click', (event) => {
        const button = event.target.closest('.dial__option');
        if (!button) return;
        selectMood(button.dataset.mood);
    });
}

function selectMood(mood) {
    selectedMood = mood;
    $$('.dial__option').forEach((option) => {
        option.setAttribute('aria-pressed', String(option.dataset.mood === mood));
    });
    $('#moodBlurb').textContent = MOOD_COPY[mood] || '';

    // Tint the composer card and the whole page as soon as a mood is chosen,
    // so the choice is felt rather than just recorded.
    $('#dial').closest('.card').dataset.moodColour = mood;
    applyMoodTint(mood);
}

function buildTags() {
    const row = $('#tagRow');
    row.innerHTML = TAGS.map((tag) =>
        `<button class="chip" type="button" data-tag="${tag}" aria-pressed="false">${tag}</button>`).join('');

    row.addEventListener('click', (event) => {
        const chip = event.target.closest('.chip');
        if (!chip) return;
        const tag = chip.dataset.tag;
        if (selectedTags.has(tag)) selectedTags.delete(tag);
        else selectedTags.add(tag);
        chip.setAttribute('aria-pressed', String(selectedTags.has(tag)));
    });
}

function wireIntensity() {
    const slider = $('#level');

    const paint = () => {
        const value = Number(slider.value);
        $('#levelValue').textContent = value;
        $('#levelWord').textContent = intensityWord(value);
        // The filled part of the track is driven by a CSS variable so the
        // gradient stays in step with the thumb on every browser.
        slider.style.setProperty('--fill', `${((value - 1) / 9) * 100}%`);
    };

    slider.addEventListener('input', paint);
    paint();
}

async function save() {
    if (!selectedMood) {
        toast('Pick a mood first', 'bad');
        $('#dial').querySelector('.dial__option')?.focus();
        return;
    }

    const button = $('#saveMood');
    button.disabled = true;
    button.textContent = 'Saving…';

    try {
        await api('/api/mood', {
            method: 'POST',
            body: {
                mood: selectedMood,
                level: Number($('#level').value),
                note: $('#note').value.trim(),
                tags: [...selectedTags],
            },
        });

        toast('Check-in saved', 'good');
        $('#note').value = '';
        selectedTags.clear();
        $$('#tagRow .chip').forEach((chip) => chip.setAttribute('aria-pressed', 'false'));
        await load();
    } catch (error) {
        toast(error.message || 'Could not save that check-in', 'bad');
    } finally {
        button.disabled = false;
        button.textContent = 'Save this check-in';
    }
}

async function clearAll() {
    const ok = window.confirm(
        'Delete every mood check-in?\n\nThis cannot be undone, and the dashboard will start again from zero.',
    );
    if (!ok) return;

    try {
        const result = await api('/api/mood/reset', { method: 'POST' });
        toast(`Deleted ${result.deleted} check-in${result.deleted === 1 ? '' : 's'}`);
        await load();
    } catch {
        toast('Could not clear the history', 'bad');
    }
}

/* --------------------------------------------------------------------------
   Log
   -------------------------------------------------------------------------- */

function buildFilters() {
    const row = $('#filterRow');
    row.innerHTML = [
        '<button class="chip" type="button" data-filter="all" aria-pressed="true">All</button>',
        ...MOODS.map((mood) =>
            `<button class="chip" type="button" data-filter="${mood}" data-mood-colour="${mood}" aria-pressed="false">${mood}</button>`),
    ].join('');

    row.addEventListener('click', (event) => {
        const chip = event.target.closest('.chip');
        if (!chip) return;
        filter = chip.dataset.filter;
        $$('#filterRow .chip').forEach((c) => c.setAttribute('aria-pressed', String(c.dataset.filter === filter)));
        paintLog();
    });
}

async function load() {
    const [rows, summary] = await Promise.all([
        api('/api/mood?limit=300').catch(() => []),
        api('/api/stats/summary?days=90').catch(() => null),
    ]);

    entries = Array.isArray(rows) ? rows : [];
    paintLog();

    $('#totalCheckins').textContent = entries.length;
    $('#totalCaption').textContent = entries.length
        ? `Most recent ${relativeDay(entries[0].createdAt).toLowerCase()}.`
        : 'Your first one is the hardest.';

    if (summary) {
        renderSpread($('#spread'), summary.distribution, MOOD_COLOURS);
        if (summary.latest && summary.latest.hasMood) applyMoodTint(summary.latest.mood);
    }
}

function paintLog() {
    const log = $('#moodLog');
    const visible = filter === 'all' ? entries : entries.filter((row) => row.mood === filter);

    $('#logCount').textContent = entries.length
        ? `${visible.length} of ${entries.length}`
        : '';

    if (!visible.length) {
        log.innerHTML = `
            <div class="empty">
                <span class="empty__mark">${icon('leaf')}</span>
                <p class="empty__title">${entries.length ? 'Nothing matches that filter' : 'No check-ins yet'}</p>
                <p>${entries.length
                    ? 'Try another mood, or clear the filter.'
                    : 'Pick a mood above, set how strongly you feel it, and save. That is the whole ritual.'}</p>
            </div>`;
        return;
    }

    log.innerHTML = visible.map((row) => `
        <div class="log__item" data-mood-colour="${escapeHtml(row.mood)}" data-id="${row.id}">
            <span class="log__glyph">${moodGlyph(row.mood)}</span>
            <div>
                <div class="log__head">
                    <span class="log__mood">${escapeHtml(row.mood)}</span>
                    <span class="log__level">${Array.from({ length: 10 }, (_, i) =>
                        `<i class="log__pip" data-on="${i < row.level}"></i>`).join('')}</span>
                    <span class="log__meta">${escapeHtml(relativeDay(row.createdAt))}</span>
                </div>
                ${row.note ? `<p class="log__note">${escapeHtml(row.note)}</p>` : ''}
                ${row.tags ? `<div class="row" style="margin-top:8px">${row.tags.split(',')
                    .filter(Boolean)
                    .map((tag) => `<span class="chip chip--static">${escapeHtml(tag)}</span>`).join('')}</div>` : ''}
            </div>
            <button class="btn btn--quiet btn--sm" type="button" data-delete="${row.id}" aria-label="Delete this check-in">${icon('trash')}</button>
        </div>`).join('');

    log.querySelectorAll('[data-delete]').forEach((button) => {
        button.addEventListener('click', () => remove(Number(button.dataset.delete)));
    });
}

async function remove(id) {
    try {
        await api(`/api/mood/${id}`, { method: 'DELETE' });
        entries = entries.filter((row) => row.id !== id);
        paintLog();
        toast('Check-in deleted');
    } catch {
        toast('Could not delete that entry', 'bad');
    }
}
