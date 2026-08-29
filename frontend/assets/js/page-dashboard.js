/* CEREVIA — Today */

import {
    api, applyMoodTint, bootPage, el, escapeHtml, greetingForHour, icon,
    moodGlyph, relativeDay, toast, $, MOODS,
} from './core.js';
import { renderSky, renderSpread } from './sky.js';

const MOOD_COLOURS = Object.fromEntries(
    MOODS.map((mood) => [mood, `var(--mood-${mood.toLowerCase()})`]),
);

const started = await bootPage({ active: 'dashboard.html' });
if (started) {
    await refresh();
    $('#refreshBtn').addEventListener('click', () => refresh(true));
    $('#anotherSuggestion').addEventListener('click', loadSuggestion);

    // A dashboard left open overnight should not still be showing yesterday.
    document.addEventListener('visibilitychange', () => {
        if (!document.hidden) refresh();
    });
}

async function refresh(announce = false) {
    $('#greeting').textContent = greetingForHour();

    const results = await Promise.allSettled([
        api('/api/stats/summary?days=14'),
        api('/api/crisis'),
        api('/api/mood?limit=5'),
        api('/api/journal/stats'),
    ]);

    const [summary, crisis, recent, journal] = results.map((r) => (r.status === 'fulfilled' ? r.value : null));

    if (!summary) {
        $('#headLede').textContent = 'The CEREVIA server is not responding. Start it with ./cerevia and refresh.';
        return;
    }

    paintSummary(summary);
    paintCrisis(crisis);
    paintRecent(recent || []);
    paintJournal(journal);
    await loadSuggestion();
    paintQuickActions(summary);

    if (announce) toast('Refreshed', 'good');
}

function paintSummary(summary) {
    const latest = summary.latest || {};
    if (latest.hasMood && latest.mood) applyMoodTint(latest.mood);

    // Gauge — a 235-unit arc of a 314 circumference circle (r = 50).
    const arcLength = 235;
    const score = Number(summary.score) || 0;
    $('#gaugeNumber').textContent = summary.scoreSampleSize ? score : '—';
    $('#gaugeArc').style.strokeDashoffset = String(arcLength - (arcLength * score) / 100);

    if (!summary.scoreSampleSize) {
        $('#gaugeCaption').textContent = 'No check-ins yet — the score appears after your first one.';
        $('#gaugeSample').textContent = '';
    } else {
        $('#gaugeCaption').textContent = describeScore(score);
        $('#gaugeSample').textContent = summary.scoreFromAllTime
            ? `From all ${summary.scoreSampleSize} check-ins — none in the last 14 days.`
            : `Across ${summary.scoreSampleSize} check-in${summary.scoreSampleSize === 1 ? '' : 's'}.`;
    }

    // Streak
    const streak = Number(summary.streakDays) || 0;
    $('#streakValue').textContent = streak;
    $('#streakCaption').textContent = streak === 0
        ? 'Check in today to start a streak.'
        : streak === 1
            ? 'One day in. Tomorrow makes it a streak.'
            : `${streak} days running. That consistency is the point, not the score.`;

    // Head lede
    if (latest.hasMood) {
        $('#headLede').innerHTML =
            `Last check-in ${escapeHtml(relativeDay(latest.createdAt).toLowerCase())} — `
            + `<strong>${escapeHtml(latest.mood)}</strong>, intensity ${latest.level}/10.`;
    } else {
        $('#headLede').textContent = 'Nothing logged yet. One check-in is enough to start seeing a shape.';
    }

    renderSky($('#sky'), summary.trend);
    renderSpread($('#spread'), summary.distribution, MOOD_COLOURS);

    if (summary.trend && summary.trend.length) {
        const first = summary.trend[0];
        const last = summary.trend[summary.trend.length - 1];
        $('#skyRange').textContent = `${first.label} → ${last.label}`;
    }

    $('#spreadNote').textContent = summary.frequentMood
        ? `Most often: ${summary.frequentMood.toLowerCase()}.`
        : '';
}

function describeScore(score) {
    if (score >= 75) return 'A good stretch. Worth noticing what has been working.';
    if (score >= 58) return 'Broadly steady, with some lift.';
    if (score >= 45) return 'Fairly even — neither side is winning.';
    if (score >= 30) return 'A heavier stretch than usual. Be gentle with the expectations.';
    return 'A genuinely hard fortnight. This is the point to lean on someone.';
}

function paintCrisis(crisis) {
    const banner = $('#careBanner');
    if (!crisis || !crisis.crisis) {
        banner.classList.add('hide');
        return;
    }

    banner.classList.remove('hide');
    banner.innerHTML = `
        <span class="care__mark">${icon('heart')}</span>
        <div class="care__body">
            <p class="care__title">${crisis.severe ? 'Three hard days in a row' : 'A pattern worth naming'}</p>
            <p class="care__text">${escapeHtml(crisis.message)}</p>
            <div class="care__actions">
                <a class="btn care__btn" href="tel:${escapeHtml(crisis.contact)}">${icon('phone')} Call ${escapeHtml(crisis.contact)}</a>
                <a class="btn btn--ghost" href="breathe.html">Breathe first</a>
                <button class="btn btn--quiet" type="button" id="talkItThrough">Talk it through</button>
            </div>
        </div>`;

    banner.querySelector('#talkItThrough')?.addEventListener('click', () => {
        document.getElementById('companionOpen')?.click();
    });
}

function paintRecent(rows) {
    const log = $('#recentLog');
    if (!rows.length) {
        log.innerHTML = `
            <div class="empty">
                <span class="empty__mark">${icon('mood')}</span>
                <p class="empty__title">No check-ins yet</p>
                <p>Logging one takes about fifteen seconds, and it is what everything else on this page is built from.</p>
            </div>`;
        return;
    }

    log.innerHTML = rows.map((row) => `
        <div class="log__item" data-mood-colour="${escapeHtml(row.mood)}">
            <span class="log__glyph">${moodGlyph(row.mood)}</span>
            <div>
                <div class="log__head">
                    <span class="log__mood">${escapeHtml(row.mood)}</span>
                    <span class="log__level">${pips(row.level)}</span>
                    <span class="log__meta">${escapeHtml(relativeDay(row.createdAt))}</span>
                </div>
                ${row.note ? `<p class="log__note">${escapeHtml(row.note)}</p>` : ''}
            </div>
            <span class="log__meta tabular">${row.score}</span>
        </div>`).join('');
}

function pips(level) {
    return Array.from({ length: 10 }, (_, index) =>
        `<i class="log__pip" data-on="${index < level}"></i>`).join('');
}

function paintJournal(stats) {
    if (!stats) return;
    $('#journalValue').textContent = stats.count || 0;
    $('#journalCaption').textContent = stats.count
        ? `${stats.words.toLocaleString()} words across ${stats.daysWritten} day${stats.daysWritten === 1 ? '' : 's'}.`
        : 'Nothing written yet.';
}

async function loadSuggestion() {
    try {
        const suggestion = await api('/api/suggestion');
        $('#suggestionText').textContent = suggestion.message;
        $('#suggestionMood').textContent = suggestion.urgent
            ? 'Please read this one'
            : suggestion.hasMood
                ? `Because you felt ${String(suggestion.mood).toLowerCase()}`
                : 'A place to start';

        const card = $('#suggestionMood').closest('.card');
        card.dataset.moodColour = suggestion.mood || '';
    } catch {
        $('#suggestionText').textContent = 'Could not load a suggestion right now.';
    }
}

function paintQuickActions(summary) {
    const latest = summary.latest || {};
    const actions = [];

    if (!latest.hasMood) {
        actions.push(['mood.html', 'mood', 'Log your first check-in', 'Everything else grows from this.']);
    } else if (isToday(latest.createdAt)) {
        actions.push(['journal.html', 'journal', 'Write today out', 'You have checked in — say more if you want to.']);
    } else {
        actions.push(['mood.html', 'mood', 'Check in for today', `Last one was ${relativeDay(latest.createdAt).toLowerCase()}.`]);
    }

    const low = ['Sad', 'Anxious', 'Angry'].includes(latest.mood);
    if (low) {
        actions.push(['breathe.html', 'breathe', 'Two minutes of breathing', 'Shortest route back to a steadier body.']);
    } else {
        actions.push(['eq.html', 'toolkit', 'Open the toolkit', 'Reading and activities matched to your last mood.']);
    }

    actions.push(['breathe.html', 'spark', '5-4-3-2-1 grounding', 'For when thinking is not helping.']);

    $('#quickActions').innerHTML = actions.map(([href, name, title, note]) => `
        <a class="entry" href="${href}" style="display:block;text-decoration:none;margin:0">
            <div class="row" style="gap:14px;flex-wrap:nowrap">
                <span class="icon-fill" style="width:22px;height:22px;color:var(--mood);flex:none">${icon(name)}</span>
                <span style="flex:1">
                    <span class="log__mood" style="display:block">${title}</span>
                    <span class="log__meta">${note}</span>
                </span>
                <span class="icon-fill" style="width:16px;height:16px;color:var(--ink-faint);flex:none">${icon('arrow')}</span>
            </div>
        </a>`).join('');
}

function isToday(iso) {
    if (!iso) return false;
    const parsed = new Date(iso.replace(' ', 'T'));
    const now = new Date();
    return parsed.getFullYear() === now.getFullYear()
        && parsed.getMonth() === now.getMonth()
        && parsed.getDate() === now.getDate();
}
