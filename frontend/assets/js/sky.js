/* ==========================================================================
   CEREVIA — the sky chart
   --------------------------------------------------------------------------
   Fourteen days of wellbeing drawn as a horizon line. Above the dotted line is
   a better-than-even day; below it is not. Days without a check-in are drawn
   as gaps rather than zeroes, so a break in the habit never looks like a
   collapse in mood.
   ========================================================================== */

const W = 760;
const H = 230;
const PAD = { top: 18, right: 16, bottom: 34, left: 16 };

const scoreToY = (score) => {
    const usable = H - PAD.top - PAD.bottom;
    return PAD.top + usable * (1 - Math.max(0, Math.min(100, score)) / 100);
};

/** Catmull-Rom through the points, converted to cubic beziers. */
function smoothPath(points) {
    if (points.length === 0) return '';
    if (points.length === 1) return `M ${points[0].x} ${points[0].y}`;

    let path = `M ${points[0].x} ${points[0].y}`;
    for (let i = 0; i < points.length - 1; i += 1) {
        const p0 = points[i - 1] || points[i];
        const p1 = points[i];
        const p2 = points[i + 1];
        const p3 = points[i + 2] || p2;

        const c1x = p1.x + (p2.x - p0.x) / 6;
        const c1y = p1.y + (p2.y - p0.y) / 6;
        const c2x = p2.x - (p3.x - p1.x) / 6;
        const c2y = p2.y - (p3.y - p1.y) / 6;

        path += ` C ${c1x.toFixed(1)} ${c1y.toFixed(1)}, ${c2x.toFixed(1)} ${c2y.toFixed(1)}, ${p2.x.toFixed(1)} ${p2.y.toFixed(1)}`;
    }
    return path;
}

/** Splits the trend into runs of consecutive days that actually have data. */
function segmentsOf(points) {
    const runs = [];
    let current = [];
    points.forEach((point) => {
        if (point.hasData) {
            current.push(point);
        } else if (current.length) {
            runs.push(current);
            current = [];
        }
    });
    if (current.length) runs.push(current);
    return runs;
}

export function renderSky(container, trend) {
    if (!container) return;

    if (!trend || !trend.length) {
        container.innerHTML = '';
        return;
    }

    const step = (W - PAD.left - PAD.right) / Math.max(1, trend.length - 1);
    const points = trend.map((day, index) => ({
        ...day,
        x: PAD.left + index * step,
        y: scoreToY(day.hasData ? day.score : 50),
    }));

    const horizonY = scoreToY(50);
    const runs = segmentsOf(points);

    const areas = runs
        .filter((run) => run.length > 1)
        .map((run) => {
            const line = smoothPath(run);
            const first = run[0];
            const last = run[run.length - 1];
            return `<path class="sky__area" d="${line} L ${last.x.toFixed(1)} ${horizonY.toFixed(1)} L ${first.x.toFixed(1)} ${horizonY.toFixed(1)} Z"/>`;
        })
        .join('');

    const lines = runs
        .filter((run) => run.length > 1)
        .map((run) => `<path class="sky__line" d="${smoothPath(run)}"/>`)
        .join('');

    // Dashed connectors bridge a gap so the eye can follow the sequence,
    // without implying data exists in between.
    const bridges = runs
        .slice(0, -1)
        .map((run, index) => {
            const from = run[run.length - 1];
            const to = runs[index + 1][0];
            return `<path class="sky__gap" stroke-dasharray="3 5" d="M ${from.x.toFixed(1)} ${from.y.toFixed(1)} L ${to.x.toFixed(1)} ${to.y.toFixed(1)}"/>`;
        })
        .join('');

    const nodes = points
        .map((point) => {
            if (!point.hasData) {
                return `<circle cx="${point.x.toFixed(1)}" cy="${horizonY.toFixed(1)}" r="2" fill="var(--line-strong)"/>`;
            }
            const label = `${point.label} — ${point.score}/100${point.mood ? `, mostly ${point.mood.toLowerCase()}` : ''}`;
            return `<circle class="sky__node" cx="${point.x.toFixed(1)}" cy="${point.y.toFixed(1)}" r="4.5">
                        <title>${label}</title>
                    </circle>`;
        })
        .join('');

    // Only every other weekday label, so a fortnight does not crowd.
    const labels = points
        .map((point, index) => (index % 2 === 0 || index === points.length - 1
            ? `<text class="sky__label" x="${point.x.toFixed(1)}" y="${H - 12}">${point.label}</text>`
            : ''))
        .join('');

    container.innerHTML = `
        <svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" role="img"
             aria-label="Wellbeing over the last ${trend.length} days">
            <defs>
                <linearGradient id="skyGradient" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%"   stop-color="var(--mood)" stop-opacity="0.42"/>
                    <stop offset="100%" stop-color="var(--mood)" stop-opacity="0"/>
                </linearGradient>
            </defs>
            <line class="sky__horizon" x1="${PAD.left}" y1="${horizonY.toFixed(1)}" x2="${W - PAD.right}" y2="${horizonY.toFixed(1)}"/>
            <text class="sky__caption" x="${PAD.left}" y="${(horizonY - 6).toFixed(1)}">even</text>
            ${areas}${bridges}${lines}${nodes}${labels}
        </svg>`;
}

export function renderSpread(container, distribution, colours) {
    if (!container) return;

    const entries = Object.entries(distribution || {});
    const total = entries.reduce((sum, [, count]) => sum + count, 0);

    if (!total) {
        container.innerHTML = '<p class="field__hint">No check-ins in this window yet.</p>';
        return;
    }

    container.innerHTML = entries
        .sort((a, b) => b[1] - a[1])
        .map(([mood, count]) => {
            const percent = Math.round((count / total) * 100);
            return `<div class="spread__row">
                        <span class="spread__name">${mood}</span>
                        <span class="spread__track"><span class="spread__fill" style="width:${percent}%;background:${colours[mood]}"></span></span>
                        <span class="spread__count tabular">${count}</span>
                    </div>`;
        })
        .join('');
}
