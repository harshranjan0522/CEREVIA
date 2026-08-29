/* ==========================================================================
   CEREVIA — icon set
   --------------------------------------------------------------------------
   Inline stroke SVGs rather than an emoji font. Emoji render differently on
   every platform and carry a tone this app does not want; these are drawn once
   and inherit currentColor, so they follow the mood tint.
   ========================================================================== */

const wrap = (body, size = 24) =>
    `<svg viewBox="0 0 ${size} ${size}" fill="none" stroke="currentColor" stroke-width="1.6"
          stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;

export const Icons = {
    /* Navigation */
    today:   wrap('<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M19.1 4.9l-1.4 1.4M6.3 17.7l-1.4 1.4"/>'),
    mood:    wrap('<path d="M3 15c2.5-4 5-6 9-6s6.5 2 9 6"/><circle cx="8" cy="9" r="1"/><circle cx="16" cy="9" r="1"/><path d="M3 19h18"/>'),
    journal: wrap('<path d="M5 4h11a2 2 0 0 1 2 2v14H7a2 2 0 0 1-2-2z"/><path d="M5 16h13"/><path d="M9 8h6M9 11h4"/>'),
    breathe: wrap('<circle cx="12" cy="12" r="3"/><circle cx="12" cy="12" r="7.5" opacity=".55"/><circle cx="12" cy="12" r="11" opacity=".25"/>'),
    toolkit: wrap('<path d="M12 3l2.2 4.6 5 .7-3.6 3.5.9 5-4.5-2.4L7.5 16.8l.9-5L4.8 8.3l5-.7z"/>'),
    settings: wrap('<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/>'),

    /* Actions */
    send:    wrap('<path d="M4 12l16-8-6 16-2.5-6.2z"/><path d="M11.5 13.8L20 4"/>'),
    close:   wrap('<path d="M6 6l12 12M18 6L6 18"/>'),
    check:   wrap('<path d="M4 12.5l5 5L20 6.5"/>'),
    plus:    wrap('<path d="M12 5v14M5 12h14"/>'),
    trash:   wrap('<path d="M4 7h16M9 7V5h6v2M6 7l1 13h10l1-13"/><path d="M10 11v6M14 11v6"/>'),
    arrow:   wrap('<path d="M5 12h13M13 6l6 6-6 6"/>'),
    external: wrap('<path d="M14 4h6v6"/><path d="M20 4l-9 9"/><path d="M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>'),
    lock:    wrap('<rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/>'),
    shield:  wrap('<path d="M12 3l8 3v6c0 5-3.4 8.3-8 9.5C7.4 20.3 4 17 4 12V6z"/><path d="M9 12l2.2 2.2L15.5 10"/>'),
    heart:   wrap('<path d="M12 20s-7-4.4-7-9.4A4 4 0 0 1 12 8a4 4 0 0 1 7 2.6c0 5-7 9.4-7 9.4z"/>'),
    spark:   wrap('<path d="M12 4v5M12 15v5M4 12h5M15 12h5"/><path d="M7.5 7.5l2.5 2.5M14 14l2.5 2.5M16.5 7.5L14 10M10 14l-2.5 2.5"/>'),
    flame:   wrap('<path d="M12 3s5 4.2 5 9a5 5 0 0 1-10 0c0-1.9 1-3.4 2-4.5.4 1.4 1.2 2 2 2 0-2.6 1-5 1-6.5z"/>'),
    leaf:    wrap('<path d="M5 19c0-8 5-13 14-14 0 9-4 14-11 14H5z"/><path d="M5 19c3-4 6-6 10-7.5"/>'),
    alert:   wrap('<path d="M12 4l9 16H3z"/><path d="M12 10v4M12 17.2v.1"/>'),
    phone:   wrap('<path d="M6 3h3l2 5-2.5 1.6a12 12 0 0 0 5.9 5.9L16 13l5 2v3a2 2 0 0 1-2.2 2A17 17 0 0 1 4 5.2 2 2 0 0 1 6 3z"/>'),
    search:  wrap('<circle cx="11" cy="11" r="6.5"/><path d="M16 16l4 4"/>'),
    refresh: wrap('<path d="M20 11a8 8 0 1 0-.7 4.3"/><path d="M20 5v6h-6"/>'),
    exit:    wrap('<path d="M14 4h4a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-4"/><path d="M9 12h11M13 8l4 4-4 4"/>'),
    eye:     wrap('<path d="M2 12s3.6-6.5 10-6.5S22 12 22 12s-3.6 6.5-10 6.5S2 12 2 12z"/><circle cx="12" cy="12" r="2.6"/>'),
    quote:   wrap('<path d="M9 7c-2.5 1-4 3.2-4 6v4h5v-5H7c0-2 .8-3.4 2.6-4.2z"/><path d="M18 7c-2.5 1-4 3.2-4 6v4h5v-5h-3c0-2 .8-3.4 2.6-4.2z"/>'),
    clock:   wrap('<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>'),

    /* Mood glyphs — hand-drawn feel, one per mood in the vocabulary */
    face: {
        Happy:   wrap('<circle cx="24" cy="24" r="18"/><path d="M16 20.5c.8-1.2 2.2-1.2 3 0M29 20.5c.8-1.2 2.2-1.2 3 0"/><path d="M15.5 28c2.4 3.6 5.4 5.4 8.5 5.4s6.1-1.8 8.5-5.4"/>', 48),
        Calm:    wrap('<circle cx="24" cy="24" r="18"/><path d="M15.5 21h4.5M28 21h4.5"/><path d="M17 29.5c2 1.7 4.4 2.5 7 2.5s5-.8 7-2.5"/>', 48),
        Neutral: wrap('<circle cx="24" cy="24" r="18"/><circle cx="17.6" cy="20.5" r="1.5"/><circle cx="30.4" cy="20.5" r="1.5"/><path d="M17 30h14"/>', 48),
        Anxious: wrap('<circle cx="24" cy="24" r="18"/><circle cx="17.6" cy="21.5" r="1.7"/><circle cx="30.4" cy="21.5" r="1.7"/><path d="M14.5 16.5l4.5 2M33.5 16.5L29 18.5"/><path d="M17.5 31c1.6-1.4 3-.4 4.4.5s2.9 1.6 4.4.5 2.9-1.9 4.2-1"/>', 48),
        Angry:   wrap('<circle cx="24" cy="24" r="18"/><circle cx="17.6" cy="22" r="1.5"/><circle cx="30.4" cy="22" r="1.5"/><path d="M14 17l5 2.6M34 17l-5 2.6"/><path d="M17 32c2.2-2.6 4.9-3.9 7-3.9s4.8 1.3 7 3.9"/>', 48),
        Sad:     wrap('<circle cx="24" cy="24" r="18"/><path d="M14.8 19.5c1.4-1.4 3.2-1.4 4.6 0M28.6 19.5c1.4-1.4 3.2-1.4 4.6 0"/><path d="M17 32.5c2.2-2.8 4.7-4.2 7-4.2s4.8 1.4 7 4.2"/>', 48),
    },
};

export function icon(name, className = '') {
    const markup = Icons[name] || Icons.spark;
    if (!className) return markup;
    return markup.replace('<svg ', `<svg class="${className}" `);
}

export function moodGlyph(mood, className = '') {
    const markup = Icons.face[mood] || Icons.face.Neutral;
    if (!className) return markup;
    return markup.replace('<svg ', `<svg class="${className}" `);
}
