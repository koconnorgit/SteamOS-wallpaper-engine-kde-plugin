// Wallpaper schedule: data model helpers and a stateless resolver.
//
// A schedule is a list of slots. Each slot is a change point: it fires at a
// moment (a clock time, or sunrise/sunset plus an offset) and stays active
// until another slot fires. A slot targets one wallpaper or a playlist; a
// playlist rotates on its own interval, counted from the moment the slot fired.
//
// Everything here is a pure function of (config, now, sun times), so the
// right wallpaper can be recomputed after a restart or suspend without any
// stored position. Runs both in QML and in node (see tests/schedule.test.mjs).

export const VERSION = 1;
export const DAY_MS = 24 * 60 * 60 * 1000;
const MIN_MS = 60 * 1000;

export function emptyConf() {
    return { version: VERSION, enabled: false, playlists: [], slots: [] };
}

export function parse(str) {
    const conf = emptyConf();
    if (!str) return conf;
    let data = null;
    try {
        data = JSON.parse(str);
    } catch (e) {
        return conf;
    }
    if (!data || typeof data !== 'object') return conf;
    conf.enabled = Boolean(data.enabled);
    if (Array.isArray(data.playlists)) {
        conf.playlists = data.playlists.filter(p => p && p.id).map(p => ({
            id: String(p.id),
            name: String(p.name || 'Playlist'),
            order: p.order === 'shuffle' ? 'shuffle' : 'sequential',
            interval: Math.max(1, Math.round(Number(p.interval) || 30)),
            items: Array.isArray(p.items) ? p.items.filter(isItem).map(cleanItem) : []
        }));
    }
    if (Array.isArray(data.slots)) {
        conf.slots = data.slots.filter(s => s && s.id).map(s => ({
            id: String(s.id),
            trigger: cleanTrigger(s.trigger),
            target: cleanTarget(s.target)
        }));
    }
    return conf;
}

export function serialize(conf) {
    return JSON.stringify(conf);
}

export function clone(conf) {
    return JSON.parse(JSON.stringify(conf));
}

export function newId(prefix) {
    return `${prefix}${Date.now().toString(36)}${Math.floor(Math.random() * 1e6).toString(36)}`;
}

function isItem(it) {
    return Boolean(it && it.wid && it.source);
}

function cleanItem(it) {
    return {
        wid: String(it.wid),
        source: String(it.source),
        title: String(it.title || it.wid),
        preview: String(it.preview || '')
    };
}

function cleanTrigger(t) {
    if (t && t.type === 'sun') {
        return {
            type: 'sun',
            event: t.event === 'sunrise' ? 'sunrise' : 'sunset',
            offset: Math.round(Number(t.offset) || 0)
        };
    }
    const minutes = Math.round(Number(t && t.minutes) || 0);
    return { type: 'clock', minutes: ((minutes % 1440) + 1440) % 1440 };
}

function cleanTarget(t) {
    if (t && t.type === 'playlist' && t.id) return { type: 'playlist', id: String(t.id) };
    if (t && t.type === 'wallpaper' && isItem(t)) return Object.assign({ type: 'wallpaper' }, cleanItem(t));
    return { type: 'none' };
}

// Builds the two-slot schedule that replaces the old Day/Night switch.
export function fromDayNight(day, night) {
    const conf = emptyConf();
    const add = (event, wp) => {
        if (!isItem(wp)) return;
        conf.slots.push({
            id: newId('s'),
            trigger: { type: 'sun', event: event, offset: 0 },
            target: Object.assign({ type: 'wallpaper' }, cleanItem(wp))
        });
    };
    add('sunrise', day);
    add('sunset', night);
    conf.enabled = conf.slots.length > 0;
    return conf;
}

export function usesSun(conf) {
    return conf.slots.some(s => s.trigger.type === 'sun');
}

export function findPlaylist(conf, id) {
    return conf.playlists.find(p => p.id === id) || null;
}

// ---- firing times ----

function clockAt(dayRef, minutes, dayOffset) {
    const d = new Date(dayRef);
    return new Date(d.getFullYear(), d.getMonth(), d.getDate() + dayOffset,
                    Math.floor(minutes / 60), minutes % 60, 0, 0).getTime();
}

// Known sun events padded by a day's step on each side, so that a slot such
// as "sunset + 1h" still has a previous firing when only today's is known.
function sunCandidates(events) {
    const list = (events || []).map(Number).filter(v => isFinite(v) && v > 0).sort((a, b) => a - b);
    if (list.length === 0) return [];
    const out = list.slice();
    for (let k = 1; k <= 2; k++) {
        out.unshift(list[0] - k * DAY_MS);
        out.push(list[list.length - 1] + k * DAY_MS);
    }
    return out;
}

// Returns { last, next } in epoch ms; either may be null when unknown.
export function firingTimes(trigger, now, sun) {
    let candidates;
    if (trigger.type === 'sun') {
        const events = sun ? (trigger.event === 'sunrise' ? sun.sunrises : sun.sunsets) : null;
        candidates = sunCandidates(events).map(t => t + trigger.offset * MIN_MS);
    } else {
        candidates = [-1, 0, 1].map(d => clockAt(now, trigger.minutes, d));
    }
    let last = null, next = null;
    candidates.forEach(t => {
        if (t <= now) { if (last === null || t > last) last = t; }
        else if (next === null || t < next) next = t;
    });
    return { last: last, next: next };
}

// ---- playlist position ----

function hashStr(str) {
    let h = 2166136261;
    for (let i = 0; i < str.length; i++) {
        h ^= str.charCodeAt(i);
        h = Math.imul(h, 16777619);
    }
    return h >>> 0;
}

function mulberry32(seed) {
    let a = seed >>> 0;
    return function () {
        a = (a + 0x6D2B79F5) >>> 0;
        let t = a;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

function permutation(n, seedStr) {
    const rand = mulberry32(hashStr(seedStr));
    const p = Array.from({ length: n }, (_, i) => i);
    for (let i = n - 1; i > 0; i--) {
        const j = Math.floor(rand() * (i + 1));
        const tmp = p[i]; p[i] = p[j]; p[j] = tmp;
    }
    return p;
}

// Which entry of `count` items is showing `step` intervals after the slot fired.
// In shuffle order each pass through the playlist is reshuffled, and a pass
// does not start with the wallpaper the previous pass ended on.
export function playlistIndex(order, count, step, seedBase) {
    if (count <= 0) return -1;
    if (order !== 'shuffle' || count === 1) return step % count;
    // With two items the only repeat-free order is to alternate.
    if (count === 2) return (step + hashStr(seedBase)) % 2;
    const pass = Math.floor(step / count);
    const p = permutation(count, `${seedBase}/${pass}`);
    if (pass > 0) {
        // The swap below never moves a pass's last entry (count >= 3), so the
        // previous pass's unadjusted order is enough to know where it ended.
        const prev = permutation(count, `${seedBase}/${pass - 1}`);
        if (p[0] === prev[count - 1]) {
            const tmp = p[0]; p[0] = p[1]; p[1] = tmp;
        }
    }
    return p[step % count];
}

// ---- resolver ----

// resolve(conf, now, sun, isAvailable)
//   sun: { sunrises: [epoch ms], sunsets: [epoch ms] } or null
//   isAvailable(item): optional, false for wallpapers that no longer exist
// Returns null when nothing is scheduled, otherwise:
//   slot         active slot
//   firedAt      when it fired
//   item         { wid, source, ... } to show, or null if the target is empty
//   key          changes whenever the wallpaper to show changes
//   nextSlotAt   when the next slot fires (any slot)
//   nextChangeAt when to evaluate again (next slot or next playlist step)
export function resolve(conf, now, sun, isAvailable) {
    if (!conf || !conf.enabled || !conf.slots.length) return null;
    const ok = isAvailable || (() => true);

    let active = null, firedAt = null, nextSlotAt = null;
    conf.slots.forEach(slot => {
        const t = firingTimes(slot.trigger, now, sun);
        if (t.last !== null && (firedAt === null || t.last >= firedAt)) {
            active = slot;
            firedAt = t.last;
        }
        if (t.next !== null && (nextSlotAt === null || t.next < nextSlotAt)) nextSlotAt = t.next;
    });
    if (!active) return null;

    const res = {
        slot: active,
        firedAt: firedAt,
        item: null,
        playlist: null,
        index: -1,
        key: `${active.id}@${firedAt}`,
        nextSlotAt: nextSlotAt,
        nextChangeAt: nextSlotAt
    };

    const target = active.target;
    if (target.type === 'wallpaper') {
        if (ok(target)) res.item = target;
    } else if (target.type === 'playlist') {
        const pl = findPlaylist(conf, target.id);
        const items = pl ? pl.items.filter(ok) : [];
        if (items.length > 0) {
            const interval = pl.interval * MIN_MS;
            const step = Math.floor((now - firedAt) / interval);
            res.playlist = pl;
            res.index = playlistIndex(pl.order, items.length, step, `${pl.id}@${firedAt}`);
            res.item = items[res.index];
            if (items.length > 1) {
                const nextStep = firedAt + (step + 1) * interval;
                if (res.nextChangeAt === null || nextStep < res.nextChangeAt) res.nextChangeAt = nextStep;
            }
        }
    }
    if (res.item) res.key += `#${res.item.wid}#${res.item.source}`;
    return res;
}

// ---- display helpers ----

export function pad2(n) {
    return (n < 10 ? '0' : '') + n;
}

export function formatDuration(minutes) {
    const m = Math.abs(minutes);
    const h = Math.floor(m / 60), r = m % 60;
    if (h && r) return `${h} h ${r} min`;
    if (h) return `${h} h`;
    return `${r} min`;
}

export function describeTrigger(trigger) {
    if (trigger.type === 'sun') {
        const name = trigger.event === 'sunrise' ? 'Sunrise' : 'Sunset';
        if (!trigger.offset) return name;
        return `${formatDuration(trigger.offset)} ${trigger.offset < 0 ? 'before' : 'after'} ${name.toLowerCase()}`;
    }
    return `${pad2(Math.floor(trigger.minutes / 60))}:${pad2(trigger.minutes % 60)}`;
}

// Time of day (minutes since midnight) at which a slot fires today, used for
// sorting and for showing "≈ 17:42" next to sun slots. null when unknown.
export function todayMinutes(trigger, now, sun) {
    if (trigger.type === 'clock') return trigger.minutes;
    const start = clockAt(now, 0, 0);
    const events = sun ? (trigger.event === 'sunrise' ? sun.sunrises : sun.sunsets) : null;
    const cands = sunCandidates(events).map(t => t + trigger.offset * MIN_MS);
    if (!cands.length) return null;
    // Prefer the firing that falls on today's date; otherwise the nearest one.
    let best = null;
    cands.forEach(t => {
        if (t >= start && t < start + DAY_MS) best = t;
    });
    if (best === null) {
        best = cands.reduce((a, b) => Math.abs(b - now) < Math.abs(a - now) ? b : a);
    }
    const d = new Date(best);
    return d.getHours() * 60 + d.getMinutes();
}

export function sortedSlots(conf, now, sun) {
    return conf.slots
        .map((slot, index) => ({ slot: slot, index: index, minutes: todayMinutes(slot.trigger, now, sun) }))
        .sort((a, b) => {
            const am = a.minutes === null ? 1e9 : a.minutes;
            const bm = b.minutes === null ? 1e9 : b.minutes;
            return am - bm || a.index - b.index;
        });
}
