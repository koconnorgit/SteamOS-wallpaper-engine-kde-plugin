// Run with: node --test tests/schedule.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
// A zone with daylight saving, so the DST tests test something wherever they
// run. Set before the first Date is made.
process.env.TZ = 'America/New_York';

import * as S from '../plugin/contents/ui/js/schedule.mjs';

const at = (h, m = 0, day = 15) => new Date(2026, 5, day, h, m, 0, 0).getTime();
const wp = (wid) => ({ type: 'wallpaper', wid, source: `file:///w/${wid}/scene.pkg+scene`, title: wid, preview: '' });
const item = (wid) => ({ wid, source: `file:///w/${wid}/scene.pkg+scene`, title: wid, preview: '' });
const clock = (h, m = 0) => ({ type: 'clock', minutes: h * 60 + m });
const sunTrig = (event, offset = 0) => ({ type: 'sun', event, offset });
const sun = { sunrises: [at(5, 30)], sunsets: [at(20, 30)] };

function conf(slots, playlists = []) {
    return { version: 1, enabled: true, playlists, slots: slots.map((s, i) => ({ id: `s${i}`, ...s })) };
}

test('disabled or empty schedule resolves to null', () => {
    assert.equal(S.resolve(S.emptyConf(), at(12), sun), null);
    const c = conf([{ trigger: clock(13), target: wp('c') }]);
    c.enabled = false;
    assert.equal(S.resolve(c, at(14), sun), null);
});

test('clock slots: latest fired wins and holds until the next', () => {
    const c = conf([
        { trigger: clock(13), target: wp('c') },
        { trigger: clock(15), target: wp('d') },
    ]);
    assert.equal(S.resolve(c, at(13, 0), sun).item.wid, 'c');
    assert.equal(S.resolve(c, at(14, 59), sun).item.wid, 'c');
    assert.equal(S.resolve(c, at(15, 0), sun).item.wid, 'd');
    // before the first slot of the day, yesterday's last slot is still active
    const r = S.resolve(c, at(9), sun);
    assert.equal(r.item.wid, 'd');
    assert.equal(r.firedAt, at(15, 0, 14));
    assert.equal(r.nextSlotAt, at(13));
    assert.equal(r.nextChangeAt, at(13));
});

test('sun slots with offsets', () => {
    const c = conf([
        { trigger: sunTrig('sunset'), target: wp('a') },
        { trigger: sunTrig('sunset', 60), target: wp('b') },
        { trigger: sunTrig('sunrise', -30), target: wp('m') },
    ]);
    assert.equal(S.resolve(c, at(20, 29), sun).item.wid, 'm');
    assert.equal(S.resolve(c, at(20, 30), sun).item.wid, 'a');
    assert.equal(S.resolve(c, at(21, 29), sun).item.wid, 'a');
    assert.equal(S.resolve(c, at(21, 30), sun).item.wid, 'b');
    assert.equal(S.resolve(c, at(5, 0), sun).item.wid, 'm');
    // earlier the same night only yesterday's sunset is relevant (extrapolated)
    const r = S.resolve(c, at(2), sun);
    assert.equal(r.item.wid, 'b');
    assert.equal(r.firedAt, at(21, 30, 14));
    assert.equal(r.nextSlotAt, at(5, 0));
});

test('mixed clock and sun slots follow real firing order', () => {
    const c = conf([
        { trigger: clock(19), target: wp('evening') },
        { trigger: sunTrig('sunset', 60), target: wp('dark') },
    ]);
    // summer: sunset+1h (21:30) is after 19:00
    assert.equal(S.resolve(c, at(20), sun).item.wid, 'evening');
    assert.equal(S.resolve(c, at(22), sun).item.wid, 'dark');
    // winter: sunset+1h (17:30) is before 19:00
    const winter = { sunrises: [at(8)], sunsets: [at(16, 30)] };
    assert.equal(S.resolve(c, at(18), winter).item.wid, 'dark');
    assert.equal(S.resolve(c, at(19, 30), winter).item.wid, 'evening');
});

test('sun slots are ignored when sun times are unknown', () => {
    const c = conf([
        { trigger: sunTrig('sunset'), target: wp('a') },
        { trigger: clock(8), target: wp('c') },
    ]);
    assert.equal(S.resolve(c, at(23), null).item.wid, 'c');
    assert.equal(S.resolve(c, at(23), { sunrises: [], sunsets: [] }).item.wid, 'c');
    assert.equal(S.resolve(conf([{ trigger: sunTrig('sunset'), target: wp('a') }]), at(23), null), null);
});

test('sequential playlist rotates from the moment the slot fired', () => {
    const pl = { id: 'p1', name: 'P', order: 'sequential', interval: 20, items: ['x', 'y', 'z'].map(item) };
    const c = conf([{ trigger: clock(10), target: { type: 'playlist', id: 'p1' } }], [pl]);
    const seq = [0, 19, 20, 40, 60, 80].map(m => S.resolve(c, at(10, 0) + m * 60000, sun).item.wid);
    assert.deepEqual(seq, ['x', 'x', 'y', 'z', 'x', 'y']);
    const r = S.resolve(c, at(10, 25), sun);
    assert.equal(r.nextChangeAt, at(10, 40));
    assert.equal(r.nextSlotAt, at(10, 0, 16));
});

test('playlist step never outlives the slot', () => {
    const pl = { id: 'p1', name: 'P', order: 'sequential', interval: 45, items: ['x', 'y'].map(item) };
    const c = conf([
        { trigger: clock(10), target: { type: 'playlist', id: 'p1' } },
        { trigger: clock(11), target: wp('c') },
    ], [pl]);
    assert.equal(S.resolve(c, at(10, 50), sun).nextChangeAt, at(11));
    assert.equal(S.resolve(c, at(11), sun).item.wid, 'c');
});

test('shuffle is stable, covers every item per pass, and avoids back-to-back repeats', () => {
    const ids = ['a', 'b', 'c', 'd', 'e'];
    const pl = { id: 'p1', name: 'P', order: 'shuffle', interval: 10, items: ids.map(item) };
    const c = conf([{ trigger: clock(0), target: { type: 'playlist', id: 'p1' } }], [pl]);
    const seq = [];
    for (let step = 0; step < 100; step++) {
        const now = at(0) + step * 600000 + 1000;
        const w = S.resolve(c, now, sun).item.wid;
        assert.equal(S.resolve(c, now + 5000, sun).item.wid, w);
        seq.push(w);
    }
    for (let p = 0; p < 20; p++)
        assert.deepEqual(seq.slice(p * 5, p * 5 + 5).sort(), ids);
    for (let i = 1; i < seq.length; i++) assert.notEqual(seq[i], seq[i - 1]);
    assert.notDeepEqual(seq.slice(0, 5), seq.slice(5, 10));
});

test('missing wallpapers are skipped', () => {
    const pl = { id: 'p1', name: 'P', order: 'sequential', interval: 10, items: ['x', 'gone', 'z'].map(item) };
    const c = conf([
        { trigger: clock(10), target: { type: 'playlist', id: 'p1' } },
        { trigger: clock(12), target: wp('gone') },
    ], [pl]);
    const avail = (it) => it.wid !== 'gone';
    assert.equal(S.resolve(c, at(10, 10), sun, avail).item.wid, 'z');
    const r = S.resolve(c, at(12, 10), sun, avail);
    assert.equal(r.item, null);
    assert.equal(r.slot.id, 's1');
});

test('empty or dangling playlist target yields no item but still schedules', () => {
    const c = conf([{ trigger: clock(10), target: { type: 'playlist', id: 'nope' } }]);
    const r = S.resolve(c, at(11), sun);
    assert.equal(r.item, null);
    assert.equal(r.nextChangeAt, at(10, 0, 16));
});

test('key changes exactly when the wallpaper changes', () => {
    const pl = { id: 'p1', name: 'P', order: 'sequential', interval: 30, items: ['x', 'y'].map(item) };
    const c = conf([{ trigger: clock(10), target: { type: 'playlist', id: 'p1' } }], [pl]);
    const k = (m) => S.resolve(c, at(10, m), sun).key;
    assert.equal(k(0), k(29));
    assert.notEqual(k(29), k(30));
});

test('parse sanitises input and round-trips', () => {
    assert.deepEqual(S.parse(''), S.emptyConf());
    assert.deepEqual(S.parse('{broken'), S.emptyConf());
    const c = conf([
        { trigger: sunTrig('sunset', 60), target: wp('a') },
        { trigger: clock(13), target: { type: 'playlist', id: 'p1' } },
    ], [{ id: 'p1', name: 'Ünïcode ✓', order: 'shuffle', interval: 15, items: [item('x')] }]);
    // serialize() keeps the per-monitor part; playlists travel separately
    assert.deepEqual(S.parse(S.serialize(c)), { ...c, playlists: [] });
    assert.deepEqual(S.withPlaylists(S.parse(S.serialize(c)), S.parsePlaylists(S.serializePlaylists(c.playlists))), c);
    const dirty = S.parse(JSON.stringify({ enabled: 1, slots: [{ id: 1, trigger: { type: 'clock', minutes: 1500 }, target: { type: 'wallpaper' } }], playlists: [{ id: 'p', interval: 0, items: [{}, item('x')] }] }));
    assert.equal(dirty.slots[0].trigger.minutes, 60);
    assert.equal(dirty.slots[0].target.type, 'none');
    assert.equal(dirty.playlists[0].interval, 30);
    assert.equal(dirty.playlists[0].items.length, 1);
});

test('fromDayNight builds sunrise and sunset slots', () => {
    const c = S.fromDayNight(item('day'), item('night'));
    assert.equal(c.enabled, true);
    assert.equal(S.resolve(c, at(12), sun).item.wid, 'day');
    assert.equal(S.resolve(c, at(23), sun).item.wid, 'night');
    assert.equal(S.resolve(c, at(3), sun).item.wid, 'night');
    assert.equal(S.fromDayNight(null, null).enabled, false);
});

test('describe and sort helpers', () => {
    assert.equal(S.describeTrigger(clock(13, 5)), '13:05');
    assert.equal(S.describeTrigger(sunTrig('sunset')), 'Sunset');
    assert.equal(S.describeTrigger(sunTrig('sunset', 60)), '1 h after sunset');
    assert.equal(S.describeTrigger(sunTrig('sunrise', -90)), '1 h 30 min before sunrise');
    assert.equal(S.todayMinutes(sunTrig('sunset', 60), at(9), sun), 21 * 60 + 30);
    assert.equal(S.todayMinutes(sunTrig('sunset'), at(9), null), null);
    const c = conf([
        { trigger: sunTrig('sunset', 60), target: wp('b') },
        { trigger: clock(13), target: wp('c') },
        { trigger: sunTrig('sunrise'), target: wp('m') },
    ]);
    assert.deepEqual(S.sortedSlots(c, at(9), sun).map(e => e.slot.id), ['s2', 's1', 's0']);
});

test('DST change does not shift clock slots', () => {
    // 2026-03-08 is the US spring-forward date.
    const c = conf([{ trigger: clock(13), target: wp('c') }, { trigger: clock(1), target: wp('n') }]);
    const now = new Date(2026, 2, 8, 14, 0).getTime();
    const r = S.resolve(c, now, null);
    assert.equal(r.item.wid, 'c');
    assert.equal(new Date(r.firedAt).getHours(), 13);
    assert.equal(new Date(r.nextSlotAt).getHours(), 1);
});

test('shared playlists parse, merge and resolve with a monitor\'s slots', () => {
    assert.deepEqual(S.parsePlaylists(''), []);
    assert.deepEqual(S.parsePlaylists('{broken'), []);
    assert.deepEqual(S.parsePlaylists('{"version":1}'), []);
    const a = { id: 'pa', name: 'A', order: 'sequential', interval: 10, items: [item('x'), item('y')] };
    const b = { id: 'pb', name: 'B', order: 'sequential', interval: 10, items: [item('z')] };
    const edited = { ...a, name: 'A, edited elsewhere' };
    // merging keeps what the store already has and adds only unknown ids
    assert.deepEqual(S.mergePlaylists([edited], [a, b]), [edited, b]);
    assert.deepEqual(S.mergePlaylists([], [a]), [a]);

    // a legacy per-monitor conf still yields its playlists, for migration
    const legacy = JSON.stringify(conf([{ trigger: clock(8), target: { type: 'playlist', id: 'pa' } }], [a]));
    assert.deepEqual(S.parse(legacy).playlists, [a]);

    // two monitors, one playlist: same items, each from its own slots
    const store = S.parsePlaylists(S.serializePlaylists([a, b]));
    const mon1 = S.withPlaylists(S.parse(S.serialize(conf([{ trigger: clock(8), target: { type: 'playlist', id: 'pa' } }]))), store);
    const mon2 = S.withPlaylists(S.parse(S.serialize(conf([{ trigger: clock(9), target: { type: 'playlist', id: 'pa' } }]))), store);
    const now = new Date(2026, 5, 1, 9, 5).getTime();
    assert.equal(S.resolve(mon1, now, null).item.wid, 'x'); // 65 min in: step 6
    assert.equal(S.resolve(mon2, now, null).item.wid, 'x'); // 5 min in: step 0
    assert.equal(S.resolve(mon1, now + 10 * 60000, null).item.wid, 'y');
    // a slot whose playlist was deleted elsewhere shows nothing new
    const gone = S.withPlaylists(mon1, [b]);
    assert.equal(S.resolve(gone, now, null).item, null);
});

test('monitors sharing a shuffled playlist shuffle it differently', () => {
    const ids = Array.from({ length: 12 }, (_, i) => `w${i}`);
    const shared = [{ id: 'p', name: 'P', order: 'shuffle', interval: 10, items: ids.map(item) }];
    const monitor = slotId => ({ version: 1, enabled: true, playlists: shared,
        slots: [{ id: slotId, trigger: clock(8), target: { type: 'playlist', id: 'p' } }] });
    const order = c => ids.map((_, i) => S.resolve(c, at(8, 5 + i * 10), null).item.wid);
    const a = order(monitor('sA')), b = order(monitor('sB'));
    assert.notDeepEqual(a, b);
    // each is still a full pass with no repeats, and stable when recomputed
    assert.deepEqual([...a].sort(), [...ids].sort());
    assert.deepEqual([...b].sort(), [...ids].sort());
    assert.deepEqual(order(monitor('sA')), a);
});

test('pickCandidate skips wallpapers showing on other monitors', () => {
    const pl = [{ id: 'p', name: 'P', order: 'sequential', interval: 10, items: ['a', 'b', 'c'].map(item) }];
    const c = conf([{ trigger: clock(8), target: { type: 'playlist', id: 'p' } }], pl);
    const res = S.resolve(c, at(8, 15), null); // step 1: b, then c, a
    assert.deepEqual(res.candidates.map(i => i.wid), ['b', 'c', 'a']);
    assert.equal(S.pickCandidate(res, []).wid, 'b');
    assert.equal(S.pickCandidate(res, ['b']).wid, 'c');
    assert.equal(S.pickCandidate(res, ['c', 'b']).wid, 'a');
    // nothing free: fall back to the regular pick
    assert.equal(S.pickCandidate(res, ['a', 'b', 'c']).wid, 'b');
    // skipping ahead prefers a change over staying on the current wallpaper
    assert.equal(S.pickCandidate(res, ['b'], 'c').wid, 'a');
    assert.equal(S.pickCandidate(res, ['b', 'a'], 'c').wid, 'c');
    // ...but the regular pick is kept even when it is already showing here
    assert.equal(S.pickCandidate(res, ['c'], 'b').wid, 'b');
    // unavailable wallpapers are never candidates
    const some = S.resolve(c, at(8, 15), null, it => it.wid !== 'c');
    assert.deepEqual(some.candidates.map(i => i.wid), ['b', 'a']);
    // a slot with one wallpaper is an explicit choice and is not moved
    const fixed = S.resolve(conf([{ trigger: clock(8), target: wp('a') }]), at(9), null);
    assert.equal(S.pickCandidate(fixed, ['a']).wid, 'a');
    assert.equal(S.pickCandidate(null, ['a']), null);
});

test('rotation settings belong to the slot, not the playlist', () => {
    const plain = [{ id: 'p', name: 'P', items: ['a', 'b', 'c'].map(item) }];
    assert.deepEqual(S.parsePlaylists(S.serializePlaylists(plain)), plain);
    const c = conf([
        { trigger: clock(8), target: { type: 'playlist', id: 'p', interval: 10, order: 'sequential' } },
        { trigger: clock(12), target: { type: 'playlist', id: 'p', interval: 60, order: 'sequential' } },
        { trigger: clock(20), target: { type: 'playlist', id: 'p' } },
    ], plain);
    // the same playlist changes every 10 min in one slot and hourly in another
    assert.equal(S.resolve(c, at(8, 25), null).item.wid, 'c');
    assert.equal(S.resolve(c, at(8, 25), null).nextChangeAt, at(8, 30));
    assert.equal(S.resolve(c, at(12, 25), null).item.wid, 'a');
    assert.equal(S.resolve(c, at(13, 5), null).item.wid, 'b');
    // defaults when a slot says nothing
    assert.deepEqual(S.rotation(c, c.slots[2].target), { order: 'sequential', interval: 30 });
    assert.equal(S.resolve(c, at(20, 35), null).item.wid, 'b');
    // slot settings survive a save and reload
    const back = S.withPlaylists(S.parse(S.serialize(c)), plain);
    assert.deepEqual(back.slots[1].target, { type: 'playlist', id: 'p', interval: 60, order: 'sequential' });
    assert.deepEqual(back.slots[2].target, { type: 'playlist', id: 'p' });

    // a slot from before the change keeps rotating the way its playlist said
    const old = [{ id: 'p', name: 'P', order: 'shuffle', interval: 15, items: ['a', 'b', 'c'].map(item) }];
    const legacy = conf([{ trigger: clock(8), target: { type: 'playlist', id: 'p' } }], S.parsePlaylists(S.serializePlaylists(old)));
    assert.deepEqual(S.rotation(legacy, legacy.slots[0].target), { order: 'shuffle', interval: 15 });
    // ...until the slot gets settings of its own
    legacy.slots[0].target.interval = 5;
    assert.deepEqual(S.rotation(legacy, legacy.slots[0].target), { order: 'shuffle', interval: 5 });
});

// ---- the decision a monitor makes at each evaluation ----

const MIN = 60000;
const plOf = (ids, extra = {}) => [{ id: 'p', name: 'P', items: ids.map(item), ...extra }];
const playlistConf = (ids, target = {}, slotId = 's0') => ({
    version: 1, enabled: true, playlists: plOf(ids),
    slots: [{ id: slotId, trigger: clock(8), target: { type: 'playlist', id: 'p', interval: 10, order: 'sequential', ...target } }],
});
const state = (over = {}) => ({ key: '', holdUntil: 0, ok: true, redecide: false, taken: [], current: '', ...over });

test('decide: a new step picks its wallpaper and reports the key', () => {
    const c = playlistConf(['a', 'b', 'c']);
    const res = S.resolve(c, at(8, 5), null);
    const d = S.decide(res, at(8, 5), state());
    assert.equal(d.item.wid, 'a');
    assert.equal(d.key, res.key);
    assert.equal(d.wake, at(8, 10));
    assert.equal(d.staysUntil, at(8, 10));
    // the same step again: nothing to do
    const again = S.decide(res, at(8, 6), state({ key: res.key, current: 'a' }));
    assert.equal(again.item, null);
    assert.equal(again.key, null);
});

test('decide: a hand-picked wallpaper holds, then the schedule resumes', () => {
    const c = playlistConf(['a', 'b', 'c']);
    const res = S.resolve(c, at(8, 5), null);
    const hold = at(8, 7);
    const held = S.decide(res, at(8, 5), state({ holdUntil: hold, current: 'x' }));
    assert.equal(held.item, null);
    assert.equal(held.clearHold, false);
    assert.equal(held.wake, hold);          // sooner than the step at 8:10
    assert.equal(held.staysUntil, hold);
    const after = S.decide(S.resolve(c, at(8, 8), null), at(8, 8), state({ holdUntil: hold, current: 'x' }));
    assert.equal(after.clearHold, true);
    assert.equal(after.item.wid, 'a');
});

test('decide: nothing changes while paused, and it catches up afterwards', () => {
    const c = playlistConf(['a', 'b', 'c']);
    const res = S.resolve(c, at(8, 25), null);       // step 2: c
    const paused = S.decide(res, at(8, 25), state({ ok: false, key: 'old', current: 'a' }));
    assert.equal(paused.item, null);
    assert.equal(paused.staysUntil, 0);              // not known when it will resume
    const resumed = S.decide(res, at(8, 26), state({ key: 'old', current: 'a' }));
    assert.equal(resumed.item.wid, 'c');
});

test('decide: deciding a step again keeps what is showing when the regular pick is taken', () => {
    const c = playlistConf(['a', 'b', 'c', 'd']);
    const res = S.resolve(c, at(8, 5), null);        // regular pick: a
    // this monitor skipped to b because a is on another monitor
    const first = S.decide(res, at(8, 5), state({ taken: ['a'], current: 'z' }));
    assert.equal(first.item.wid, 'b');
    // a playlist is saved somewhere: decided again, any number of times
    for (let i = 0; i < 3; i++) {
        const d = S.decide(res, at(8, 6), state({ key: res.key, redecide: true, taken: ['a'], current: 'b' }));
        assert.equal(d.item.wid, 'b');
    }
    // once the regular pick is free it is used
    const freed = S.decide(res, at(8, 6), state({ key: res.key, redecide: true, taken: [], current: 'b' }));
    assert.equal(freed.item.wid, 'a');
    // an edited schedule replaces a wallpaper picked by hand
    const edited = S.decide(res, at(8, 6), state({ key: res.key, redecide: true, current: 'x' }));
    assert.equal(edited.item.wid, 'a');
});

test('decide: an empty schedule or target does nothing', () => {
    assert.deepEqual(S.decide(null, at(9), state()), { wake: null, clearHold: false, item: null, key: null, staysUntil: 0 });
    const c = conf([{ trigger: clock(8), target: { type: 'playlist', id: 'nope' } }]);
    const d = S.decide(S.resolve(c, at(9), null), at(9), state());
    assert.equal(d.item, null);
    assert.equal(d.wake, at(8, 0, 16));
});

test('takenBy leaves out wallpapers that are about to be replaced', () => {
    const now = at(8, 10);
    const others = [
        { wid: 'a', until: now },                    // changing at this very step
        { wid: 'b', until: now + 1500 },             // within the margin
        { wid: 'c', until: now + 10 * MIN },         // staying
        { wid: 'd', until: 0 },                      // not known: staying
        { wid: '', until: 0 },
    ];
    assert.deepEqual(S.takenBy(others, now), ['c', 'd']);
    assert.deepEqual(S.takenBy(null, now), []);
});

// Runs monitors the way the plugin does: each evaluates at its own step
// boundaries, in the order given, seeing what the others show at that moment.
function simulate(monitors, from, minutes) {
    const mons = monitors.map(m => ({ ...m, key: '', current: '', until: 0, shown: [] }));
    for (let t = from; t <= from + minutes * MIN; t += MIN) {
        mons.forEach(m => {
            const res = S.resolve(m.conf, t, null);
            const others = mons.filter(o => o !== m && o.current).map(o => ({ wid: o.current, until: o.until }));
            const d = S.decide(res, t, state({ key: m.key, taken: S.takenBy(others, t), current: m.current }));
            if (d.item) { m.key = d.key; m.current = d.item.wid; }
            m.until = d.staysUntil;
        });
        mons.forEach(m => m.shown.push(m.current));
        const now = mons.map(m => m.current);
        assert.equal(new Set(now).size, now.length, `monitors show the same wallpaper at +${(t - from) / MIN} min: ${now}`);
    }
    return mons;
}

test('two monitors on one playlist both change at every step', () => {
    const ids = ['a', 'b', 'c', 'd', 'e', 'f'];
    // same playlist, same slot time, same interval, in order: the worst case,
    // their regular picks are identical
    const mons = simulate([
        { conf: playlistConf(ids, {}, 'sA') },
        { conf: playlistConf(ids, {}, 'sB') },
    ], at(8), 80);
    mons.forEach(m => {
        for (let step = 1; step <= 8; step++) {
            assert.notEqual(m.shown[step * 10], m.shown[step * 10 - 1], `a monitor did not change at step ${step}`);
        }
    });
    assert.deepEqual(mons[0].shown.filter((_, i) => i % 10 === 0).slice(0, 5), ['a', 'b', 'c', 'd', 'e']);
    assert.deepEqual(mons[1].shown.filter((_, i) => i % 10 === 0).slice(0, 5), ['b', 'c', 'd', 'e', 'f']);
});

test('three monitors shuffling one playlist at different paces never match', () => {
    const ids = Array.from({ length: 14 }, (_, i) => `w${i}`);
    const mons = simulate([
        { conf: playlistConf(ids, { order: 'shuffle', interval: 35 }, 'sA') },
        { conf: playlistConf(ids, { order: 'shuffle', interval: 33 }, 'sB') },
        { conf: playlistConf(ids, { order: 'shuffle', interval: 30 }, 'sC') },
    ], at(8), 12 * 60);
    // and every monitor does keep changing
    mons.forEach((m, i) => {
        const interval = [35, 33, 30][i];
        for (let t = interval; t < 12 * 60; t += interval) {
            assert.notEqual(m.shown[t], m.shown[t - 1], `monitor ${i} did not change at +${t} min`);
        }
    });
});

// ---- playlists edited in two places ----

test('mergeConcurrent keeps playlists added elsewhere and honours deletions', () => {
    const p = (id, name = id) => ({ id, name, items: [] });
    const base = [p('a'), p('b'), p('c')];
    const mine = [p('a', 'renamed'), p('c'), p('mine')];           // b deleted, a renamed, one added
    const theirs = [p('a'), p('b'), p('c'), p('theirs')];          // one added elsewhere
    assert.deepEqual(S.mergeConcurrent(base, mine, theirs).map(x => `${x.id}:${x.name}`),
                     ['a:renamed', 'c:c', 'mine:mine', 'theirs:theirs']);
    // nothing happened elsewhere: the edit as it is
    assert.deepEqual(S.mergeConcurrent(base, mine, base), mine);
    assert.deepEqual(S.mergeConcurrent(null, null, null), []);
});

// ---- slots ----

test('slots at the same time: the later one wins and both are marked', () => {
    const c = conf([
        { trigger: clock(9), target: wp('first') },
        { trigger: clock(9), target: wp('second') },
        { trigger: clock(12), target: wp('noon') },
    ]);
    assert.equal(S.resolve(c, at(10), null).item.wid, 'second');
    const sorted = S.sortedSlots(c, at(10), null);
    assert.deepEqual(sorted.map(e => e.clash), [true, true, false]);
    // a clock slot can coincide with a sun slot
    const mixed = conf([{ trigger: sunTrig('sunset'), target: wp('s') }, { trigger: clock(20, 30), target: wp('c') }]);
    assert.deepEqual(S.sortedSlots(mixed, at(10), sun).map(e => e.clash), [true, true]);
    // a slot whose time is unknown clashes with nothing
    assert.deepEqual(S.sortedSlots(mixed, at(10), null).map(e => e.clash), [false, false]);
});

test('a sun time that moves a little does not restart a playlist', () => {
    const ids = ['a', 'b', 'c', 'd', 'e'];
    const c = { version: 1, enabled: true, playlists: plOf(ids),
        slots: [{ id: 's0', trigger: sunTrig('sunset'), target: { type: 'playlist', id: 'p', interval: 20, order: 'shuffle' } }] };
    const estimate = (shift) => ({ sunrises: [at(5, 30)], sunsets: [at(20, 30) + shift * MIN] });
    // 15 minutes into the slot, well inside the first step
    const a = S.resolve(c, at(20, 45), estimate(0));
    const b = S.resolve(c, at(20, 45), estimate(1));
    assert.equal(a.key, b.key);
    assert.equal(a.item.wid, b.item.wid);
    // a clock slot's key still carries its exact time
    const k = S.resolve(conf([{ trigger: clock(13), target: wp('c') }]), at(14), null).key;
    assert.ok(k.includes(String(at(13))));
});

test('DST: a slot inside the skipped or repeated hour fires once', () => {
    const c = conf([{ trigger: clock(2, 30), target: wp('night') }, { trigger: clock(12), target: wp('noon') }]);
    // spring forward, 2026-03-08: 02:30 does not exist
    let r = S.resolve(c, new Date(2026, 2, 8, 5, 0).getTime(), null);
    assert.equal(r.item.wid, 'night');
    assert.equal(r.firedAt, new Date(2026, 2, 8, 2, 30).getTime());
    assert.ok(r.firedAt < new Date(2026, 2, 8, 5, 0).getTime());
    // fall back, 2026-11-01: 01:00-02:00 happens twice, 02:30 once
    r = S.resolve(c, new Date(2026, 10, 1, 5, 0).getTime(), null);
    assert.equal(r.item.wid, 'night');
    assert.equal(new Date(r.firedAt).getHours(), 2);
    assert.equal(new Date(r.nextSlotAt).getHours(), 12);
    // a playlist steps by elapsed time, so the extra hour is an extra step
    const pc = playlistConf(['a', 'b', 'c', 'd', 'e', 'f', 'g'], { interval: 60 });
    pc.slots[0].trigger = clock(0);
    const before = S.resolve(pc, new Date(2026, 10, 1, 0, 30).getTime(), null);
    const after = S.resolve(pc, new Date(2026, 10, 1, 3, 30).getTime(), null);
    assert.equal(before.item.wid, 'a');
    assert.equal(after.item.wid, 'e');     // 4 hours elapsed between 0:30 and 3:30
});

// ---- edge cases ----

test('playlistIndex: small playlists and pass boundaries', () => {
    assert.equal(S.playlistIndex('shuffle', 0, 5, 'x'), -1);
    assert.equal(S.playlistIndex('shuffle', 1, 5, 'x'), 0);
    assert.equal(S.playlistIndex('sequential', 3, 7, 'x'), 1);
    for (let seed = 0; seed < 200; seed++) {
        const base = `seed${seed}`;
        // two items alternate
        assert.notEqual(S.playlistIndex('shuffle', 2, 0, base), S.playlistIndex('shuffle', 2, 1, base));
        for (const count of [3, 4, 7]) {
            for (let pass = 0; pass < 4; pass++) {
                const order = Array.from({ length: count }, (_, i) => S.playlistIndex('shuffle', count, pass * count + i, base));
                // every pass shows each wallpaper once
                assert.deepEqual([...order].sort((a, b) => a - b), Array.from({ length: count }, (_, i) => i));
                // and does not open with the one the previous pass ended on
                if (pass > 0) {
                    assert.notEqual(order[0], S.playlistIndex('shuffle', count, pass * count - 1, base));
                }
            }
        }
    }
});

test('parse copes with json that is not a schedule', () => {
    for (const text of ['null', '[]', '3', '"x"', 'true', '{"slots": 5}', '{"slots": [null, 7, {}]}']) {
        const c = S.parse(text);
        assert.ok(Array.isArray(c.slots), text);
        assert.ok(Array.isArray(c.playlists), text);
        assert.doesNotThrow(() => S.resolve(c, at(12), sun), text);
    }
    for (const text of ['null', '{}', '3', '{"playlists": 5}', '{"playlists": [null, {"id": "p"}]}']) {
        assert.ok(Array.isArray(S.parsePlaylists(text)), text);
    }
});

test('sun slots with offsets across midnight and bad sun data', () => {
    // an hour after a 23:30 sunset fires at 00:30 the next day
    const late = { sunrises: [at(5, 30, 14), at(5, 30)], sunsets: [at(23, 30, 14), at(23, 30)] };
    const c = conf([{ trigger: sunTrig('sunset', 60), target: wp('late') }, { trigger: clock(12), target: wp('noon') }]);
    assert.equal(S.resolve(c, at(0, 45), late).item.wid, 'late');
    assert.equal(S.resolve(c, at(0, 15), late).item.wid, 'noon');
    // junk in the sun lists is ignored
    const junk = { sunrises: [0, NaN, at(5, 30)], sunsets: [undefined, at(20, 30)] };
    assert.doesNotThrow(() => S.resolve(c, at(21), junk));
    assert.equal(S.resolve(conf([{ trigger: sunTrig('sunset'), target: wp('s') }]), at(21), junk).item.wid, 's');
});

test('a wallpaper that disappears mid-slot is skipped from then on', () => {
    const c = playlistConf(['a', 'b', 'c']);
    assert.equal(S.resolve(c, at(8, 15), null).item.wid, 'b');
    const gone = S.resolve(c, at(8, 15), null, it => it.wid !== 'b');
    assert.notEqual(gone.item.wid, 'b');
    assert.ok(gone.candidates.every(it => it.wid !== 'b'));
});
