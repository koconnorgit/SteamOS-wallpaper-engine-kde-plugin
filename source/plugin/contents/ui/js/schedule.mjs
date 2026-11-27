// Wallpaper schedule: data model helpers and a stateless resolver.
//
// A schedule is a list of slots. Each slot is a change point: it fires at a
// moment (a clock time, or sunrise/sunset plus an offset) and stays active
// until another slot fires. A slot targets one wallpaper or a playlist. A
// playlist is only a list of wallpapers: how often to change and in what
// order belong to the slot using it, counted from the moment the slot fired.
//
// Slots belong to one monitor; playlists are shared by all of them and are
// stored separately (see PlaylistStore). A conf object carries both, but
// serialize() writes only the per-monitor part and serializePlaylists() the
// shared part.
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
    // Only present in configs written before playlists became shared.
    conf.playlists = cleanPlaylists(data.playlists);
    if (Array.isArray(data.slots)) {
        conf.slots = data.slots.filter(s => s && s.id).map(s => ({
            id: String(s.id),
            trigger: cleanTrigger(s.trigger),
            target: cleanTarget(s.target)
        }));
    }
    return conf;
}

// The per-monitor part of a conf: everything but the playlists.
export function serialize(conf) {
    return JSON.stringify({ version: VERSION, enabled: Boolean(conf.enabled), slots: conf.slots });
}

function cleanPlaylists(list) {
    if (!Array.isArray(list)) return [];
    return list.filter(p => p && p.id).map(p => {
        const out = {
            id: String(p.id),
            name: String(p.name || 'Playlist'),
            items: Array.isArray(p.items) ? p.items.filter(isItem).map(cleanItem) : []
        };
        // Order and interval used to be set on the playlist. They are kept
        // when present, as the fallback for slots saved back then (rotation()).
        if (p.order !== undefined) out.order = cleanOrder(p.order);
        if (p.interval !== undefined) out.interval = cleanInterval(p.interval);
        return out;
    });
}

export const DEFAULT_ORDER = 'sequential';
export const DEFAULT_INTERVAL = 30; // minutes

function cleanOrder(order) {
    return order === 'shuffle' ? 'shuffle' : 'sequential';
}

function cleanInterval(interval) {
    return Math.max(1, Math.round(Number(interval) || DEFAULT_INTERVAL));
}

// How a slot rotates through its playlist: { order, interval (minutes) }.
// Set on the slot's target; a slot from before that falls back on what its
// playlist used to carry.
export function rotation(conf, target) {
    const pl = target && target.type === 'playlist' ? findPlaylist(conf, target.id) : null;
    const pick = key => (target && target[key] !== undefined) ? target[key]
        : (pl && pl[key] !== undefined) ? pl[key] : undefined;
    const order = pick('order'), interval = pick('interval');
    return {
        order: order === undefined ? DEFAULT_ORDER : cleanOrder(order),
        interval: interval === undefined ? DEFAULT_INTERVAL : cleanInterval(interval)
    };
}

// ---- shared playlists ----

export function parsePlaylists(str) {
    if (!str) return [];
    try {
        const data = JSON.parse(str);
        return cleanPlaylists(data && data.playlists);
    } catch (e) {
        return [];
    }
}

export function serializePlaylists(playlists) {
    return JSON.stringify({ version: VERSION, playlists: playlists || [] });
}

// The same conf with `playlists` in place of its own.
export function withPlaylists(conf, playlists) {
    return Object.assign({}, conf, { playlists: playlists || [] });
}

// `existing` plus those of `incoming` it does not have yet (matched by id).
export function mergePlaylists(existing, incoming) {
    const out = (existing || []).slice();
    (incoming || []).forEach(p => {
        if (!out.some(e => e.id === p.id)) out.push(p);
    });
    return out;
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
    if (t && t.type === 'playlist' && t.id) {
        const out = { type: 'playlist', id: String(t.id) };
        if (t.order !== undefined) out.order = cleanOrder(t.order);
        if (t.interval !== undefined) out.interval = cleanInterval(t.interval);
        return out;
    }
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
//   candidates   item first, then what to fall back on if it should not be
//                shown (see pickCandidate); the rest of the playlist in order
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
        candidates: [],
        playlist: null,
        index: -1,
        key: `${active.id}@${firedAt}`,
        nextSlotAt: nextSlotAt,
        nextChangeAt: nextSlotAt
    };

    const target = active.target;
    if (target.type === 'wallpaper') {
        if (ok(target)) {
            res.item = target;
            res.candidates = [target];
        }
    } else if (target.type === 'playlist') {
        const pl = findPlaylist(conf, target.id);
        const items = pl ? pl.items.filter(ok) : [];
        if (items.length > 0) {
            const how = rotation(conf, target);
            const interval = how.interval * MIN_MS;
            const step = Math.floor((now - firedAt) / interval);
            res.playlist = pl;
            // The slot id is part of the shuffle seed: playlists are shared, and
            // two monitors using the same one at the same time should not
            // shuffle it identically. Slot ids are unique to each monitor.
            res.index = playlistIndex(how.order, items.length, step, `${pl.id}@${firedAt}@${active.id}`);
            res.item = items[res.index];
            res.candidates = items.map((_, i) => items[(res.index + i) % items.length]);
            if (items.length > 1) {
                const nextStep = firedAt + (step + 1) * interval;
                if (res.nextChangeAt === null || nextStep < res.nextChangeAt) res.nextChangeAt = nextStep;
            }
        }
    }
    if (res.item) res.key += `#${res.item.wid}#${res.item.source}`;
    return res;
}

// Keeps monitors from showing the same wallpaper. `taken` holds the workshop
// ids showing on the other monitors, `current` the one showing on this one.
// The regular pick is used when it is free. Otherwise the playlist skips
// ahead to the next free wallpaper, preferably one other than `current`, so
// that the monitor still changes when its playlist moves on. A slot with a
// single wallpaper has one candidate and always gets it; so does a playlist
// with nothing free.
export function pickCandidate(res, taken, current) {
    if (!res || !res.item) return null;
    const used = taken || [];
    const free = res.candidates.filter(it => used.indexOf(it.wid) < 0);
    if (free.length === 0 || free[0] === res.item) return res.item;
    return free.find(it => it.wid !== current) || free[0];
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
