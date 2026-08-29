/* CEREVIA — Settings */

import {
    api, bootPage, currentThemePreference, escapeHtml, session, setTheme, setTinting,
    storage, tintEnabled, toast, $, $$, KEY,
} from './core.js';

const started = await bootPage({ active: 'settings.html' });
if (started) {
    wireTheme();
    $('#saveProfile').addEventListener('click', saveProfile);
    $('#savePin').addEventListener('click', savePin);
    $('#saveQuestion').addEventListener('click', saveQuestion);
    $('#exportData').addEventListener('click', exportData);
    $('#wipeAll').addEventListener('click', wipeAll);

    await Promise.all([loadProfile(), loadStats(), loadAbout()]);
}

/* --------------------------------------------------------------------------
   Profile
   -------------------------------------------------------------------------- */

async function loadProfile() {
    try {
        const profile = await api('/api/profile');
        $('#displayName').value = profile.displayName === 'friend' ? '' : profile.displayName;
        $('#emergencyContact').value = profile.emergencyContact || '';
        $('#securityQuestion').value = profile.securityQuestion || '';
    } catch {
        toast('Could not load your profile', 'bad');
    }
}

async function saveProfile() {
    try {
        await api('/api/profile', {
            method: 'POST',
            body: {
                displayName: $('#displayName').value.trim() || 'friend',
                emergencyContact: $('#emergencyContact').value.trim() || '112',
            },
        });
        toast('Saved', 'good');
    } catch {
        toast('Could not save those settings', 'bad');
    }
}

async function savePin() {
    const pin = $('#newPin').value.trim();
    if (pin.length < 4 || pin.length > 12) {
        toast('Choose a PIN between 4 and 12 characters', 'bad');
        return;
    }
    try {
        await api('/api/auth/pin', { method: 'POST', body: { pin } });
        $('#newPin').value = '';
        toast('PIN updated', 'good');
    } catch (error) {
        toast(error.message || 'Could not update the PIN', 'bad');
    }
}

async function saveQuestion() {
    const question = $('#securityQuestion').value.trim();
    const answer = $('#securityAnswer').value.trim();

    if (!question) { toast('The recovery question cannot be empty', 'bad'); return; }
    if (!answer) {
        toast('Enter the answer as well, so it can be stored securely', 'bad');
        $('#securityAnswer').focus();
        return;
    }

    try {
        await api('/api/profile', { method: 'POST', body: { securityQuestion: question, securityAnswer: answer } });
        $('#securityAnswer').value = '';
        toast('Recovery question updated', 'good');
    } catch {
        toast('Could not update the recovery question', 'bad');
    }
}

/* --------------------------------------------------------------------------
   Appearance
   -------------------------------------------------------------------------- */

function wireTheme() {
    const paint = () => {
        const preference = currentThemePreference();
        $$('#themeSwitch button').forEach((button) => {
            button.setAttribute('aria-pressed', String(button.dataset.themeChoice === preference));
        });
    };

    $('#themeSwitch').addEventListener('click', (event) => {
        const button = event.target.closest('button');
        if (!button) return;
        setTheme(button.dataset.themeChoice);
        paint();
    });

    paint();

    const paintTint = () => {
        const on = tintEnabled();
        $$('#tintSwitch button').forEach((button) => {
            button.setAttribute('aria-pressed', String((button.dataset.tintChoice === 'on') === on));
        });
    };

    $('#tintSwitch').addEventListener('click', (event) => {
        const button = event.target.closest('button');
        if (!button) return;
        setTinting(button.dataset.tintChoice === 'on');
        paintTint();
        const mood = storage.get(KEY.mood);
        toast(button.dataset.tintChoice === 'on'
            ? (mood ? `Tinting on — following ${mood}` : 'Tinting on — waiting for a check-in')
            : 'Tinting off');
    });

    paintTint();
}

/* --------------------------------------------------------------------------
   Data
   -------------------------------------------------------------------------- */

async function loadStats() {
    const rows = $('#dataStats');
    try {
        const [summary, journal, breathing] = await Promise.all([
            api('/api/stats/summary?days=365'),
            api('/api/journal/stats'),
            api('/api/breathing'),
        ]);

        rows.innerHTML = pairs([
            ['Mood check-ins', summary.moodCount],
            ['Journal entries', journal.count],
            ['Words written', (journal.words || 0).toLocaleString()],
            ['Breathing sessions', breathing.sessions],
            ['Breathing cycles', breathing.cycles],
            ['Current streak', `${summary.streakDays} day${summary.streakDays === 1 ? '' : 's'}`],
        ]);
    } catch {
        rows.innerHTML = '<dt>Status</dt><dd>Server offline</dd>';
    }
}

async function loadAbout() {
    let version = '2.0.0';
    try {
        const meta = await api('/api/meta');
        version = meta.version || version;
    } catch { /* offline */ }

    $('#aboutRows').innerHTML = pairs([
        ['Application', 'CEREVIA'],
        ['Version', version],
        ['Storage', 'Local SQLite file on this machine'],
        ['Journal entries', 'Encrypted at rest with a key kept beside the database'],
        ['Network', 'Loopback only — the server does not accept remote connections'],
        ['Companion', 'Runs offline; no message is ever sent to a third party'],
        ['Built by', 'Harsh, Abhay, Akanksha'],
    ]);
}

function pairs(entries) {
    return entries.map(([key, value]) =>
        `<dt>${escapeHtml(key)}</dt><dd>${escapeHtml(String(value))}</dd>`).join('');
}

async function exportData() {
    try {
        const [moods, journal, breathing, profile] = await Promise.all([
            api('/api/mood?limit=1000'),
            api('/api/journal?limit=1000'),
            api('/api/breathing'),
            api('/api/profile'),
        ]);

        const payload = {
            exportedAt: new Date().toISOString(),
            application: 'CEREVIA',
            profile: { displayName: profile.displayName, emergencyContact: profile.emergencyContact },
            moods,
            journal,
            breathing,
        };

        const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = `cerevia-export-${new Date().toISOString().slice(0, 10)}.json`;
        document.body.append(link);
        link.click();
        link.remove();
        URL.revokeObjectURL(url);

        toast('Export downloaded', 'good');
    } catch {
        toast('Could not build the export', 'bad');
    }
}

async function wipeAll() {
    const typed = window.prompt(
        'This deletes every mood check-in, journal entry and breathing session.\n\n'
        + 'Type DELETE to confirm.',
    );
    if (typed !== 'DELETE') {
        if (typed !== null) toast('Nothing was deleted');
        return;
    }

    try {
        await Promise.all([
            api('/api/mood/reset', { method: 'POST' }),
            api('/api/journal/reset', { method: 'POST' }),
            api('/api/breathing/reset', { method: 'POST' }),
        ]);
        storage.remove(KEY.mood);
        storage.remove('cerevia.chatHistory');
        storage.remove('cerevia.journalDraft');
        document.documentElement.removeAttribute('data-mood-colour');
        toast('Everything deleted');
        await loadStats();
    } catch {
        toast('Could not delete everything', 'bad');
    }
}
