/* CEREVIA — Journal */

import {
    api, bootPage, debounce, escapeHtml, icon, relativeDay, storage, toast, $,
} from './core.js';

const DRAFT_KEY = 'cerevia.journalDraft';

let entries = [];
let currentPrompt = '';

const started = await bootPage({ active: 'journal.html' });
if (started) {
    $('#privacyChip').innerHTML = `${icon('lock')} Encrypted before it is written to disk`;
    $('#todayLabel').textContent = new Date().toLocaleDateString([], {
        weekday: 'long', day: 'numeric', month: 'long',
    });

    restoreDraft();
    wireComposer();

    $('#newPrompt').addEventListener('click', loadPrompt);
    $('#usePrompt').addEventListener('click', usePrompt);
    $('#saveEntry').addEventListener('click', save);
    $('#clearJournal').addEventListener('click', clearAll);
    $('#search').addEventListener('input', debounce((event) => load(event.target.value), 260));

    await Promise.all([loadPrompt(), load()]);
}

/* --------------------------------------------------------------------------
   Composer
   -------------------------------------------------------------------------- */

function wireComposer() {
    const field = $('#entry');

    const update = () => {
        const text = field.value;
        const words = text.trim() ? text.trim().split(/\s+/).length : 0;
        $('#counter').textContent = `${words} word${words === 1 ? '' : 's'}`;

        // An unsaved draft is kept locally so closing the tab mid-thought does
        // not lose it. It is cleared the moment the entry is saved.
        if (text.trim()) {
            storage.set(DRAFT_KEY, text);
            $('#draftNote').textContent = 'Draft kept on this device until you save.';
        } else {
            storage.remove(DRAFT_KEY);
            $('#draftNote').textContent = '';
        }
    };

    field.addEventListener('input', update);
    update();

    // Cmd/Ctrl+Enter saves, which is what anyone who writes a lot expects.
    field.addEventListener('keydown', (event) => {
        if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
            event.preventDefault();
            save();
        }
    });
}

function restoreDraft() {
    const draft = storage.get(DRAFT_KEY, '');
    if (draft) $('#entry').value = draft;
}

async function loadPrompt() {
    try {
        const { prompt } = await api('/api/journal/prompt');
        currentPrompt = prompt;
        $('#promptText').textContent = prompt;
    } catch {
        currentPrompt = '';
        $('#promptText').textContent = 'What is taking up the most space in your head right now?';
    }
}

function usePrompt() {
    const field = $('#entry');
    const prefix = field.value.trim() ? `${field.value.trim()}\n\n` : '';
    field.value = `${prefix}${$('#promptText').textContent}\n\n`;
    field.focus();
    field.setSelectionRange(field.value.length, field.value.length);
    field.dispatchEvent(new Event('input'));
}

async function save() {
    const field = $('#entry');
    const text = field.value.trim();

    if (!text) {
        toast('Write something first', 'bad');
        field.focus();
        return;
    }

    const button = $('#saveEntry');
    button.disabled = true;
    button.textContent = 'Saving…';

    try {
        await api('/api/journal', {
            method: 'POST',
            body: { text, prompt: currentPrompt },
        });

        field.value = '';
        storage.remove(DRAFT_KEY);
        field.dispatchEvent(new Event('input'));
        toast('Entry saved', 'good');
        await Promise.all([load($('#search').value), loadPrompt()]);
    } catch (error) {
        toast(error.message || 'Could not save that entry', 'bad');
    } finally {
        button.disabled = false;
        button.textContent = 'Save entry';
    }
}

/* --------------------------------------------------------------------------
   History
   -------------------------------------------------------------------------- */

async function load(search = '') {
    const query = search.trim() ? `?q=${encodeURIComponent(search.trim())}` : '';

    const [rows, stats] = await Promise.all([
        api(`/api/journal${query}`).catch(() => []),
        api('/api/journal/stats').catch(() => null),
    ]);

    entries = Array.isArray(rows) ? rows : [];
    paint(search.trim());

    if (stats) {
        $('#entryCount').textContent = stats.count;
        $('#wordCount').textContent = `${(stats.words || 0).toLocaleString()} words in total`;
        $('#lastWritten').textContent = stats.count
            ? `Last entry ${stats.lastEntry}.`
            : 'Nothing yet. The first entry is usually the shortest.';
    }
}

function paint(search) {
    const list = $('#entryList');
    $('#resultCount').textContent = search
        ? `${entries.length} match${entries.length === 1 ? '' : 'es'}`
        : `${entries.length} entr${entries.length === 1 ? 'y' : 'ies'}`;

    if (!entries.length) {
        list.innerHTML = `
            <div class="empty">
                <span class="empty__mark">${icon('quote')}</span>
                <p class="empty__title">${search ? 'Nothing matches that' : 'No entries yet'}</p>
                <p>${search
                    ? 'Search looks inside the text of every entry. Try a shorter word.'
                    : 'Write anything above and save it. Entries are encrypted before they reach the database.'}</p>
            </div>`;
        return;
    }

    list.innerHTML = entries.map((entry) => `
        <article class="entry" data-id="${entry.id}">
            <div class="entry__head">
                <span class="entry__date">${escapeHtml(relativeDay(entry.createdAt) || entry.date)}</span>
                <button class="btn btn--quiet btn--sm" type="button" data-delete="${entry.id}" aria-label="Delete this entry">${icon('trash')}</button>
            </div>
            ${entry.prompt ? `<p class="entry__prompt">${escapeHtml(entry.prompt)}</p>` : ''}
            <p class="entry__body">${highlight(entry.text, search)}</p>
            <div class="entry__foot">
                <span>${entry.wordCount} words</span>
                <span>${escapeHtml(entry.date)}</span>
            </div>
        </article>`).join('');

    list.querySelectorAll('[data-delete]').forEach((button) => {
        button.addEventListener('click', () => remove(Number(button.dataset.delete)));
    });
}

/** Escapes first, then marks the search term — never the other way round. */
function highlight(text, search) {
    const safe = escapeHtml(text);
    if (!search) return safe;
    const pattern = new RegExp(`(${search.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi');
    return safe.replace(pattern, '<mark style="background:var(--mood-wash);color:inherit">$1</mark>');
}

async function remove(id) {
    if (!window.confirm('Delete this entry? It cannot be recovered.')) return;
    try {
        await api(`/api/journal/${id}`, { method: 'DELETE' });
        toast('Entry deleted');
        await load($('#search').value);
    } catch {
        toast('Could not delete that entry', 'bad');
    }
}

async function clearAll() {
    const ok = window.confirm(
        'Delete every journal entry?\n\nThis erases everything you have written and cannot be undone.',
    );
    if (!ok) return;

    try {
        const result = await api('/api/journal/reset', { method: 'POST' });
        toast(`Deleted ${result.deleted} entr${result.deleted === 1 ? 'y' : 'ies'}`);
        await load();
    } catch {
        toast('Could not clear the journal', 'bad');
    }
}
