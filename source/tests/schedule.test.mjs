// Run with: node --test tests/schedule.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
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
    assert.deepEqual(S.parse(S.serialize(c)), c);
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
    // 2026-03-08 is the US spring-forward date; harmless in other zones.
    const c = conf([{ trigger: clock(13), target: wp('c') }, { trigger: clock(1), target: wp('n') }]);
    const now = new Date(2026, 2, 8, 14, 0).getTime();
    const r = S.resolve(c, now, null);
    assert.equal(r.item.wid, 'c');
    assert.equal(new Date(r.firedAt).getHours(), 13);
    assert.equal(new Date(r.nextSlotAt).getHours(), 1);
});
