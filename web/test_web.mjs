// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device, a v11 drums-only FM-1
//   (fm1-drummer: one track, no engine; the commands of SLOOP's synth side unanswered), live pushes, the drum lanes,
//   locks, nudges, fill conditions, backup / restore (SLOOP's objects left out), the drum kit and synth pages, the
//   songs and the player, its tab layout and ja/en strings, and the user-sample pipeline byte for byte against
//   tools/sampleio.py
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { logicalImage, productOf } from "./fm1pkg.js";
import { Updater, pack7, unpack7, FM1_PORT } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const PYTHON = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const py = (code, ...args) => execFileSync(PYTHON, ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = fileURLToPath(new URL(".", import.meta.url));

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, parseWav, resample, normalize, rootFromName, buildSlot, makeMockDevice, CMD, PROTO, SMP,
   wavFile, zipStore, crc32, startWatch, fmtValue, F, DRUM_LANES, LV_NAMES, emptyDrum, lockable, MICRO, FC, fillGet, fillSet, pack7, unpack7,
   parseNote, backupCapture, backupRestore, backupObjects, b64enc, b64dec, BK, KIT, KIT_SONGS, kitOptions, kitPad, kitZones, kitLen, kitBytes,
   kitFit, kitSplit, kitLaneOf, kitPlace, zipRead, ARR, mockSettings, DSYN, DSYN_MOCK_NAMES, DSYN_MOCK_KITS, SetGate, bkProjFormat, bkProjOk,
   bkFwAtLeast, SNG, PLAY, SED, PEDALIN, PCLK })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder, Blob, Response, DecompressionStream });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
/* a request expected to get no reply (the command is gone, or not for this one track) */
const noReply = (rq, r) => rq(r, { timeout: 60, retries: 0, quiet: true }).then(() => false, (e) => /^timeout/.test(e.message));
const fw = (f) => readFileSync(join(HERE, "../firmware/src/" + f), "utf8");

/* the device v11: INFO, the parameters, DUMP / SET, a project, a sample, the commands that are gone */
async function editorMock() {
  const C = E.CMD;
  const { rq, ev, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.nengines === 0 && !info.engines.length && info.ntrk === 1 && info.proto === 11 && E.PROTO === 11 && info.pcount === 71 && info.gcount === 45
    && info.pe0 === 63 && info.nstep === 64, "editor: INFO v11: no engine, one track, protocol 11 (71 / 45 / pe0 63)");
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[C.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {   /* the drum track's P_E0 is its kit, P_E1..P_E7 nothing (params.c e_desc / E_NONE); G_DRCH is "CH" */
    const pc = fw("params.c");
    const e = [];
    for (let i = 0; i < 8; i++) e.push(E.parse[C.DESC](await rq(E.req.desc(0, info.pe0 + i))));
    const ch = E.parse[C.DESC](await rq(E.req.desc(1, 24)));
    ok(e[0].label === "KIT" && e.slice(1).every((d) => d.label === "-" && d.min === 0 && d.max === 0) && ch.label === "CH" && ch.def === 10
      && /static const param_desc_t E_NONE = PD\("-", F_INT, 0, 0, 0\);/.test(pc) && /\[G_DRCH\] = PE\("CH", N_MCH, 10\)/.test(pc)
      && /is_drum\(t\) && id == P_E0 \? &DRUM_KIT_DESC : &E_NONE/.test(pc), "editor: KIT on P_E0, P_E1..7 nothing, the drums' channel CH (mock == params.c)");
  }
  const dump = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(dump.engine === 0 && dump.preset === 0 && dump.p.length === info.pcount && dump.g.length === info.gcount && dump.p[info.pe0] === 5,
    "editor: DUMP (engine byte 0 = the number of engines: the drum track; kit 808)");
  const set = E.parse[C.SET](await rq(E.req.set(0, 50, 500)));
  const eng = E.parse[C.SET](await rq(E.req.set(1, 20, 3)));
  ok(set.value === 63 && eng.value === 0, "editor: SET clamps to the range; G_ENGSEL stays (no engine to change)");
  const pj = E.parse[C.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  /* sample upload as smpUpload() does it, into USR3; USR1 and USR2 hold the songs: refused (rc 4) */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[C.SMP_BEGIN](await rq(E.req.smpBegin(2), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[C.SMP_WRITE](await rq(E.req.smpWrite(2, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[C.SMP_END](await rq(E.req.smpEnd(2, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[C.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[2].zones === 1 && si.slots[2].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  const u1 = [];
  for (const k of [0, 1])
    u1.push(E.parse[C.SMP_BEGIN](await rq(E.req.smpBegin(k))).rc, E.parse[C.SMP_ERASE](await rq(E.req.smpErase(k))).rc,
      E.parse[C.SMP_WRITE](await rq(E.req.smpWrite(k, 512, data.subarray(0, 256)))).rc);
  const ec = fw("editor.c");
  ok(js(E.SMP.SONGS) === js([0, 1]) && u1.join() === "4,4,4,4,4,4" && !si.slots[0].zones && !si.slots[1].zones && /#define SNG_SMP_TAKEN\(k\) \(\(k\) < 2u\)/.test(fw("seq.c"))
    && (ec.match(/if \(SNG_SMP_TAKEN\(a\[0\]\)\) \{/g) || []).length === 2, "editor: USR1 and USR2 (the songs' flash) refuse sound: rc 4 (editor.c), listed empty");
  /* PRESET 8, NAMES 10, UP_LIST..UP_ERASE 16..21, FM6_GET..FM6_ERASE 68..71: gone, no reply */
  const gone = [];
  for (const [cmd, args] of [[8, [0, 0]], [10, [0]], [16, [0, 16]], [17, [0]], [19, [0, 65, 0]], [20, [0]], [21, [0]], [68, [0, 0]], [70, []], [71, [0]]])
    gone.push(await noReply(rq, [cmd, args]));
  ok(gone.every(Boolean) && ev.timeouts === 0 && !("PRESET" in C) && !("NAMES" in C) && !("UP_LIST" in C) && !("FM6_GET" in C)
    && !E.req.preset && !E.req.names && !E.req.upList && !E.req.fm6Get, "editor: PRESET, NAMES, UP_*, FM6_*: no reply, none in the editor");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  done();
  /* a SLOOP (protocol 10 and before): INFO says so, nothing else answers; the page stops there and says why */
  const o = attachMock({ proto: 10 });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const od = await noReply(o.rq, E.req.dump());
  const page = html.slice(html.indexOf("async function load(d)"), html.indexOf("async function getDescOrNull"));
  ok(oi.proto === 10 && oi.nengines === 12 && oi.ntrk === 4 && od && /if \(!\(d\.info\.proto >= PROTO\)\)/.test(page)
    && page.indexOf("proto >= PROTO") < page.indexOf("getDescOrNull(0, i)") && /sayK\("fwRefused", e\.refused\)/.test(html)
    && /fwRefused: "[^"]*BackBeat[^"]*11/.test(html) && (html.match(/fwRefused: "/g) || []).length === 2,
    "editor: a device below protocol 11: refused right after INFO, said in both languages");
  o.done();
}

/* live pushes: CHANGED, RELOAD, STEP_CHANGED; WATCH kept on by PING; SETs coalesced */
async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(50, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === 71 && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 50 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.drumStep(7));
  const s7 = E.parse[C.DRUM_STEP](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && js(E.parse[C.RELOAD](r.a)) === js({ engine: 0, preset: 0, track: 0 }) && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD (0, 0, track 0) and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.project(0, 0), { timeout: 4000, retries: 0 });
  await rq(E.req.drumStep(9, { ...E.emptyDrum(), on: 1 }));
  await sleep(10);
  ok(ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr + 1 && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: RELOAD after an editor PROJECT load too, nothing after its DRUM_STEP");
  const lv = m.sim.level(77);
  await sleep(10);
  const lc = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).find((c) => c.scope === 1);
  ok(lc && lc.id === 25 && lc.value === lv, "live: the drums' level turned on the device: CHANGED of G_DRLVL");

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 50, v - 20), { key: "0:50" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[50] === 19 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0 && E.req.watch(1)[1].join() === "1" && E.req.watch(0)[1].join() === "0", "live: no timeouts; WATCH asks for pushes only (no TRACK_CHANGED: one track)");
  done();
}

/* ------------------------------- editor 2.6: SET coalescing per control (SetGate) --- */
/* userSet as editor.html has it, on the mock: one SET per control in flight, the same value not queued again */
async function editorSetGate() {
  const C = E.CMD;
  const { rq, link, sent, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  let div = -1;
  for (let i = 0; i < info.pcount && div < 0; i++) if (E.parse[C.DESC](await rq(E.req.desc(0, i))).label === "DIV") div = i;
  const gate = new E.SetGate(), k = "0:" + div;
  const pending = (key) => link.hasQueued(key) || (!!link.cur && link.cur.key === key);
  const user = (v) => {                               /* editor.html userSet / sendSet */
    if (!gate.user(k, v, pending(k), Date.now())) return null;
    gate.get(k).sent = v;
    return rq(E.req.set(0, div, v), { key: k });
  };
  const divNow = async () => E.parse[C.DUMP](await rq(E.req.dump()), info).p[div];
  const d0 = await divNow(), want = d0 === 1 ? 2 : 1;
  await user(want);
  const n0 = sent[C.SET] || 0;
  const p = user(want);                               /* answered: the same value is sent again (a knob may have moved it) */
  if (p) await p;
  ok(div === 30 && (await divNow()) === want && sent[C.SET] === n0 + 1, "SetGate: DIV set; the same value again once answered: sent");
  gate.clear();
  ok(gate.get(k) === undefined, "SetGate: clear() forgets every control (a reload, another FM-1)");
  /* a drag: while a SET is pending, the same value is not queued again; a new one replaces the queued one */
  const a = user(5), b = user(5), c = user(6), d = user(6);
  ok(a && b === null && c && d === null, "SetGate: pending: the same value skipped, a new value sent (queued)");
  await Promise.all([a, c]);
  ok((await divNow()) === 6, "SetGate: the last value wins");
  await rq(E.req.set(0, div, d0));
  done();
}

/* ------------------------- fm1-drummer: songs (the Song and Setlist tabs) --- */
async function editorSongs() {
  const C = E.CMD;
  /* the packer: byte for byte what tools/fm1_song.py writes */
  const spec = JSON.parse(readFileSync(join(HERE, "../tests/example-song.json"), "utf8"));
  const tmp = mkdtempSync(join(tmpdir(), "sng-"));
  execFileSync("python3", [join(HERE, "../tools/fm1_song.py"), join(HERE, "../tests/example-song.json"), "--out", join(tmp, "e.bin")], { stdio: "ignore" });
  const py = new Uint8Array(readFileSync(join(tmp, "e.bin")));
  const js = E.SNG.pack(spec);
  ok(js.length === py.length && js.every((v, i) => v === py[i]), `songs: SNG.pack == tools/fm1_song.py (${js.length} bytes)`);
  const h = E.SNG.check(js);
  ok(h && h.name === "EXAMPLE" && h.bpm === 112 && h.nsec === 5 && h.sections[4].outro && h.sections[1].steps === 32 && h.bytes === js.length, "songs: SNG.check reads the header and the sections");
  const back = E.SNG.unpack(js);
  ok(JSON.stringify(E.SNG.pack(back)) === JSON.stringify(js) && back.sections[0].lanes.KICK === spec.sections[0].lanes.KICK && back.sections[1].ratchets.SNARE === spec.sections[1].ratchets.SNARE
    && back.sections[1].fill === spec.sections[1].fill, "songs: unpack -> pack gives the same bytes (lanes, ratchets, fill conditions)");
  let bad = 0;
  for (const x of [{ ...spec, bpm: 300 }, { ...spec, sections: [] }, { ...spec, sections: [{ ...spec.sections[0], steps: 65 }] },
                   { ...spec, sections: [{ ...spec.sections[0], lanes: { KICK: "x...q" } }] }, { ...spec, sections: [{ ...spec.sections[0], div: "1/3" }] }]) {
    try { E.SNG.pack(x); } catch (e) { bad++; }
  }
  ok(bad === 5 && !E.SNG.check(js.subarray(0, js.length - 1)) && !E.SNG.check(Uint8Array.from([1, 2, 3])), "songs: bad JSON refused; a truncated or alien blob is not a song");
  /* the device: send, list, read back, erase; the pedal channel */
  const { rq, done } = attachMock({});
  let rows = await E.SNG.summaries(rq);
  ok(rows.length === 16 && rows.every((r) => r === null), "songs: a new device has 16 empty slots");
  {   /* the Song tab works without an FM-1: only the pedal settings and Send are the device's; the slots are the Setlist tab's */
    const panel = (/<section class="panel" id="p-songs"[^>]*>/.exec(html) || [""])[0];
    const songs = html.slice(html.indexOf('id="p-songs"'), html.indexOf('id="p-setlist"'));
    ok(!/data-dev\b/.test(panel) && (songs.match(/ data-devonly>/g) || []).length === 3 && /\$\("songs-offline"\)\.hidden = ready/.test(html)
      && /<span data-devonly><label><span data-t="gpiSlot"><\/span> <select id="gpi-slot">/.test(html) && /<span data-devonly><label><span data-t="gpiSlot"><\/span> <select id="sed-slot">/.test(html)
      && /<section class="panel" id="p-setlist" data-tab="setlist" data-dev hidden>/.test(html) && !songs.includes('id="songslots"')
      && /tabSong: "Jam"/.test(html) && /tabSongs: "Song"/.test(html) && /tabSetlist: "Setlist"/.test(html) && /songsOffline: "No FM-1 connected/.test(html),
      "songs: the Song tab without an FM-1 (import, editor, player), the pedal and Send with one; the slots in Setlist; Jam, Song, Setlist");
    ok(/`\$\{k \+ 1\}: \$\{x\.name\}/.test(html) && /`\$\{SONGED\.sel \+ 1\}: \$\{sedSec\(\)\.name\}/.test(html) && /sp\.swing \?\? 0;/.test(html),
      "sed: the sections numbered 1..16 as on the device; a song without swing shows 0");
  }
  ok(/el\("b", \{ text: String\(i \+ 1\) \}\)/.test(html) && (html.match(/text: String\(i \+ 1\) \}\)\);/g) || []).length === 2
    && /replace\("\{n\}", slot \+ 1\)/.test(html) && /NO SONG \$\{slot \+ 1\}/.test(html) && /fmt_int\(b, \(int32_t\)slot \+ 1\)/.test(fw("sng.c")),
    "songs: the slots numbered 1..16 as the device shows them (cards, pickers, dialogs, NO SONG n)");
  ok((await E.SNG.put(rq, 2, js)) === 0, "songs: PUT into slot 2 (begin, data, commit) ok");
  rows = await E.SNG.summaries(rq);
  ok(rows[2] && rows[2].name === "EXAMPLE" && rows[2].sections.map((x) => x.name).join() === "INTRO,VERSE,CHORUS,SOLO,OUTRO" && rows[2].bytes === js.length && !rows[0] && !rows[3],
    "songs: the summary of slot 2 (name, sections) from the header and the section headers");
  const got = await E.SNG.get(rq, 2);
  ok(got && got.length === js.length && got.every((v, i) => v === js[i]), "songs: GET gives the bytes back");
  ok((await E.SNG.put(rq, 2, Uint8Array.from([9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9]))) === 2, "songs: a blob that is not a song: rc 2, the slot keeps its song");
  ok((await E.SNG.get(rq, 2)).length === js.length, "songs: ..kept");
  ok((await E.SNG.put(rq, 2, new Uint8Array(0))) === 0 && (await E.SNG.summaries(rq))[2] === null, "songs: an empty PUT erases the slot");
  ok((await E.SNG.getPedal(rq)) === 16 && (await E.SNG.setPedal(rq, 17)) === 0 && (await E.SNG.getPedal(rq)) === 17, "songs: the pedal channel read (16) and set (any)");
  {   /* the count-in (bars of clicks before a start from stopped) rides in the same object; 3 bars is refused */
    const was = await E.SNG.getPedalSet(rq);
    ok(was.ch === 17 && was.count === 0 && (await E.SNG.setPedal(rq, 16, 2)) === 0 && JSON.stringify(await E.SNG.getPedalSet(rq)) === JSON.stringify({ ch: 16, count: 2 })
      && E.SNG.arrsetRead(E.SNG.arrsetBlob(16, 2))?.count === 2 && E.SNG.arrsetRead(Uint8Array.from([0x41, 0x52, 0x53, 0x31, 16, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])) === null,
      "songs: the count-in read (0) and set (2 bars) with the channel; 3 bars is not a setting");
  }
  const L = E.parse[C.BK_LIST](await rq(E.req.bkList(), { timeout: 5000, retries: 0 }));
  ok(!L.items.some((it) => it.id >= 16 && it.id <= 24), "songs: not in BK_LIST (a SLOOP backup stays restorable)");
  done();
}

/* ------------------------------------- fm1-drummer: the player's rules (PLAY) --- */
function editorPlay() {
  const P = E.PLAY;
  const spec = { name: "T", bpm: 120, sections: [
    { name: "A", steps: 16, div: "1/16", lanes: { KICK: "x...x...x...x...", HAT: "x.x.x.x.x.x.x.x." }, fill: "............FFFF" },
    { name: "B", steps: 32, div: "1/16", lanes: { SNARE: "....x.......x.......x.......x...", CRASH: "x..............................." } },
    { name: "OUT", steps: 16, div: "1/16", outro: true, lanes: { KICK: "x..............." } }] };
  spec.sections[0].lanes.HITOM = "............x.x.";   /* FILL ONLY steps 13..16 carry the toms */
  const st = P.create(spec);
  ok(P.tick(st) === null && !st.playing, "play: stopped: no steps");
  P.request(st, 0);
  let ev = P.tick(st);
  ok(st.playing && st.sec === 0 && ev.sec === 0 && ev.step === 0 && ev.hits.map((h) => h.lane).sort().join() === "0,4" && Math.abs(ev.secs - 0.125) < 1e-9,
    "play: a section from stopped starts at once: step 0 kick + hat, a 1/16 at 120 BPM = 125 ms");
  for (let i = 1; i < 12; i++) ev = P.tick(st);          /* steps 1..11 */
  ok(!ev.fill && st.bar === 0, "play: no fill before a request");
  {   /* swing as the device (seq.c trk_grid): every second step of a straight grid starts swing x step / 200 late */
    const sp = { ...spec, swing: 50 }, w = P.create(sp);
    P.request(w, 0);
    const t = Array.from({ length: 4 }, () => P.tick(w).secs);
    ok(Math.abs(t[0] - 0.15625) < 1e-9 && Math.abs(t[1] - 0.09375) < 1e-9 && Math.abs(t[2] - 0.15625) < 1e-9,
      "play: swing 50 on 1/16 at 120 BPM: the even steps 156 ms, the odd 94 ms (the odd step a quarter of a step late)");
    const z = P.create({ ...spec, swing: 0 }); P.request(z, 0);
    ok(Math.abs(P.tick(z).secs - 0.125) < 1e-9 && Math.abs(P.tick(z).secs - 0.125) < 1e-9, "play: swing 0: straight (the default now)");
    const tr = P.create({ name: "T", bpm: 120, swing: 100, sections: [{ name: "T", steps: 12, div: "8T", lanes: { KICK: "x" } }] });
    P.request(tr, 0);
    ok(Math.abs(P.tick(tr).secs - P.tick(tr).secs) < 1e-12, "play: no swing on the triplet grids (as the device)");
    sp.swing = 0;                                            /* (edited while playing: at once) */
    ok(Math.abs(P.tick(w).secs - 0.125) < 1e-9, "play: the song's swing edited while playing applies at once");
  }
  ok(/"swing": 0,/.test(readFileSync(join(HERE, "../tests/example-song.json"), "utf8")) && /swing = Math\.round\(spec\.swing == null \? 0/.test(html)
    && /"swing": 0, "drum_level"/.test(readFileSync(join(HERE, "../tools/gp2song.py"), "utf8")),
    "songs: straight by default (swing 0: the example, the editor's packer, the importer)");
  ok(ev.pos === 11 * 12 && ev.req === -1 && !ev.hold && !ev.end && Math.floor((ev.pos % P.barU(spec.sections[0])) / 48) === 2,
    "play: each step carries what the LIVE view shows: its place (step 11 = beat 3), nothing cued, no fill, no ending");
  ok(/id="play-live"/.test(html) && /id="pl-beats"/.test(html) && /id="pl-prog"/.test(html) && /function playLive\(\)/.test(html)
    && /q\[0\]\.at <= ctx\.currentTime/.test(html), "play: the LIVE view (beats, bar, progress, what comes, tiles) follows the audio clock");
  ok(/\/\^\(\?:Digit\|Numpad\)\(\[1-8\]\)\$\/\.exec\(e\.code\)/.test(html) && /e\.key === "f"\) PLAY\.fillNext/.test(html) && /data-t="playKeys"/.test(html)
    && /playKeys: "keys: 1–8 a section \(0: switches to 9–16/.test(html) && /SPLAY\.bank = SPLAY\.bank \? 0 : 1/.test(html)
    && /\+ 8 \* \(SPLAY\.bank \|\| 0\)/.test(html) && /plive-tiles rows-\$\{Math\.max\(1, Math\.ceil\(n \/ 4\)\)\}/.test(html),
    "play: keys 1..8 by the key (shift and the keypad too; 0: sections 9..16), f the fill, the tiles in rows of four as the device");
  P.fillNext(st); P.request(st, 1);                       /* CC 21 with CC 20 (a section brings no fill of its own): the
                                                              rest of the bar is the fill */
  const fillHits = [];
  for (let i = 12; i < 16; i++) { ev = P.tick(st); fillHits.push(ev.hits.some((h) => h.lane === 10)); }
  ok(ev.fill && fillHits.join() === "true,false,true,false" && ev.newBar, "play: the fill: FILL ONLY toms play on 13 and 15, the bar ends");
  ev = P.tick(st);
  ok(st.sec === 1 && ev.sec === 1 && ev.step === 0 && !ev.fill && ev.hits.some((h) => h.lane === 11) && st.req < 0, "play: B on the bar with its crash, the fill over");
  for (let i = 1; i < 16; i++) ev = P.tick(st);
  ok(ev.newBar && st.bar === 1 && st.sec === 1, "play: B's second bar (32 steps = 2 bars)");
  {   /* a section asked for waits for the one playing to end its pattern: B (2 bars) asked for in its first bar plays
       * its second (the fill), the section lands after it; asked for in the last bar: the rest of it the fill */
    const w = P.create(spec);
    P.request(w, 1); for (let i = 0; i < 4; i++) P.tick(w);          /* B, 4 steps into its first bar */
    P.request(w, 0);
    ok(w.req === 0 && w.atEnd && !w.fill, "play: asked for in B's first bar: cued, no fill");
    let e; for (let i = 4; i < 16; i++) e = P.tick(w);
    ok(e.newBar && w.sec === 1 && w.bar === 1 && !w.fill && w.req === 0, "play: ..B's second bar, no fill of its own, the request waits");
    for (let i = 0; i < 16; i++) e = P.tick(w);
    ok(w.sec === 0 && w.step === 0 && !w.fill && !w.atEnd && w.req < 0, "play: ..the section lands at B's end");
    P.request(w, 1); for (let i = 0; i < 16; i++) P.tick(w);          /* A (1 bar) -> B on the next bar */
    for (let i = 0; i < 20; i++) P.tick(w);                          /* 4 steps into B's second bar */
    P.request(w, 2);
    ok(w.sec === 1 && w.bar === 1 && !w.fill && w.atEnd, "play: asked for in B's last bar: cued, no fill");
    for (let i = 20; i < 32; i++) P.tick(w);
    ok(w.sec === 2 && w.bar === 0, "play: ..the section on the bar");
    P.request(w, 1); for (let i = 0; i < 16; i++) P.tick(w);          /* OUT (1 bar) -> B */
    for (let i = 0; i < 4; i++) P.tick(w);
    P.ending(w, null);                                               /* the outro after B's end */
    ok(w.req === 2 && w.atEnd && !w.fill && w.endReq === 1, "play: the ending waits for B's end too");
    for (let i = 4; i < 32; i++) P.tick(w);
    ok(w.sec === 2 && w.endBars === 1, "play: ..the outro after it");
    const v = P.create({ name: "N", bpm: 100, sections: [spec.sections[1]] });
    P.request(v, 0); for (let i = 0; i < 4; i++) P.tick(v);
    P.ending(v, null);
    ok(v.endBars === 2 && v.atEnd && !v.fill && v.req < 0, "play: an ending with no outro: B's end, no fill");
    for (let i = 4; i < 16; i++) P.tick(v);
    ok(v.playing && !v.fill, "play: ..the last bar");
    for (let i = 0; i < 16; i++) P.tick(v);
    ok(!v.playing, "play: ..then the stop");
  }
  P.request(st, 0, true);                                  /* CC 27: on the next bar, without waiting for B's end, no fill */
  ok(!st.atEnd && !st.fill && st.req === 0, "play: CC 27: cued for the next bar, no fill");
  for (let i = 0; i < 16; i++) ev = P.tick(st);            /* the rest of bar 2 */
  ok(st.sec === 0 && !st.fill && st.req < 0, "play: ..the section on the next bar");
  for (let i = 0; i < 16; i++) ev = P.tick(st);            /* (a bar of A) */
  P.fillNext(st);                                          /* CC 21 in A (1 bar): the fill now, and through the next bar */
  ok(st.fill && st.fillHold === 2, "play: the fill (CC 21) from now");
  for (let i = 0; i < 16; i++) ev = P.tick(st);
  ok(st.fill && st.fillHold === 1, "play: ..and the next bar (the next cycle of a 1-bar section)");
  for (let i = 0; i < 16; i++) ev = P.tick(st);
  ok(!st.fill && !st.fillHold && st.sec === 0, "play: ..over after it");
  {   /* in B (2 bars): asked for in the first bar, the fill runs to the pattern's end; in the last bar, the next cycle too */
    const h = P.create(spec);
    P.request(h, 1); for (let i = 0; i < 4; i++) P.tick(h);
    P.fillNext(h);
    ok(h.fill && h.fillHold === 1, "play: fill in B's first bar: until B's end");
    for (let i = 4; i < 16; i++) P.tick(h);
    ok(h.fill && h.bar === 1, "play: ..B's second bar still the fill");
    for (let i = 0; i < 16; i++) P.tick(h);
    ok(!h.fill && h.sec === 1 && h.bar === 2, "play: ..off at the pattern's end, B goes on");
    for (let i = 0; i < 20; i++) P.tick(h);                           /* into B's last bar */
    P.fillNext(h);
    ok(h.fill && h.fillHold === 2, "play: fill in B's last bar: through the next cycle");
    for (let i = 20; i < 32; i++) P.tick(h);
    for (let i = 0; i < 32; i++) P.tick(h);
    ok(!h.fill && h.sec === 1, "play: ..off after the next cycle");
  }
  P.ending(st, null);                                      /* the flagged outro */
  ok(st.req === 2 && st.endReq === 1 && !st.fill, "play: the ending takes the outro, no fill");
  for (let i = 0; i < 16; i++) ev = P.tick(st);
  ok(st.sec === 2 && st.endBars === 1 && st.playing, "play: the outro plays its bar");
  for (let i = 0; i < 16; i++) ev = P.tick(st);
  ok(!st.playing && st.stopped && P.tick(st) === null, "play: ..then the stop on the bar");
  {   /* the count-in (the device's setting mirrored): clicks first, the section after them; stop cancels; playing: unchanged */
    const c1 = P.create(spec, 1);
    P.request(c1, 1);
    const evs = [];
    for (let i = 0; i < 4; i++) evs.push(P.tick(c1));
    ok(c1.playing && evs.every((e) => e.hits.length === 0 && e.sec < 0) && evs.map((e) => e.click).join() === "2,1,1,1" && evs.map((e) => e.count).join() === "4,3,2,1"
      && Math.abs(evs[0].secs - 0.5) < 1e-9 && evs[3].newBar && !evs[2].newBar && c1.sec === 1 && c1.counting === 0,
      "play: count-in of a bar: four clicks a beat, the first accented, counted 4..1, nothing else");
    const e5 = P.tick(c1);
    ok(e5.sec === 1 && e5.step === 0 && e5.hits.some((h) => h.lane === 11) && c1.req < 0, "play: ..then the section from its first step (B with its crash)");
    const c2 = P.create(spec, 2);
    P.request(c2, 0);
    { const e0 = P.tick(c2); ok(e0.ci === 1 && e0.ciN === 8 && e0.ciNum === 4 && e0.req === 0, "play: the count-in's steps carry COUNT 1 / 8 for the view"); }
    P.request(c2, 2);
    ok(c2.playing && c2.counting === 7 && c2.req === 2 && c2.sec < 0, "play: two bars; a section asked for during the count only changes what starts");
    P.stop(c2);
    ok(!c2.playing && c2.counting === 0 && P.tick(c2) === null, "play: stop during the count-in cancels it");
    const c3 = P.create(spec, 1);
    P.request(c3, 0);
    for (let i = 0; i < 5; i++) P.tick(c3);
    P.request(c3, 1);
    ok(c3.sec === 0 && !c3.fill && c3.req === 1 && !c3.counting, "play: playing, a section lands on the bar as before (no count-in)");
    ok(P.create(spec).countIn === 0 && (P.request(P.create(spec), 0), true), "play: without the setting nothing changes");
  }
  {   /* the section playing asked for again: nothing, not a restart (a fill before 2026-10-14); with an ending pending it
       * cancels the ending as any section; next / previous step through the sections, wrapping, the first / last from
       * stopped */
    const a = P.create(spec);
    P.request(a, 0);
    for (let i = 0; i < 5; i++) P.tick(a);
    P.request(a, 0);
    ok(a.sec === 0 && a.req < 0 && !a.fill && !a.fillHold, "play: the section playing pressed again: nothing, nothing cued");
    for (let i = 5; i < 16; i++) P.tick(a);
    const e1 = P.tick(a);
    ok(e1.sec === 0 && !e1.fill && e1.bar === 1 && e1.step === 0, "play: ..the section goes on (bar 2, not a restart)");
    P.request(a, 2); P.request(a, 0);
    ok(a.req === 0 && !a.fill, "play: with another section cued it is a request as any other");
    for (let i = 1; i < 16; i++) P.tick(a);
    P.ending(a, 2);
    P.request(a, 0);
    ok(a.req === 0 && a.endReq === 0 && !a.fillHold, "play: with an ending pending the same section cancels the ending instead");
    const b = P.create(spec);
    P.step(b, 1);
    ok(b.playing && b.sec === 0, "play: next from stopped starts with the first section");
    P.step(b, 1); P.step(b, 1);
    ok(b.req === 2 && !b.fill, "play: next twice in a bar: the last one counts (2), no fill");
    for (let i = 0; i < 16; i++) P.tick(b);
    P.step(b, 1);
    ok(b.sec === 2 && b.req === 0, "play: next wraps to the first");
    for (let i = 0; i < 16; i++) P.tick(b);
    P.step(b, -1);
    ok(b.sec === 0 && b.req === 2, "play: previous wraps to the last");
    const c = P.create(spec);
    P.step(c, -1);
    ok(c.sec === 2, "play: previous from stopped starts with the last section");
    const d = P.create({ name: "ONE", bpm: 120, sections: [spec.sections[0]] });
    P.step(d, 1); for (let i = 0; i < 3; i++) P.tick(d); P.step(d, 1);
    ok(d.sec === 0 && d.req < 0 && !d.fill && !d.fillHold, "play: next with a single section lands on it: nothing");
  }
  {   /* the player's default kit: web/kit/LANE_note.wav as the page fetches them (PLAY_NOTE in editor.html) */
    const notes = JSON.parse((/const PLAY_NOTE = (\{[^}]*\})/.exec(html) || [])[1].replace(/(\w+):/g, '"$1":'));
    const have = E.SNG.LANES.filter((l) => existsSync(join(HERE, "kit", `${l}_${notes[l]}.wav`)));
    ok(have.length === 16 && existsSync(join(HERE, "kit", "ATTRIBUTION.txt")), `play: the kit served next to the editor: ${have.length} lanes (Muldjord), with its attribution`);
    ok(/kitPreload\(\)/.test(html) && /kitPreloaded:/.test(html), "kits: the Drum kit page preloads the served kit onto empty pads");
    ok(readFileSync(join(HERE, "gp2song.py"), "utf8") === readFileSync(join(HERE, "../tools/gp2song.py"), "utf8"), "gpi: web/gp2song.py is tools/gp2song.py (the page fetches it next to the editor)");
  }
  {   /* the ending while the flagged outro plays already (nothing cued): it plays to its end, then the stop (pedal.c) */
    const o = P.create(spec), k = P.outro(spec);
    P.request(o, k); for (let i = 0; i < 4; i++) P.tick(o);
    P.ending(o, null);
    ok(o.req < 0 && o.atEnd && o.endBars === P.bars(spec, k) && o.playing, "play: the ending while its outro plays: to the outro's end");
    for (let i = 4; i < 16 * P.bars(spec, k) + 1; i++) P.tick(o);
    ok(!o.playing, "play: ..then the stop");
  }
  const st2 = P.create({ name: "N", bpm: 100, sections: [spec.sections[0]] });
  P.request(st2, 0); P.tick(st2); P.ending(st2, null);
  ok(st2.endBars === 1 && !st2.fill && st2.req < 0, "play: an ending with no outro: stop on the next bar, no fill");
  for (let i = 0; i < 16; i++) P.tick(st2);
  ok(!st2.playing, "play: ..stopped");
}

/* ------------------------------------- fm1-drummer: the section editor's operations (SED) --- */
function editorSed() {
  const S = E.SED;
  {   /* a section is whole bars of its time (the FM-1 counts bars): one that is not is flagged, with the signature that
       * fits (tools/fm1_song.py bar_fit, the same rule) */
    const intro = { ...S.blank("INTRO", 4) }, fit = S.barFit(intro);
    ok(fit && fit.bars === 0.25 && fit.sig === "1/4" && !S.barFit(S.setTime(intro, fit.sig)), "sed: 4 steps of 1/16 in 4/4 flagged, \"1/4\" fits (then no flag)");
    ok(!S.barFit(S.blank("VERSE", 32)) && S.barFit(S.blank("V", 20)).sig === "5/4" && !S.barFit(S.setTime(S.blank("S", 14), "7/8")),
      "sed: whole bars not flagged; 20 steps: \"5/4\"; 14 sixteenths of 7/8 whole");
    ok(/id="sed-barwarn"/.test(html) && /id="sed-barfix"/.test(html) && /sedBarFix: "Set time to \{sig\}"/.test(html) && /sedBarFix: "拍子を \{sig\} に"/.test(html),
      "sed: the warning and its fix button in the page, English and Japanese");
  }
  let sec = S.blank("VERSE", 32);
  ok(sec.steps === 32 && sec.div === "1/16" && Object.keys(sec.lanes).length === 0 && !sec.fill, "sed: a blank section");
  sec = S.setHit(sec, "KICK", 0, "x"); sec = S.setHit(sec, "KICK", 8, "X"); sec = S.setHit(sec, "SNARE", 4, "x", 2); sec = S.setHit(sec, "HAT", 2, "g");
  ok(sec.lanes.KICK === "x.......X......................." && sec.lanes.SNARE[4] === "x" && sec.ratchets.SNARE[4] === "2" && sec.lanes.HAT[2] === "g", "sed: hits with levels and a ratchet");
  ok(S.cell(sec, "SNARE", 4).rat === 2 && S.cell(sec, "KICK", 8).hit === "X" && S.cell(sec, "KICK", 1).hit === "", "sed: cells read back");
  sec = S.setHit(sec, "SNARE", 4, "");
  ok(!("SNARE" in sec.lanes) && !sec.ratchets, "sed: a cleared lane and its ratchet go away");
  sec = S.setCond(sec, 28, "F"); sec = S.setCond(sec, 29, "N");
  ok(sec.fill.length === 32 && sec.fill[28] === "F" && sec.fill[29] === "N", "sed: fill conditions");
  const parts = S.split(sec, 1);
  ok(parts && parts[0].steps === 16 && parts[1].steps === 16 && parts[0].lanes.KICK === "x.......X......." && !("KICK" in parts[1].lanes) && parts[1].fill[12] === "F" && parts[1].name === "VERSE 2", "sed: split at bar 1");
  ok(S.split(sec, 2) === null && S.split(sec, 0) === null, "sed: no split outside the pattern");
  const back = S.merge(parts[0], parts[1]);
  ok(back.steps === 32 && back.lanes.KICK === sec.lanes.KICK && back.fill === sec.fill, "sed: merge gives the section back");
  ok(S.merge(parts[0], S.setDiv(parts[1], "1/8")) === null && S.merge(S.blank("A", 40), S.blank("B", 40)) === null, "sed: no merge across divisions or over 64 steps");
  ok(S.setSteps(sec, 16).lanes.KICK === "x.......X......." && S.setSteps(sec, 48).lanes.KICK.length === 48, "sed: steps cut and pad the strings");
  const bars = [{ lanes: { KICK: "x...x...x...x...", HAT: "x.x.x.x.x.x.x.x." } }, { lanes: { KICK: "x...x...x.x.x...", SNARE: "....x.......x..." }, ratchets: { SNARE: "............2..." } }];
  const fb = S.fromBars(bars, "CHORUS");
  ok(fb.steps === 32 && fb.lanes.KICK === "x...x...x...x...x...x...x.x.x..." && fb.lanes.SNARE.slice(16) === "....x.......x..." && fb.ratchets.SNARE[28] === "2", "sed: a pattern from two bars of the file");
  const fill = { lanes: { HITOM: "............x.x.", LOWTOM: ".............x.x", KICK: "x...x...x...x..." } };
  const wf = S.withFill(fb, fill);
  ok(!wf.sec.fill && wf.sec.lanes.HITOM.slice(28) === "x.x." && wf.sec.fills.HITOM.slice(28) === "F.F." && wf.sec.lanes.LOWTOM.slice(29) === "x.x" && wf.sec.fills.LOWTOM.slice(29) === "F.F"
    && wf.sec.fills.KICK.slice(16) === "..........N....." && wf.sec.fills.SNARE.slice(16) === "....N.......N..." && wf.dropped.length === 0,
    "sed: the fill bar, lane by lane: the toms only in a fill, the pattern's lone kick not in one, nothing dropped");
  {   /* the undo history: a state marked after each change (a redraw is not a change), undo and redo, redo dropped by a new change, a cap */
    const h = S.hist();
    const a = { name: "H", bpm: 120, sections: [S.blank("A")] };
    ok(S.mark(h, a) && !S.mark(h, a) && h.undo.length === 0 && S.undo(h) === null, "sed: history: the first state marked, marked again: no change, nothing to undo");
    const b = { ...a, sections: [S.setHit(a.sections[0], "KICK", 0, "x")] };
    ok(S.mark(h, b) && h.undo.length === 1, "sed: history: a change marked");
    const u = S.undo(h);
    ok(u && !("KICK" in u.sections[0].lanes) && h.redo.length === 1 && S.undo(h) === null, "sed: history: undo gives the state before (only one)");
    const r = S.redo(h);
    ok(r && r.sections[0].lanes.KICK[0] === "x" && h.redo.length === 0 && S.redo(h) === null, "sed: history: redo gives it back");
    S.undo(h);
    const c = { ...a, sections: [S.setHit(a.sections[0], "SNARE", 4, "X")] };
    ok(S.mark(h, c) && h.redo.length === 0 && h.undo.length === 1, "sed: history: a change after an undo drops the redo");
    h.max = 3;
    for (let i = 1; i <= 5; i++) S.mark(h, { ...a, name: "H" + i });
    ok(h.undo.length === 3 && S.undo(h).name === "H4" && S.undo(h).name === "H3" && S.undo(h).name === "H2" && S.undo(h) === null, "sed: history: capped at max states");
  }
  {   /* the marks: set with a hit, changed on a hit, kept through split / merge / copy, packed as flag 2 and unpacked */
    let m = S.setHit(S.blank("M", 16), "HITOM", 12, "x", 1, "F");
    m = S.setHit(m, "HAT", 12, "o", 1, "N");
    m = S.setHit(m, "KICK", 0, "x");
    ok(S.cell(m, "HITOM", 12).mark === "F" && S.cell(m, "HAT", 12).mark === "N" && S.cell(m, "KICK", 0).mark === "" && m.fills.HITOM === "............F...", "sed: a hit set with its mark");
    m = S.setMark(m, "KICK", 0, "N");
    ok(m.fills.KICK === "N..............." && S.setMark(m, "KICK", 0, "").fills.KICK === undefined && S.setMark(m, "SNARE", 3, "F").fills.SNARE === undefined, "sed: a mark changed on a hit, none on a rest");
    const two = S.merge(m, m), sp = S.split(two, 1);
    ok(two.fills.HITOM === "............F...............F..." && sp[1].fills.HAT === "............N...", "sed: marks through merge and split");
    ok(S.pasteCols(S.blank("P", 16), 4, S.copyCols(m, 12, 12)).fills.HITOM === "....F...........", "sed: marks through copy and paste");
    const packed = E.SNG.pack({ name: "M", bpm: 100, sections: [m] }), h = E.SNG.check(packed);
    ok(h && packed.length === 32 + 18 + 4 + 32 + 3 && E.SNG.unpack(packed).sections[0].fills.HITOM === "............F..." && E.SNG.unpack(packed).sections[0].fills.HAT === "............N..."
      && E.SNG.unpack(packed).sections[0].fills.KICK === "N...............",
      "sed: packed in the hit's byte (2 bytes a step, a byte a hit), unpacked to the same marks");
    const st = E.PLAY.create({ name: "M", bpm: 120, sections: [m] });
    E.PLAY.request(st, 0);
    const steps = []; for (let i = 0; i < 16; i++) steps.push(E.PLAY.tick(st));
    ok(steps[12].hits.map((x) => x.lane).sort().join() === "4" && steps[0].hits.length === 1, "play: outside a fill the tom (F) is out, the hat (N) and the kick (N) in");
    E.PLAY.fillNext(st);                                   /* (a 1-bar section: the fill from now, and the next bar too) */
    const f = []; for (let i = 0; i < 16; i++) f.push(E.PLAY.tick(st));
    ok(f[12].fill && f[12].hits.map((x) => x.lane).sort().join() === "10", "play: in a fill the tom in, the hat out");
  }
  ok(S.outro(fb, true).outro === true && !("outro" in S.outro(fb, false)) && S.rename(fb, "end part long name").name === "END PART LON", "sed: outro and names");
  ok(S.hits(fb) === 19 && E.SNG.pack({ name: "T", bpm: 100, sections: [wf.sec, parts[0]] }).length > 32, "sed: hit count; the result packs");
  {   /* columns: copy steps 1..4 of the first bar, paste at 17 (the second bar), clear 1..4 */
    const clip = S.copyCols(fb, 0, 3);
    ok(clip.n === 4 && clip.lanes.KICK === "x..." && clip.lanes.HAT === "x.x." && !("SNARE" in clip.lanes) === false && clip.lanes.SNARE === "....", "sed: copy of four steps");
    const p2 = S.pasteCols(fb, 16, clip);
    ok(p2.lanes.HAT.slice(16, 20) === "x.x." && p2.lanes.HAT.slice(20) === fb.lanes.HAT.slice(20) && p2.lanes.KICK.slice(16, 20) === "x..." && p2.lanes.KICK.slice(0, 16) === fb.lanes.KICK.slice(0, 16), "sed: paste writes the four steps and nothing else");
    const p3 = S.pasteCols(fb, 30, clip);
    ok(p3.lanes.KICK.length === 32 && p3.lanes.KICK.slice(30) === "x." && S.pasteCols(fb, 32, clip) === fb, "sed: a paste past the end is cut; one at the end does nothing");
    const cl = S.clearCols(fb, 0, 3);
    ok(cl.lanes.KICK.slice(0, 4) === "...." && cl.lanes.KICK.slice(4) === fb.lanes.KICK.slice(4) && !("HAT" in cl.lanes) === false, "sed: clear empties the steps");
    const cf = S.copyCols(S.setCond(fb, 2, "F"), 0, 3);
    ok(cf.fill === "..F." && S.pasteCols(fb, 8, cf).fill[10] === "F", "sed: the fill conditions travel with the steps");
  }
}

/* ------------------------------------- fm1-drummer: SNG2, the meter, the real pedal --- */
function editorMeter() {
  const S = E.SNG, P = E.PLAY, D = E.SED;
  /* the compact form: a full song of 8 sections x 64 steps of three hits fits, the old form did not (10 bytes a step) */
  const dense = { name: "SECTION", steps: 64, div: "1/16", lanes: { KICK: "x...".repeat(16), HAT: "x".repeat(64), SNARE: "..x.".repeat(16) } };
  const full = S.pack({ name: "FULL", bpm: 120, sections: Array.from({ length: 8 }, (_, k) => ({ ...dense, name: "S" + k })) });
  ok(full.length === 32 + 8 * (18 + 16 + 128 + 96) && S.check(full).nsec === 8, `sng2: 8 sections of 64 steps (3 hits a step) fit: ${full.length} bytes`);
  /* 16 sections (8 until 2026-10-14): sixteen of 16 steps fit; 17 refused; sixteen dense 64-step ones overflow the sector */
  const short = { name: "S", steps: 16, div: "1/16", lanes: { KICK: "x...x...x...x...", SNARE: "....x.......x...", HAT: "x.x.x.x.x.x.x.x." } };
  const sixteen = S.pack({ name: "SIXTEEN", bpm: 120, sections: Array.from({ length: 16 }, (_, k) => ({ ...short, name: "S" + k })) });
  let over = 0;
  try { S.pack({ name: "X", bpm: 120, sections: Array.from({ length: 17 }, (_, k) => ({ ...short, name: "S" + k })) }); } catch (e) { over++; }
  try { S.pack({ name: "X", bpm: 120, sections: Array.from({ length: 16 }, (_, k) => ({ ...dense, name: "S" + k })) }); } catch (e) { over++; }
  const seqc = readFileSync(join(HERE, "../firmware/src/seq.c"), "utf8");
  ok(S.SECTIONS === 16 && +(/#define SNG_SECTIONS (\d+)u/.exec(seqc) || [])[1] === S.SECTIONS && S.check(sixteen).nsec === 16 && sixteen.length <= S.MAX && over === 2,
    `sng2: 16 sections (== seq.c SNG_SECTIONS): sixteen of 16 steps fit (${sixteen.length} bytes); 17, or sixteen dense 64-step ones, refused`);
  /* the meter: "7/8" -> the byte 7 | 1 << 5, back; 4/4 is no byte; bad ones refused */
  ok(S.meterByte("7/8") === (7 | 32) && S.meterByte("4/4") === 0 && S.meterByte(null) === 0 && S.meterByte("3/4") === 3 && S.meterByte("12/16") === (12 | 96)
    && S.meterOf(7 | 32) === "7/8" && S.meterOf(0) === null, "meter: n/d <-> the section header's byte");
  let bad = 0;
  for (const t of ["7", "0/4", "17/8", "5/3", "x/y"]) { try { S.meter(t); } catch (e) { bad++; } }
  ok(bad === 5, "meter: 7, 0/4, 17/8, 5/3, x/y refused");
  const seven = { name: "SEVEN", steps: 14, div: "1/16", time: "7/8", lanes: { KICK: "x.....x.......", SNARE: "....x.....x..." } };
  const four = { name: "FOUR", steps: 16, div: "1/16", lanes: { KICK: "x.......x......." } };
  const spec = { name: "ODD", bpm: 120, sections: [seven, four] };
  const b = S.pack(spec), back = S.unpack(b);
  ok(b[32 + 15] === (7 | 32) && back.sections[0].time === "7/8" && !("time" in back.sections[1]) && S.check(b).sections[0].time === "7/8",
    "meter: packed and unpacked with its section");
  const tmp = mkdtempSync(join(tmpdir(), "odd-"));
  writeFileSync(join(tmp, "odd.json"), JSON.stringify(spec));
  execFileSync(PYTHON, [join(HERE, "../tools/fm1_song.py"), join(tmp, "odd.json"), "--out", join(tmp, "odd.bin")], { stdio: "ignore" });
  ok(eq([...new Uint8Array(readFileSync(join(tmp, "odd.bin")))], [...b]), "meter: SNG.pack == tools/fm1_song.py with a 7/8 section");
  /* the player: a 7/8 bar is 14 sixteenths; the section after it lands on the 7/8 bar line; the count-in clicks
     seven eighths (the first accented); a triplet division in 7/8 counts its bars by time, not steps */
  ok(P.bars(spec, 0) === 1 && P.bars({ sections: [{ ...seven, steps: 28 }] }, 0) === 2 && P.bars({ sections: [{ ...seven, steps: 16 }] }, 0) === 2, "play: 7/8 bars: 14 steps one, 28 two, 16 two");
  const st = P.create(spec);
  P.request(st, 0);
  let e;
  for (let i = 0; i < 4; i++) e = P.tick(st);
  P.request(st, 1);
  ok(!st.fill && st.atEnd, "play 7/8: asked for in the (1-bar) pattern's bar: cued, no fill");
  for (let i = 4; i < 14; i++) e = P.tick(st);
  ok(e.newBar && st.sec === 1 && st.step === 0, "play 7/8: ..the 4/4 section lands after 14 sixteenths");
  const c = P.create(spec, 1);
  P.request(c, 0);
  const clicks = [];
  for (let i = 0; i < 7; i++) clicks.push(P.tick(c));
  ok(clicks.map((x) => x.click).join() === "2,1,1,1,1,1,1" && Math.abs(clicks[0].secs - 0.25) < 1e-9 && clicks[6].newBar && c.sec === 0, "play 7/8: the count-in: seven eighths, the first accented");
  const tr = P.create({ name: "T", bpm: 120, sections: [{ name: "TRIP", steps: 21, div: "8T", time: "7/8", lanes: { KICK: "x".repeat(21) } }] });
  P.request(tr, 0);
  const nb = []; for (let i = 0; i < 21; i++) nb.push(P.tick(tr).newBar ? i : -1);
  ok(nb.filter((x) => x >= 0).join() === "10,20" && tr.bar === 2, "play 7/8 in triplet eighths: the bar lines inside steps 11 and 21 (10.5 steps a bar)");
  /* the section editor: its meter kept, steps a bar, bar lines, a split on a 7/8 bar, fromBars with 14-step bars */
  ok(D.spb(seven) === 14 && D.beat(seven) === 2 && D.spb(four) === 16 && D.beat(four) === 4 && D.norm(seven).time === "7/8" && !("time" in D.norm({ ...seven, time: "4/4" })),
    "sed: 7/8: 14 steps a bar, a beat of 2; 4/4: 16 and 4; the meter kept, 4/4 left out");
  const two = D.norm({ ...seven, steps: 28, lanes: { KICK: "x.....x.......".repeat(2) } }), parts = D.split(two, 1);
  ok(parts && parts[0].steps === 14 && parts[1].steps === 14 && parts[1].time === "7/8", "sed: a 7/8 section split at its bar: 14 + 14");
  let refused = false;
  try { D.setTime(four, "9/7"); } catch (x) { refused = true; }
  ok(refused && D.setTime(four, "3/4").time === "3/4" && D.bars(D.setTime(four, "3/4")) === 2, "sed: setTime: a bad meter refused; 16 steps in 3/4 are two bars");
  const fb = D.fromBars([{ steps: 14, time: "7/8", lanes: { KICK: "x.....x......." } }, { steps: 14, time: "7/8", lanes: { SNARE: "....x.....x..." } }], "IMP");
  ok(fb.steps === 28 && fb.time === "7/8" && fb.lanes.KICK === "x.....x......." + ".".repeat(14) && fb.lanes.SNARE.slice(14) === "....x.....x...", "sed: fromBars with the file's 7/8 bars");
  /* PC while playing (PLAY.load): the pattern playing goes on, the new song's sections from the next asked for, its tempo with it */
  const other = { name: "OTHER", bpm: 90, sections: [four, seven] };
  const g = P.create(spec);
  P.request(g, 0);
  for (let i = 0; i < 3; i++) P.tick(g);
  P.load(g, other);
  e = P.tick(g);
  ok(g.spec === other && g.cur === seven && e.hits.length === 0 && g.bpm === 120 && P.tick(g).hits.some((h) => h.lane === 2), "load: playing, the old pattern goes on (its snare at step 5) at its tempo");
  P.request(g, 1);
  ok(g.req === 1 && g.atEnd, "load: ..the section asked for is the new song's (no fill for 'the same index again')");
  for (let i = 5; i < 14; i++) P.tick(g);
  ok(g.sec === 1 && !g.cur && g.bpm === 90 && P.playing(g) === seven, "load: ..it lands at the old pattern's end, with the new tempo");
  const q = P.create(spec, 1);
  P.load(q, other);
  ok(q.spec === other && q.countIn === 1 && !q.playing && q.bpm === 90, "load: stopped: the song cued, the count-in setting kept");
}

async function editorPedalIn() {
  const R = E.PEDALIN;
  /* the router: CC 20..27 and PC on the pedal's channel (16 here; 17 any; 0 off), as pedal.c reads the jack */
  const cc = (n, v, ch = 16) => [0xB0 | (ch - 1), n, v];
  ok(JSON.stringify(R.route(cc(20, 2), 16)) === JSON.stringify({ kind: "section", k: 2, now: false }) && R.route(cc(27, 1), 16).now === true
    && R.route(cc(21, 127), 16).kind === "fill" && R.route(cc(22, 3), 16).k === 3 && R.route(cc(23, 0), 16).kind === "stop"
    && R.route(cc(24, 0), 16).dir === 1 && R.route(cc(25, 0), 16).dir === -1 && R.route(cc(26, 0), 16).kind === "tap",
    "pedal in: CC 20..27 -> section, fill bar, fill, ending, stop, next, previous, tap");
  ok(R.route([0xCF, 3], 16).slot === 3 && R.route([0xCF, 15], 16).slot === 15 && R.route([0xCF, 16], 16).slot === -1 && R.route([0xCF, 127], 16).slot === -1,
    "pedal in: PC n -> song n (0..15); PC 16.. -> no song (SLOOP's A..D)");
  ok(R.route(cc(20, 1, 1), 16) === null && R.route(cc(20, 1, 1), 17).k === 1 && R.route(cc(20, 1), 0) === null && R.route(cc(64, 127), 16) === null
    && R.route([0x9F, 36, 100], 16) === null, "pedal in: another channel ignored (any: taken; off: nothing); CC 64 and notes are not the pedal's");
  const spec = { name: "S", bpm: 120, sections: [{ name: "VERSE", steps: 16, div: "1/16", lanes: {} }, { name: "OUT", steps: 16, div: "1/16", outro: true, lanes: {} }] };
  ok(R.say(R.route(cc(20, 0), 16), spec) === "NEXT: VERSE" && R.say(R.route(cc(27, 1), 16), spec) === "NOW: OUT" && R.say(R.route(cc(22, 127), 16), spec) === "ENDING: OUT"
    && R.say(R.route([0xCF, 2], 16), null) === "SONG 2" && R.say(R.route(cc(23, 0), 16), spec) === "STOP", "pedal in: said in the FM-1's words");
  const taps = [];
  ok(R.tap(taps, 0) === null && R.tap(taps, 500) === 120 && R.tap(taps, 1000) === 120 && R.tap(taps, 1600) === 113 && R.tap(taps, 5000) === null && R.tap(taps, 5250) === 240,
    "pedal in: tap tempo: 500 ms -> 120; the last three intervals averaged; a pause starts over; 240 at most");
  /* the page: the router feeds the player as the Play card's buttons do */
  const page = html.slice(html.indexOf("function pinMsg("), html.indexOf('$("pin-listen").addEventListener'));
  ok(/PLAY\.request\(st, a\.k, a\.now\)/.test(page) && /PLAY\.fillNext\(st\)/.test(page) && /PLAY\.ending\(st, a\.k\)/.test(page) && /playStop\(\)/.test(page)
    && /PLAY\.step\(st, a\.dir\)/.test(page) && /PLAY\.load\(SPLAY\.st, spec\)/.test(html), "pedal in: the page applies each action to the player (request, fill, ending, stop, step, load)");

  /* MIDI clock in (PCLK, the device's seq.c mclk_event): the tempo every 24 pulses, START / CONTINUE / STOP, the downbeat */
  const C = E.PCLK, c = C.create();
  ok(C.msg(c, 0xFA, 0).kind === "start" && C.msg(c, 0xFB, 0).kind === "continue" && C.msg(c, 0xFC, 0).kind === "stop" && C.msg(c, 0xFE, 0) === null,
    "clock in: FA start, FB continue, FC stop; active sensing ignored");
  C.msg(c, 0xFA, 0);
  let p0 = C.msg(c, 0xF8, 0), last = p0, bpms = [];
  for (let i = 1; i <= 48; i++) { last = C.msg(c, 0xF8, i * 20.833); if (last.bpm) bpms.push(last.bpm); }   /* 120 BPM: a pulse every 20.8 ms */
  ok(p0.first && p0.pulses === 0 && p0.beat && last.pulses === 48 && last.beat && js(bpms) === js([120, 120]) && C.on(c, 48 * 20.833 + 400) && !C.on(c, 48 * 20.833 + 600),
    "clock in: the first pulse after START is the downbeat; 120 BPM read every beat; on while pulses come (0.5 s)");
  const s2 = C.create(); let b2 = [];
  for (let i = 0; i <= 48; i++) { const a = C.msg(s2, 0xF8, i * 5); if (a.bpm) b2.push(a.bpm); }
  const s3 = C.create(); let b3 = [];
  for (let i = 0; i <= 24; i++) { const a = C.msg(s3, 0xF8, 1000 + i * 100); if (a.bpm) b3.push(a.bpm); }
  const s4 = C.create(); let e4 = 0;
  for (let i = 0; i <= 8; i++) { const a = C.msg(s4, 0xF8, i * 20.833); if (a.est) e4 = a.est; }
  ok(e4 === 120, "clock in: the tempo from the pulses' interval before a whole beat has come");
  ok(b2.length === 0 && b3.length === 0, "clock in: a beat under 250 ms or over 1500 ms gives no tempo (40..240 BPM, as the device)");
  /* the player's time of a beat: between its steps (swung: the beat still on its step), or after them at the tempo */
  const P = E.PLAY, g = P.create({ name: "S", bpm: 120, swing: 50, sections: [{ name: "A", steps: 16, div: "1/16", lanes: { KICK: "x..............." } }] });
  P.request(g, 0);
  const marks = []; let at = 0, u = 0;
  for (let i = 0; i < 8; i++) { const ev = P.tick(g); marks.push({ at, u }); at += ev.secs; u += ev.u; }
  ok(u === 96 && Math.abs(C.timeOf(marks, 48, u, at, 120) - 0.5) < 1e-9 && Math.abs(C.timeOf(marks, 96, u, at, 120) - 1) < 1e-9
    && Math.abs(C.timeOf(marks, 144, u, at, 120) - 1.5) < 1e-9 && C.timeOf(marks.slice(4), 0, u, at, 120) === null,
    "clock in: the player's steps in units (swing kept inside the beat): beat 1 at 0.5 s, 2 at 1 s, 3 after them at the tempo");
  ok(C.nudge(0.01) === 0.005 && C.nudge(0.04) === 0.02 && C.nudge(-0.04) === -0.02 && C.nudge(0.3) === 0.3, "clock in: the steps to come move half the error, 20 ms at most a beat; over 50 ms all of it");
  const clk = html.slice(html.indexOf("function pinClock("), html.indexOf("function pinMsg("));
  ok(/if \(PIN\.clock\) pinClock\(data\[0\], stamp\)/.test(html) && /pinMsg\(e\.data, e\.timeStamp/.test(html) && /st\.counting = 0; PLAY\.bar\(st\)/.test(clk)
    && /playStart\(at\)/.test(clk) && /pinClock: "Follow its MIDI clock"/.test(html) && /id="pin-clock"/.test(html),
    "clock in: the page: realtime bytes to pinClock when the box is ticked, the start on the downbeat, no count-in");
}


/* ------------------------------------- editor v6: backup / restore --- */
async function editorBackup() {
  const C = E.CMD;
  const { m, rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const ec = fw("editor.c");
  ok(/ED_BK_IDS\[\] = \{0, 1, 2, 3, 4, 5, 9, 10, 32, 33, 34, 35\}/.test(ec) && /if \(id >= 32u && SNG_SMP_TAKEN\(id - 32u\)\) \{/.test(ec)
    && js(E.BK.RESTORE.concat(E.BK.SAMPLES).sort((a, b) => a - b)) === js([0, 1, 2, 3, 4, 5, 9, 10, 34, 35]) && js(E.BK.SKIP) === js([6, 7, 8, 32, 33]),
    "backup: the object ids == editor.c ED_BK_IDS (no 6..8: drums only; USR1 and USR2 listed empty: the songs)");
  await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 });
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  await rq(E.req.smpBegin(2), { timeout: 1000, retries: 0 });
  for (let off = 0; off < data.length; off += 256) await rq(E.req.smpWrite(2, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 });
  await rq(E.req.smpEnd(2, hdr), { timeout: 2000, retries: 0 });
  const A = await E.backupCapture(rq, info);
  const obj = (id) => A.objects.find((o) => o.id === id);
  ok(A.format === "sloop-backup" && A.objects.map((o) => o.id).join() === "0,1,2,3,4,5,9,32,33,34,35" && obj(9).len === 1464 && obj(34).len > 512 && obj(33).len === 0 && obj(35).len === 0
    && obj(4).len > 0 && obj(3).len === 0 && obj(32).len === 0 && E.bkProjFormat(E.b64dec(obj(0).data)) === "DRM1",
    "backup: LIST + GET: 11 objects (project in C as DRM1, sample in USR3, B empty, the SYN kits, USR1 and USR2 empty)");
  await rq(E.req.smpErase(1), { timeout: 2500, retries: 0 });
  await rq(E.req.set(0, 50, 5));
  await rq(E.req.project(1, 1), { timeout: 4000, retries: 0 });
  const r0 = await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));   /* as written to disk and read back */
  const B = await E.backupCapture(rq, info);
  ok(js(B.objects) === js(A.objects) && js(r0.skipped) === "[]", "backup: restore -> the same bytes again (projects, samples, settings); nothing skipped (USR1 empty)");
  /* a SLOOP backup: user preset banks, the FM6 bank and a sample in USR1 too: the rest restored, those left out and named */
  const S = JSON.parse(JSON.stringify(A));
  const blob = (id, n) => { const d = Uint8Array.from({ length: n }, (_, i) => (i * 7 + id) & 0xFF); return { id, len: n, crc: E.crc32(d) >>> 0, data: E.b64enc(d) }; };
  S.objects = S.objects.filter((o) => o.id !== 32).concat([blob(6, 300), blob(7, 300), blob(8, 3472), { ...blob(32, 900) }]);
  await rq(E.req.smpErase(1), { timeout: 2500, retries: 0 });
  const r1 = await E.backupRestore(rq, S);
  const C2 = await E.backupCapture(rq, info);
  ok(js(r1.skipped) === js([6, 7, 8, 32]) && js(C2.objects) === js(A.objects) && !m.state.smp[0].zones,
    "backup: a SLOOP backup restores the rest; its presets, FM6 bank and USR1 are skipped and said");
  ok(/if \(skipped\.length\) sayK\("bkSkipped", bkSkipNames\(skipped\)\);/.test(html) && (html.match(/bkSkipUsr1: "/g) || []).length === 2,
    "backup: the page says what was left out (both languages)");
  /* the project formats the firmware reads: DRM1 and SLOOP 2.x's FUN4..6; SLOOP 1.x's (or an unknown one): refused, nothing written */
  const pc = fw("project.c");
  ok(/PROJ_MAGIC 0x44524D31u/.test(pc) && /PROJ_MAGIC_V6 0x46554E36u/.test(pc) && /PROJ_MAGIC_V4 0x46554E34u/.test(pc) && !/PROJ_MAGIC_V3/.test(pc)
    && E.bkProjFormat(Uint8Array.of(0x31, 0x4D, 0x52, 0x44)) === "DRM1" && E.bkProjOk(Uint8Array.of(0x36, 0x4E, 0x55, 0x46, 1)) && E.bkProjOk(Uint8Array.of(0x34, 0x4E, 0x55, 0x46))
    && !E.bkProjOk(Uint8Array.of(0x33, 0x4E, 0x55, 0x46)) && !E.bkProjOk(Uint8Array.of(1, 2, 3, 4)) && E.bkProjOk(new Uint8Array(0))
    && E.bkFwAtLeast("SLOOP 2.6", [2, 5, 1]) && !E.bkFwAtLeast("SLOOP 2.5", [2, 5, 1]) && !E.bkFwAtLeast("FELUCCA SLOOP 2.0 (MOCK)", [2, 5, 1]) && E.bkFwAtLeast("FELUCCA BACKBEAT 1.0", [2, 5, 1]),
    "backup: project formats read (DRM1, FUN4..6 == project.c), the firmware compared");
  {
    const F3 = JSON.parse(JSON.stringify(A)), p2 = F3.objects.find((x) => x.id === 2);
    const d = E.b64dec(p2.data);
    d.set([0x33, 0x4E, 0x55, 0x46], 0);
    p2.data = E.b64enc(d);
    p2.crc = E.crc32(d) >>> 0;
    let err = null, writes = 0;
    const rqw = (r, o) => { if (r[0] !== C.INFO) writes++; return rq(r, o); };
    try { await E.backupRestore(rqw, F3); } catch (e) { err = e; }
    ok(err && err.code === "bkNewer" && writes === 0, "backup: a SLOOP 1.x project (FUN3) in it: refused (bkNewer), nothing written");
  }
  const bad = JSON.parse(JSON.stringify(A));
  bad.objects[2].data = E.b64enc(E.b64dec(bad.objects[2].data).map((x, i) => i === 3 ? x ^ 1 : x));
  let caught = "";
  try { E.backupObjects(bad); } catch (e) { caught = e.code; }
  ok(caught === "bkBad", "backup: a damaged file is refused before anything is written");
  done();
}

/* ------------------------------------- editor v5 (SLOOP 2.0): lanes, levels, ratchets --- */
async function editorV5() {
  const C = E.CMD;
  const { rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 11 && /^FELUCCA BACKBEAT \d/.test(info.version) && info.pcount === 71 && info.gcount === 45 && info.pe0 === 63, "v5..v11: INFO ends with the protocol version (11: drums only); 45 globals");
  /* the firmware says the same: the command numbers, INFO's layout, protocol 11 */
  const ec = fw("editor.c"), pc = fw("params.c");
  const en = (/enum \{ ED_INFO = 1,([^}]*)\}/.exec(ec) || [])[1] || "";
  const names = ["ED_INFO", ...en.replace(/\/\*[^*]*\*\//g, "").split(",").map((x) => x.trim().replace(/ = \d+$/, "")).filter(Boolean)];
  const num = (n) => { const m = new RegExp(`${n} = (\\d+)`).exec(en); return m ? +m[1] : names.indexOf(n) + 1; };
  const want = { INFO: 1, GET: 2, SET: 3, DUMP: 4, DESC: 5, STEP_GET: 6, STEP_SET: 7, PROJECT: 9, SMP_BEGIN: 11, SMP_INFO: 15, WATCH: 22, CHANGED: 23,
    RELOAD: 24, PING: 25, STEP_CHANGED: 26, TRACK: 27, TRACK_CHANGED: 32, DRUM_STEP: 33, BK_LIST: 34, BK_PUT: 36, LOCK_GET: 37, LOCK_SET: 38,
    MICRO_GET: 39, MICRO_SET: 40, FILL_GET: 41, FILL_SET: 42 };
  ok(Object.entries(want).every(([k, v]) => C[k] === v && num("ED_" + k) === v)
    && /#define ED_PROTO 11u/.test(ec) && /ed_b\(ED_PROTO\);/.test(ec) && /ed_b\(0\);\s*\/\* the engines \(drums only: none, no names\) \*\//.test(ec)
    && /#define ED_NOENG 0u/.test(ec) && !/case ED_PRESET:|case ED_NAMES:|case ED_UP_|case ED_FM6_/.test(ec)
    && /enum \{ ED_DSYN_LIST = 72, ED_DSYN_GET, ED_DSYN_PUT, ED_DSYN_STORE, ED_DSYN_PLAY \};/.test(fw("editor_dsyn.c"))
    && C.DSYN_LIST === 72 && C.DSYN_PLAY === 76, "v5..v11: command numbers and INFO == editor.c / editor_dsyn.c (11; no engine; PRESET, NAMES, UP_*, FM6_* gone)");
  const enumNames = (id) => (new RegExp(`${id}\\[\\] = \\{([^}]*)\\}`).exec(pc) || [])[1].split(",").map((x) => x.trim().replace(/"/g, ""));
  const gd = [];
  for (let i = 27; i < 32; i++) gd.push(E.parse[C.DESC](await rq(E.req.desc(1, i))));
  ok(gd.map((d) => d.label).join() === "DUST,DUCK,FILT,ROLL,NEW" && gd[3].names.join() === enumNames("N_ROLL").join() && gd[3].def === 1
    && /\[G_DUST\] = PD\("DUST", F_PCT, 0, 127, 0\)/.test(pc) && /\[G_FILT\] = PD\("FILT", F_FILT, -64, 63, 0\)/.test(pc)
    && /\[G_ROLL\] = PE\("ROLL", N_ROLL, 1\)/.test(pc) && /\[G_NEWPRJ\] = PE\("NEW", N_GO, 0\)/.test(pc),
    "v5: DUST, DUCK, FILT, ROLL, NEW (mock == params.c)");
  /* the value formats (params.c fmt_value) */
  const fv = (fmt, v, max = 127, min = 0) => E.fmtValue({ fmt, min, max, names: [] }, v).join("");
  ok(fv(E.F.SWING, 0, 100) === "0" && fv(E.F.SWING, 50, 100) === "50" && fv(E.F.SWING, 100, 100) === "100"
    && fv(E.F.FILT, 0, 63, -64) === "OFF" && fv(E.F.FILT, -64, 63, -64) === "LP100%" && fv(E.F.FILT, -32, 63, -64) === "LP50%" && fv(E.F.FILT, 63, 63, -64) === "HP100%"
    && fv(E.F.PCT, 127) === "100%" && fv(E.F.PCT, 64) === "50%" && fv(E.F.PCT, 60, 120) === "50%", "v5: SWING 0..100 (2.5), FILT LP / OFF / HP, PCT of the range");
  /* the drum track: 16 lanes with their level and ratchet; lane 15 uses the top bits */
  const d = E.emptyDrum();
  d.on = (1 << 0) | (1 << 4) | (1 << 15); d.lvl[0] = 3; d.lvl[4] = 1; d.rat[4] = 3; d.lvl[15] = 2; d.rat[15] = 1;
  const ds = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(5, d)));
  const dg = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(5)));
  ok(ds.index === 5 && dg.on === d.on && dg.lvl.join() === d.lvl.join() && dg.rat.join() === d.rat.join()
    && E.req.drumStep(5, d)[1].every((b) => b >= 0 && b < 128) && E.req.drumStep(5, d)[1].length === 14, "v5: DRUM_STEP round trip (16 lanes, levels, ratchets)");
  ok(E.DRUM_LANES.length === 16 && E.DRUM_LANES[0][0] === 36 && E.DRUM_LANES[15][0] === 56 && E.LV_NAMES.join() === "NORM,GHOST,SOFT,HARD",
    "v5: the 16 lanes (kick .. cowbell), 4 levels");
  const dc = fw("drums.c");
  const lanes = ((/LANE_NOTE\[DRUM_LANES\] = \{([^}]*)\}/.exec(dc) || [])[1] || "").split(",").map((x) => +x);
  ok(lanes.join() === E.DRUM_LANES.map((x) => x[0]).join(), "v5: lane notes == drums.c LANE_NOTE");
  /* the kit: the drum track's P_E0 (no track to select: the one track) */
  const kit = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  const dk = (/DRUM_KIT_NAMES\[\] = \{([^}]*)\}/.exec(dc) || [])[1] || "";
  const gen = join(HERE, "../build/gen/felucca_drumkits.h");
  const dsList = existsSync(gen) ? ((/#define DS_KIT_NAME_LIST (.*)/.exec(readFileSync(gen, "utf8")) || [])[1] || "") : null;
  const fwKits = dsList == null ? null : dk.replace("DS_KIT_NAME_LIST", dsList).split(",").map((x) => x.trim().replace(/"/g, ""));
  ok(kit.label === "KIT" && kit.names[5] === "808" && kit.names.length === kit.max + 1 && (!fwKits || fwKits.join() === kit.names.join()),
    `v5: the drum track's KIT (${kit.names.length} kits${fwKits ? ", == drums.c" : ""})`);
  /* the kit list without USR1 (its flash holds the songs; drums.c drum_kit_step passes over it): the numbers kept */
  const opts = E.kitOptions(kit, 5), usr1 = kit.names.indexOf("USR1");
  const withU = E.kitOptions(kit, usr1);
  ok(js(E.KIT_SONGS) === js(["USR1", "USR2"]) && usr1 > 0 && opts.length === kit.names.length - 2 && !opts.some(([, n]) => n === "USR1" || n === "USR2")
    && opts.every(([v, n]) => kit.names[v - kit.min] === n) && withU.length === kit.names.length - 1 && withU.some(([v]) => v === usr1)
    && /while \(k >= \(int32_t\)DRUM_USR && k <= \(int32_t\)DRUM_USR \+ 1/.test(dc) && /"USR1", "USR2", "USR3", "USR4", "USR3\+4"/.test(dc)
    && /kitOptions\(kd, cur\)/.test(html) && /\[KIT_PAIR, 1, 2, 3\]\.filter\(\(d\) => !SMP\.taken\(d\)\)/.test(html),
    "v5: the kits offered without USR1 and USR2 (== drums.c drum_kit_step), values kept, one shown while it is the kit; the kit page's slots too");
  ok(await noReply(rq, E.req.lockGet(1)) && !ev.unknown.length, "v5: one track: track 1 is not there (no reply); no unmatched replies");
  done();
}

/* ----------------------------- editor protocol v7: nudges and parameter locks (SLOOP 2.4) --- */
async function editorV7() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 11, "v7: INFO ends with 11 (v11 includes v7)");
  /* the lockable set == seq.c p_lockable (the ids as core.h names them) */
  const sc = fw("seq.c"), ch = fw("core.h");
  const ids = {}; let n = 0;
  for (const nm of (/enum \{\s*\/\* per-track parameters \*\/([^}]*)\}/.exec(ch) || [])[1].replace(/\/\*[\s\S]*?\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)) ids[nm] = n++;
  const lockableC = (id) => id <= ids.P_LD_AMP || id === ids.P_SGATE || (id >= ids.P_DIST && id <= ids.P_REV) || id === ids.P_GLIDE || id === ids.P_PAN
    || id === ids.P_DETUNE || (id >= ids.P_SLCR && id <= ids.P_SLDEPTH) || (id >= ids.P_E0 && id <= ids.P_E7) || id === ids.P_TFLT
    || (id >= ids.P_IA && id <= ids.P_IMIX);   /* 2.6: the INSERT's values */
  let same = ids.P_COUNT === 71 && ids.P_E0 === 63;
  for (let id = 0; id < 71; id++) same = same && !!E.lockable(id) === !!lockableC(id);
  ok(same && /id <= P_LD_AMP \|\| id == P_SGATE \|\| \(id >= P_DIST && id <= P_REV\) \|\| id == P_GLIDE \|\| id == P_PAN/.test(sc)
    && !E.lockable(ids.P_SLEN) && !E.lockable(ids.P_MUTE) && E.lockable(ids.P_TFLT) && E.lockable(ids.P_E0) && E.lockable(ids.P_IMIX),
    "v7: the lockable parameters (editor == seq.c p_lockable; LEN, MUTE are not; FILTER, the kit, the INSERT are)");
  /* MICRO: 64 bytes offset by 64; a set clamps to -32..31 */
  const mg = E.parse[C.MICRO_GET](await rq(E.req.microGet(0)));
  ok(mg.track === 0 && mg.micro.length === 64 && mg.micro.every((x) => x === 0), "v7: MICRO_GET: 64 nudges, all 0");
  const ms = E.parse[C.MICRO_SET](await rq(E.req.microSet(0, 4, -8)));
  const ms2 = E.parse[C.MICRO_SET](await rq(E.req.microSet(0, 5, 100)));
  const ms3 = E.parse[C.MICRO_SET](await rq(E.req.microSet(0, 6, -100)));
  const mg2 = E.parse[C.MICRO_GET](await rq(E.req.microGet(0)));
  ok(ms.step === 4 && ms.micro === -8 && ms2.micro === 31 && ms3.micro === -32 && mg2.micro[4] === -8 && mg2.micro[5] === 31 && mg2.micro[6] === -32
    && E.req.microSet(0, 4, -8)[1].join() === "0,4,56" && E.req.microSet(0, 4, 31)[1][2] === 95 && E.req.microSet(0, 4, -32)[1][2] === 32,
    "v7: MICRO_SET (sent + 64: -8 -> 56, 31 -> 95, -32 -> 32), clamped to -32..31");
  /* LOCK: set, move, list, delete; refused on a non-lockable parameter, bounded */
  const l1 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_E0, 1)));
  const l2 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_TFLT, 500)));
  const l3 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_SLEN, 8)));
  const l4 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 70, ids.P_E0, 1)));
  const lg = E.parse[C.LOCK_GET](await rq(E.req.lockGet(0)));
  ok(l1.rc === 0 && l1.has === 1 && l1.value === 1 && l2.rc === 0 && l2.value === 63 && l3.rc === 2 && !l3.has && l3.value === null && l4.rc === 1
    && lg.track === 0 && lg.locks.length === 2 && lg.locks[0].step === 2 && lg.locks[0].param === ids.P_E0 && lg.locks[1].value === 63
    && E.req.lockSet(0, 2, 5, -30)[1].length === 5 && E.req.lockSet(0, 2, 5)[1].length === 3,
    "v7: LOCK_SET makes and clamps a lock (FILT 500 -> 63), refuses LEN (rc 2) and step 70 (rc 1); LOCK_GET lists them");
  const l5 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_E0, 3)));
  const l6 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_E0)));
  const lg2 = E.parse[C.LOCK_GET](await rq(E.req.lockGet(0)));
  ok(l5.value === 3 && l6.rc === 0 && l6.has === 0 && lg2.locks.length === 1 && lg2.locks[0].param === ids.P_TFLT,
    "v7: LOCK_SET again moves the lock; without a value it deletes it");
  for (let i = 0; i < 23; i++) await rq(E.req.lockSet(0, i, ids.P_PAN, 10));   /* 24 in all */
  const full = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 40, ids.P_PAN, 10)));
  ok(full.rc === 3 && E.parse[C.LOCK_GET](await rq(E.req.lockGet(0))).locks.length === 24, "v7: the 25th lock is refused (rc 3, 24 a track)");
  /* a device-side nudge pushes STEP_CHANGED; the page's new lock starts on FX > FILTER */
  await rq(E.req.watch(1));
  m.sim.nudge(7, 12);
  await sleep(30);
  const sc7 = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 7);
  ok(!!sc7 && E.parse[C.MICRO_GET](await rq(E.req.microGet(0))).micro[7] === 12 && /const id = \[P\.TFLT, \.\.\.lockableIds\(\)\]/.test(html),
    "v7: a nudge on the device -> STEP_CHANGED of its step; a new lock starts on FILTER");
  ok(!ev.unknown.length, "v7: no unmatched replies");
  done();
}

/* ----------------------------- editor protocol v8: fill conditions (SLOOP 2.4) --- */
async function editorV8() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 11, "v8: INFO ends with 11 (v11 includes v8)");
  /* the condition codes == core.h FC_*; the bit layout of a 2-bit step field */
  const ch = fw("core.h"), sc = fw("seq.c");
  ok(/enum \{ FC_NORM, FC_FILL, FC_NOFILL \}/.test(ch) && /uint8_t fill\[NSTEP \/ 4\];/.test(ch)
    && /t->fill\[idx \/ 4u\] >> \(2u \* \(idx % 4u\)\)\) & 3u/.test(sc) && E.FC.FILL === 1 && E.FC.NOFILL === 2,
    "v8: FC codes and the 2-bit layout == core.h / seq.c step_fill");
  const g0 = E.parse[C.FILL_GET](await rq(E.req.fillGet(0)));
  ok(g0.track === 0 && g0.fill.length === 16 && g0.fill.every((x) => x === 0), "v8: FILL_GET: 16 bytes, every step normal");
  const s1 = E.parse[C.FILL_SET](await rq(E.req.fillSet(0, 4, E.FC.FILL)));
  const s2 = E.parse[C.FILL_SET](await rq(E.req.fillSet(0, 3, E.FC.NOFILL)));   /* (bits 6..7 of byte 0: the top bit crosses pack7) */
  const s3 = E.parse[C.FILL_SET](await rq(E.req.fillSet(0, 63, 3)));            /* 3: normal */
  const g1 = E.parse[C.FILL_GET](await rq(E.req.fillGet(0)));
  ok(s1.step === 4 && s1.cond === 1 && s2.cond === 2 && s3.cond === 0 && g1.fill[1] === 1 && g1.fill[0] === 0x80 && g1.fill[15] === 0
    && E.fillGet(g1.fill, 4) === 1 && E.fillGet(g1.fill, 3) === 2 && E.fillGet(g1.fill, 5) === 0
    && E.req.fillSet(0, 4, 1)[1].join() === "0,4,1" && E.req.fillGet(2)[1].join() === "2",
    "v8: FILL_SET sets a step (3 -> normal); FILL_GET returns the bytes as stored (0x80 through pack7)");
  {
    const b = new Uint8Array(16); E.fillSet(b, 7, 2); E.fillSet(b, 0, 1); E.fillSet(b, 7, 0);
    const rt = E.unpack7(E.pack7(Uint8Array.from([0x80, 0xFF, 0x01, 0x7F, 0x00, 0xC3, 0x55, 0xAA, 0x01, 0x80, 0, 0, 0, 0, 0, 0x40])));
    ok(E.fillGet(b, 0) === 1 && E.fillGet(b, 7) === 0 && b[1] === 0 && rt.length === 16 && rt[0] === 0x80 && rt[1] === 0xFF && rt[7] === 0xAA && rt[15] === 0x40,
      "v8: fillSet / fillGet on the client; pack7 keeps the top bits of 16 bytes (3 groups)");
  }
  /* one track: no other; a device-side change (SEQ + step + OCT+) pushes STEP_CHANGED */
  const none = await noReply(rq, E.req.fillGet(1));
  await rq(E.req.watch(1));
  m.sim.fill(9, 2);
  await sleep(30);
  const sc9 = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9);
  ok(none && !!sc9 && E.fillGet(E.parse[C.FILL_GET](await rq(E.req.fillGet(0))).fill, 9) === 2,
    "v8: one track (FILL_GET 1: no reply); a condition set on the device -> STEP_CHANGED of its step");
  ok(!ev.unknown.length, "v8: no unmatched replies");
  done();
}

/* ------------------------------------------------- 2.4: the drum kit page --- */
await (async () => {
  const lanes = { "BD Kick 808.wav": 0, "Snare_01.wav": 2, "HH closed.wav": 4, "OH open.wav": 5, "Crash cymbal.wav": 11, "Clap.wav": 3,
    "Low Tom.wav": 9, "Hi Tom.wav": 10, "rimshot.aif": 7, "Cowbell 1.wav": 15, "shaker.wav": 13, "Conga hi.wav": 14, "ride.wav": 12,
    "pedal hat.wav": 6, "piano C4.wav": -1, "kick2.wav": 1, "Snare 2.wav": 8 };
  const got = Object.entries(lanes).filter(([n, l]) => E.kitLaneOf(n) !== l).map(([n]) => `${n}->${E.kitLaneOf(n)}`);
  ok(!got.length, "drum kit: file names sorted onto the lanes" + (got.length ? ` (${got.join(", ")})` : ""));
  const s = (n, a = 20000) => Int16Array.from({ length: n }, (_, i) => Math.round(a * Math.sin(i / 7)));
  const pads = Array(16).fill(null);
  pads[0] = { s: s(8000), fname: "kick.wav", tune: 0, gain: 0, len: 0 };
  ok(E.kitPlace(pads, "BD second kick.wav") === 1 && E.kitPlace(pads, "piano.wav") === 1 && E.kitPlace(pads, "x.wav", 9) === 9,
    "drum kit: a taken lane gives way (KICK -> KICK 2), unknown names the first free pad, a drop its pad");
  pads[2] = { s: s(6000), fname: "snare.wav", tune: 12, gain: -6, len: 0 };
  pads[5] = { s: s(20000), fname: "oh.wav", tune: 0, gain: 0, len: 4410 };
  const p2 = E.kitPad(pads[2]), p5 = E.kitPad(pads[5]);
  const pk = (x) => x.reduce((a, v) => Math.max(a, Math.abs(v)), 0);
  ok(Math.abs(p2.length - 3000) <= 2 && Math.abs(pk(p2) / 20000 - 0.501) < 0.03 && E.kitLen(pads[2]) - p2.length <= 1,
    "drum kit: +12 st plays it twice as fast (half the length), -6 dB half the level");
  ok(p5.length === 4410 && Math.abs(p5[4409]) < 400 && pk(p5.subarray(0, 4000)) > 15000, "drum kit: LENGTH cuts it, with a 4 ms fade (no click)");
  const zs = E.kitZones(pads), b = E.buildSlot("KIT", zs);
  const hz = (j) => { const o = 32 + j * 28; return [b.hdr[o + 25], b.hdr[o + 26], new DataView(b.hdr.buffer).getInt16(o + 20, true) / 16]; };
  ok(zs.length === 3 && b.hdr[6] === 3 && [0, 1, 2].every((j) => { const [lo, hi, root] = hz(j); return lo === hi && hi === root; })
    && [hz(0)[0], hz(1)[0], hz(2)[0]].join() === "36,38,46",
    "drum kit: one zone per pad, lo = hi = root = the lane's note (drums.c LANE_NOTE: KICK 36, SNARE 38, OPEN HAT 46)");
  const big = Array(16).fill(null).map((_, l) => ({ s: s(30000), fname: l + ".wav", tune: 0, gain: 0, len: 0 }));
  ok(E.kitBytes(big) > 81408 && E.kitFit(big, 81408 - 64) && E.kitBytes(big) <= 81408 - 64 && E.kitBytes(big) > 81408 - 64 - 64,
    "drum kit: FIT shortens the longest pads to fill the slot, no more");
  {   /* KIT USR3+4: a big kit shared out over two slots, each pad whole in one */
    const R = 81408 - 64, mk = (secs) => ({ s: s(Math.round(secs * 22050)), fname: "x.wav", tune: 0, gain: 0, len: 0 });
    const kit = Array(16).fill(null);
    [0.6, 0.3, 0.3, 0.15, 2.5, 1.0, 0.2, 0.2, 0.3, 0.6, 0.6, 3.0, 2.5, 0.3, 0.4, 0.3].forEach((x, l) => { kit[l] = mk(x); });
    const tot = E.kitBytes(kit), sp = E.kitSplit(kit, R);
    const load = (ls) => ls.reduce((a, l) => a + ((E.kitLen(kit[l]) + 1) >> 1), 0);
    ok(tot > R && tot <= 2 * R && sp && sp[0].length + sp[1].length === 16 && load(sp[0]) <= R && load(sp[1]) <= R
      && new Set([...sp[0], ...sp[1]]).size === 16,
      `drum kit: USR3+4 takes a ${(tot * 2 / 22050).toFixed(1)} s kit no slot could, every pad whole in one of the two`);
    const huge = Array(16).fill(null).map(() => mk(1.5));
    ok(!E.kitSplit(huge, R) && E.kitFit(huge, R, 2) && E.kitSplit(huge, R) && E.kitBytes(huge) > 2 * R * 0.9,
      "drum kit: ... FIT for the pair fills both slots (no more than they hold)");
    const one = Array(16).fill(null); one[0] = mk(9);
    ok(!E.kitSplit(one, R) && E.kitFit(one, R, 2) && E.kitSplit(one, R) && E.kitSplit(one, R)[1].length === 0,
      "drum kit: ... a sound longer than a slot is cut to one slot");
  }
  const files = [{ name: "KIT/01 KICK.wav", data: new Uint8Array([1, 2, 3]) }, { name: "KIT/03 SNARE.wav", data: new Uint8Array(1000).fill(7) }];
  const back = await E.zipRead(E.zipStore(files));
  ok(back.length === 2 && back[0].name === "KIT/01 KICK.wav" && back[1].data.length === 1000 && back[1].data[999] === 7, "drum kit: a kit ZIP reads back (zipRead)");
  const dfl = await (async () => {                     /* a deflated entry, as zip tools write them */
    const raw = new Uint8Array(await new Response(new Blob([new Uint8Array(500).fill(65)]).stream().pipeThrough(new CompressionStream("deflate-raw"))).arrayBuffer());
    const z = E.zipStore([{ name: "a.wav", data: raw }]);
    z[8] = 8; z[raw.length + 30 + 5 + 10] = 8;          /* method 8 in the local header and in the directory */
    return (await E.zipRead(z))[0];
  })();
  ok(dfl.data.length === 500 && dfl.data[499] === 65, "drum kit: ... deflated ZIP entries too");
})();


/* ------------------------------ SLOOP 2.5: length, song order --- */
async function editorSong() {
  const { ARR } = E;
  /* the song order */
  const sb = E.mockSettings(), song = ARR.decode(sb);
  ok(ARR.ok(sb) && sb.length === 88 && !song.loop && js(song.entries) === js([0, 1, 2, 3].map((scene) => ({ scene, bars: 4 }))), "song: the settings' song order (PER3, arr_defaults)");
  const rows = ARR.group([{ scene: 0, bars: 4 }, { scene: 0, bars: 4 }, { scene: 1, bars: 8 }, { scene: 0, bars: 4 }]);
  ok(js(rows) === js([{ scene: 0, bars: 4, times: 2 }, { scene: 1, bars: 8, times: 1 }, { scene: 0, bars: 4, times: 1 }]) && ARR.expand(rows).length === 4,
    "song: rows (a section n times) <-> entries");
  const enc = ARR.encode(sb, { loop: true, entries: ARR.expand(rows) });
  ok(js(ARR.decode(enc)) === js({ loop: true, entries: ARR.expand(rows) }) && enc.subarray(0, 48).every((v, i) => v === sb[i]) && enc.subarray(84).every((v, i) => v === sb[84 + i]),
    "song: encode keeps the rest of the settings");
  ok(!ARR.valid({ entries: [] }) && !ARR.valid({ entries: Array(17).fill({ scene: 0, bars: 1 }) }) && !ARR.valid({ entries: [{ scene: 4, bars: 1 }] })
    && !ARR.valid({ entries: [{ scene: 0, bars: 65 }] }) && ARR.valid({ entries: Array(16).fill({ scene: 3, bars: 64 }) }), "song: valid == arranger.h arr_valid (1..16 entries, A..D, 1..64 bars)");
  const pc = readFileSync(join(HERE, "../firmware/src/project.c"), "utf8"), ah = readFileSync(join(HERE, "../firmware/src/arranger.h"), "utf8");
  const pt = (/typedef struct \{([^}]*)\} persist_t;/.exec(pc) || [, ""])[1];
  ok(/PERSIST_MAGIC 0x50455233u/.test(pc) && /arr_config_t arrangement;/.test(pt) && /ARR_STEPS 16u/.test(ah)
    && /if \(\(song\.playing \|\| transport_req\) && memcmp\(&p\.arrangement, &arrangement, sizeof arrangement\)\)\s*return 3;/.test(pc),
    "song: PER3, persist_t has the arrangement, 16 steps; a new order refused while playing (project.c)");
  /* read and send it through the backup objects, as the Song page does */
  const { m, rq, done } = attachMock({});
  const get1 = async () => { const L = E.parse[E.CMD.BK_LIST](await rq(E.req.bkList())); const it = L.items.find((x) => x.id === 1);
    return E.parse[E.CMD.BK_GET](await rq(E.req.bkGet(1, 0, it.len))).data; };
  const put1 = async (d) => {
    await rq(E.req.bkBegin(1, d.length, E.crc32(d)));
    await rq(E.req.bkData(1, 0, d));
    return E.parse[E.CMD.BK_PUT](await rq(E.req.bkCommit(1))).rc;
  };
  const cur = await get1();
  const mine = ARR.encode(cur, { loop: true, entries: [{ scene: 2, bars: 8 }, { scene: 3, bars: 2 }] });
  const rc1 = await put1(mine), now = await get1();
  const bad = ARR.encode(cur, { loop: false, entries: [{ scene: 0, bars: 4 }] }); bad[ARR.OFF + 5] = 0;
  const rc2 = await put1(bad);
  m.sim.play(true);
  const rc3 = await put1(ARR.encode(cur, { loop: false, entries: [{ scene: 1, bars: 1 }] }));
  const rc4 = await put1(now);                                 /* the same order while playing: fine */
  m.sim.play(false);
  ok(ARR.ok(cur) && rc1 === 0 && js(ARR.decode(now)) === js({ loop: true, entries: [{ scene: 2, bars: 8 }, { scene: 3, bars: 2 }] }) && rc2 === 2 && rc3 === 3 && rc4 === 0
    && js(ARR.decode(await get1())) === js(ARR.decode(now)), "song: mock: read, send (rc 0), a bad one (rc 2), a new one while playing (rc 3)");
  done();
  /* the page */
  ok(/id="lenval" type="number" min="1" max="64"/.test(html) && /if \(!s && id === pSlen\(\)\) lenChanged\(\);/.test(html) && /function lenChanged\(\) \{ renderSteps\(\); \}/.test(html),
    "editor: the LEN control (1..64), the drum lanes redrawn when LEN changes");
}

/* ------------------------------------- SLOOP 2.5: the DRUM SYNTH page (v10) --- */
async function editorDsyn() {
  const { DSYN } = E, C = E.CMD;
  const dsc = readFileSync(join(HERE, "../firmware/src/drum_synth.c"), "utf8");
  const st = (/typedef struct \{([\s\S]*?)\} dsnd_t;/.exec(dsc) || [, ""])[1].replace(/\/\*[\s\S]*?\*\//g, "");
  const fields = [...st.matchAll(/(?:u?int8_t)\s+([^;]+);/g)].flatMap((m) => m[1].split(",").map((x) => x.trim()));
  ok(js(fields) === js(DSYN.FIELDS) && DSYN.SIZE === 22, "dsyn: the 22 fields in dsnd_t's order (drum_synth.c)");
  const ln = ((/DS_LANE_NOTE\[DS_LANES\] = \{([^}]*)\}/.exec(dsc) || [, ""])[1]).split(",").map((x) => +x.trim());
  ok(js(ln) === js(DSYN.LANE_NOTE) && DSYN.LANES_N.length === 16, "dsyn: the lanes' notes == DS_LANE_NOTE");
  ok(/sizeof\(dsu_bank_t\) == 1464u/.test(dsc) && /DSU_MAGIC 0x31555344u/.test(dsc) && DSYN.BANK_SIZE === 1464, "dsyn: backup object 9 == dsu_bank_t (1464 bytes, DSU1)");
  /* the mock's factory kits == the firmware's (when built) */
  const raw = E.b64dec(E.DSYN_MOCK_KITS);
  let hdr = null;
  try { hdr = readFileSync(join(HERE, "../build/gen/felucca_drumkits.h"), "utf8"); } catch (e) { /* (not built) */ }
  if (hdr) {
    const kits = [...hdr.matchAll(/\{"([^"]*)", "[^"]*", (0x[0-9A-Fa-f]+|\d+), \{([\s\S]*?)\}\},/g)];
    const bytes = kits.flatMap((k) => [Number(k[2]), ...[...k[3].matchAll(/\{([^{}]*)\},/g)].flatMap((r) => r[1].split(",").map((x) => +x & 255))]);
    ok(kits.length === 32 && js(kits.map((k) => k[1])) === js(E.DSYN_MOCK_NAMES) && js(Array.from(raw)) === js(bytes),
      "dsyn: the mock's 32 kits == build/gen/felucca_drumkits.h");
  }
  /* viewing never changes a value: decode / encode give every factory sound back */
  let same = true;
  for (let i = 0; i < 32 * 16; i++) {
    const at = Math.floor(i / 16) * 353 + 1 + (i % 16) * 22, b = raw.slice(at, at + 22);
    same = same && js(Array.from(DSYN.encode(DSYN.decode(b)))) === js(Array.from(b));
  }
  const wild = DSYN.decode(DSYN.encode({ wave: 9, noise: 7, clap: 1, pitch: 300, fine: 40, bend: 200, btime: 999, hold: 999, decay: -5,
    tlev: 500, t2: 999, t2lev: 999, click: 999, nlev: 999, nhold: 999, ndec: 999, fmode: 7, fall: 1, res: 99, fcut: 999, fenv: -999, hpf: 999, chip: 999, drive: 999, level: 999 }));
  ok(same && wild.wave === 5 && wild.noise === 4 && wild.clap === 1 && wild.pitch === 127 && wild.fine === 15 && wild.bend === 96 && wild.decay === 0
    && wild.fmode === 3 && wild.res === 31 && wild.fenv === -128 && wild.level === 255 && wild.drive === 127,
    "dsyn: decode / encode: all 512 factory sounds unchanged; out-of-range values into the firmware's ranges");
  ok(Math.round(DSYN.hz(69)) === 440 && Math.round(DSYN.decayMs(127)) === 4000 && Math.round(DSYN.cutHz(0)) === 30 && DSYN.db(132) === 1,
    "dsyn: the units (Hz, ms, cutoff, dB) as tools/gen_tables.py");
  /* files and the backup object */
  const kit = { name: "MY 808", crush: 0x21, src: 0, sounds: Array.from({ length: 16 }, (_, l) => raw.slice(1 + l * 22, 23 + l * 22)) };
  const back = DSYN.fromFile(JSON.parse(JSON.stringify(DSYN.toFile(kit))));
  let bad = "";
  try { DSYN.fromFile({ format: "sloop-drumsynth", sounds: [[1, 2]] }); } catch (e) { bad = e.message; }
  const bank = DSYN.bankEncode([kit, kit, kit, kit]), dec = DSYN.bankDecode(bank);
  ok(back.name === "MY 808" && back.crush === 0x21 && js(back.sounds.map((x) => Array.from(x))) === js(kit.sounds.map((x) => Array.from(x))) && /not a drum synth kit/i.test(bad)
    && bank.length === 1464 && dec && dec[3].name === "MY 808" && dec[2].crush === 0x21 && js(Array.from(dec[1].sounds[15])) === js(Array.from(kit.sounds[15]))
    && DSYN.bankDecode(bank.slice(1)) === null, "dsyn: a kit file and the backup object, there and back; a wrong file refused");
  /* the device (mock): LIST / GET / PUT / STORE / PLAY */
  const { m, rq, done } = attachMock({});
  let L = E.parse[C.DSYN_LIST](await rq(E.req.dsynList()));
  const g0 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(DSYN.USER)));
  const f5 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(5)));
  ok(L.factory === 32 && L.user === 4 && L.stored && L.names[0] === "808" && L.kits.map((k) => k.name).join() === "808,909,TRAP,TECHNO"
    && g0.rc === 0 && g0.name === "808" && g0.src === 0 && js(g0.sounds.map((x) => Array.from(x))) === js(kit.sounds.map((x) => Array.from(x)))
    && f5.rc === 0 && f5.name === "TRAP", "dsyn: LIST (32 + 4, stored), GET SYN1 = the 808, GET a factory kit");
  const o = DSYN.decode(g0.sounds[0]); o.decay = 20; o.wave = 4;
  const p1 = E.parse[C.DSYN_PUT](await rq(E.req.dsynSound(0, 0, DSYN.encode(o))));
  const p2 = E.parse[C.DSYN_PUT](await rq(E.req.dsynHead(0, "BOOM", 0x10, 0)));
  const p3 = E.parse[C.DSYN_PUT](await rq(E.req.dsynCopy(3, 25)));
  const pl = E.parse[C.DSYN_PLAY](await rq(E.req.dsynPlay(0, 0, 100)));
  const g1 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(DSYN.USER)));
  L = E.parse[C.DSYN_LIST](await rq(E.req.dsynList()));
  ok(p1.rc === 0 && p2.rc === 0 && p3.rc === 0 && pl.rc === 0 && g1.name === "BOOM" && g1.crush === 0x10 && DSYN.decode(g1.sounds[0]).decay === 20
    && DSYN.decode(g1.sounds[0]).wave === 4 && L.kits[3].name === "CHIP" && !L.stored && m.state.dsynPlayed.length === 1 && m.state.dsynPlayed[0].sound[7] === 20,
    "dsyn: PUT a sound, the name and crush, a factory copy; PLAY hears the edit; LIST: not stored");
  m.sim.play(true);
  const s3 = E.parse[C.DSYN_STORE](await rq(E.req.dsynStore()));
  m.sim.play(false);
  const s0 = E.parse[C.DSYN_STORE](await rq(E.req.dsynStore()));
  L = E.parse[C.DSYN_LIST](await rq(E.req.dsynList()));
  const bp = E.parse[C.DSYN_PLAY](await rq(E.req.dsynPlay(0, 16)));
  ok(s3.rc === 3 && s0.rc === 0 && L.stored && bp.rc === 1, "dsyn: STORE while playing: rc 3; stopped: stored; PLAY of no lane: rc 1");
  /* a backup carries the kits, a restore puts them back */
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const A = await E.backupCapture(rq, info);
  await rq(E.req.dsynCopy(0, 1));
  await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));
  const g2 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(DSYN.USER)));
  ok(A.objects.some((x) => x.id === 9 && x.len === 1464) && g2.name === "BOOM", "dsyn: a backup holds the SYN kits (object 9); a restore puts them back");
  /* 2.6: the MIDI LEARN map, object 10, in a backup only while something is learned */
  const ml = new Uint8Array(40);                            /* (2.6: the map, then GLO > MIDI: 2, 11, 8, drums 10, BEND) */
  ml[0] = 21, ml[1] = (1 << 1) | (34 << 1);                 /* CC 21 -> track 1 (bits 7..8), code 34 (bits 9..15) */
  ml[32] = 2, ml[33] = 11, ml[34] = 8, ml[35] = 10, ml[36] = 2;
  m.state.ml = ml.slice();
  const M = await E.backupCapture(rq, info);
  m.state.ml = new Uint8Array(32);
  const N = await E.backupCapture(rq, info);
  m.state.ml = new Uint8Array(40);
  await E.backupRestore(rq, JSON.parse(JSON.stringify(M)));
  ok(M.objects.some((x) => x.id === 10 && x.len === 40) && !N.objects.some((x) => x.id === 10) && m.state.ml[0] === 21 && m.state.ml[1] === ml[1]
    && m.state.ml[33] === 11, "midi learn: a backup holds the learned CCs and GLO > MIDI (object 10, only when not the default); a restore puts them back");
  {   /* 2.6: a 2.6 backup without object 10 (the defaults then): restored as the defaults, not left as they are now */
    const N1 = JSON.parse(JSON.stringify(N));
    N1.firmware = "SLOOP 2.6";
    await E.backupRestore(rq, N1);
    ok(m.state.ml.length === 40 && m.state.ml.slice(0, 32).every((x) => x === 0) && m.state.ml[32] === 1 && m.state.ml[33] === 2
      && m.state.ml[34] === 3 && m.state.ml[35] === 10 && m.state.ml[36] === 2,
      "backup: a 2.6 one without object 10 resets MIDI LEARN and GLO > MIDI to the defaults (they were then)");
  }
  done();
}


/* ------------------------------------------------- editor tabs and strings --- */
function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 9 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b)
    && js([...TABS].sort()) === js(["drumsynth", "kits", "projects", "samples", "sequencer", "setlist", "settings", "song", "songs"]),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  /* drums only: SLOOP's Sound, Tracks and Library tabs, the synth step grid, piano roll, MIDI files, CHOP and FM6 are gone; a tab
     remembered from SLOOP's editor opens the first one; the side bar's headings */
  ok(!/id="p-(sound|tracks|library)"|id="(stepgrid|rollcv|midifile|chop|fm6panel|trackbar|engine|preset)"/.test(html)
    && !/\b(ROLL|SMF|FM6|CHOP|UP|LIB)\s*=|function (renderMixer|chopUI|renderLib|fm6Send|capturePatch)\b/.test(html)
    && /return TABS\[0\];/.test(html) && TABS[0] === "sequencer"
    && js([...html.matchAll(/<div class="navh" data-t="(\w+)">/g)].map((x) => x[1])) === js(["navDrums", "navSounds", "navDevice"]),
    "editor: drums only (no Sound / Tracks / Library, synth grid, roll, MIDI files, CHOP, FM6); first tab the sequencer");
  /* the cards: the drum track's PATTERN and FX (FILTER, INSERT, INSERT 2), the globals (params.c's pages) */
  const lay = (/const LAYOUT = \[([\s\S]*?)\n\];/.exec(html) || [])[1] || "";
  const pages = [...lay.matchAll(/\["([A-Z0-9 ]+)", (\d), \[([\d, ]+)\]\]/g)].map((x) => `${x[1]}:${x[2]}:${x[3]}`);
  ok(js(pages) === js(["PATTERN:0:29, 30, 31, 32", "FILTER:0:50", "INSERT:0:53, 54, 55, 56", "INSERT 2:0:57", "GLOBAL:1:0, 1, 2, 3", "MASTER:1:27, 28, 29, 30",
    "DRUMS:1:24, 25, 26, 32", "DLY:1:4, 5, 6, 7", "REV:1:8, 9", "MIDI:1:24, 14, 36, 12", "TEXTURE:1:37, 38, 39, 40", "TEXTURE 2:1:41, 42, 43"])
    && /\{"FILTER", FAM_FX, SC_TRACK, GR_NONE, \{P_TFLT, 0xFF, 0xFF, 0xFF\}\}/.test(fw("params.c")) && /\{"REV", FAM_FX, SC_GLOBAL, GR_NONE, \{G_RSIZE, G_RDAMP, 0xFF, 0xFF\}\}/.test(fw("params.c"))
    && /\{"DRUMS CHANNEL", RK_G, G_DRCH\}, \{"IN", RK_G, G_ROUTE\}, \{"OTHER CHANNELS", RK_G, G_CHOTH\}, \{"OUT", RK_G, G_MIDI\}/.test(fw("ui_menu.c"))
    && !/"MIDI 2"/.test(lay) && /"IN", "CPU"/.test(html),
    "editor: the cards == the device's pages (PATTERN, FX, DLY REV) and SETUP's sections (GLOBAL MASTER DRUMS, MIDI in one, TEXTURE)");
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "live", "polling", "bkSkipUp", "bkSkipFm6", "bkSkipUsr1", "fcNorm", "fcFill", "fcNoFill"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  {   /* 2.4: the device's palette as tokens (:root), used through var(--...); outside them only #000 (text on a colour) */
    const css = html.slice(html.indexOf("<style>"), html.indexOf("</style>")).replace(/:root\s*\{[^}]*\}/g, "");
    const hex = [...css.matchAll(/#[0-9a-f]{3,6}\b/gi)].map((m) => m[0].toLowerCase()).filter((c) => c !== "#000");
    ok(!hex.length && /--t1: #287cff; --t2: #1ecc70; --t3: #ffc618; --t4: #ff621a;/.test(html),
      "editor: the track colours of the device (ui_studio.c TE_COL), colours only as tokens" + (hex.length ? ` (${hex.join(" ")})` : ""));
    const te = readFileSync(join(HERE, "../firmware/src/ui_studio.c"), "utf8");
    ok(/TE_COL\[4\] = \{RGB\(40, 124, 255\), RGB\(30, 204, 112\), RGB\(255, 198, 24\), RGB\(255, 98, 26\)\}/.test(te),
      "editor: ... the same four as the firmware's TE_COL");
    ok(/SLOOP-FONT\*\/url\(data:font\/ttf;base64,[A-Za-z0-9+\/=]{20000,}\)/.test(html), "editor: the device's Terminus font inlined (tools/gen_webfont.py)");
    /* the import card takes a song JSON too (into the editor), the player has the count-in, the editor undo / redo and keys */
    ok(/id="gpi-file" type="file" accept="\.gp3,\.gp4,\.gp5,\.json,application\/json"/.test(html) && /id="play-count"/.test(html)
      && /id="sed-undo"/.test(html) && /id="sed-redo"/.test(html) && /id="sed-playsec"/.test(html) && /function sedKey\(e\)/.test(script)
      && /\$\("sed"\)|for \(const id of \["gpi", "sed"\]\)/.test(script) && /PLAY\.create\(spec, SPLAY\.count\)/.test(script)
      && /title: `\$\{t\("sedKey"\)\}\$\{key\}`/.test(script) && /\["sed-undo", "Ctrl\+Z"\]/.test(script) && /\$\("play-fill"\)\.title = "CC 21  " \+ t\("sedKey"\) \+ "f"/.test(script),
      "editor: the import card opens a JSON (or a drop on it or the editor), the player counts in, the editor has undo / redo / keys / play this section");
    /* the player steps through the sections (CC 24 / 25, keys . and ,) and says a section pressed again is a fill */
    ok(/id="play-prev" data-t="playPrev"/.test(html) && /id="play-next" data-t="playNext"/.test(html) && /\$\("play-prev"\)\.title = "CC 25  " \+ t\("sedKey"\) \+ ","/.test(script)
      && /\$\("play-next"\)\.title = "CC 24  " \+ t\("sedKey"\) \+ "\."/.test(script) && /e\.key === "\." \|\| e\.key === ","/.test(script) && /no fill: F first for one/.test(script)
      && /PLAY\.step\(SPLAY\.st, -1\)/.test(script) && /PLAY\.step\(SPLAY\.st, 1\)/.test(script),
      "editor: the player has previous / next section buttons with their CC and key; the section buttons say a second press is a fill");
    /* the section editor's pen and fill mark: the chosen one painted (as the cells it draws), the choice set as aria-pressed */
    ok(/#sed-pen button\[aria-pressed="true"\] \{ background: var\(--trk\)/.test(css) && /#sed-pen button\[aria-pressed="true"\]\[data-pen="X"\] \{ background: var\(--fg\)/.test(css)
      && /#sed-mark button\[aria-pressed="true"\]\[data-mark="F"\] \{ border-color: var\(--t3\)/.test(css) && /#sed-mark button\[aria-pressed="true"\]\[data-mark="N"\] \{ border-color: var\(--red\)/.test(css)
      && /b\.dataset\.pen === SONGED\.pen \? "true" : "false"/.test(script) && /b\.dataset\.mark === SONGED\.mark \? "true" : "false"/.test(script),
      "editor: the section editor's chosen pen and fill mark are highlighted");
  }
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "felucca-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  execFileSync(PYTHON, [join(HERE, "../tools/fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
  const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
  ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
}


/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(HERE, "../build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_9\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, finalIdentity = "FM-1_900", name = "FM-1", identity = "FM-1_015" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.finalIdentity = finalIdentity;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, name);
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900", "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Felucca"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const rescue = new FakeFM1(image);
  rescue.boot("FM-1_000", "Felucca");
  const recovered = await new Updater(rescue.access).install(image, "FM-1_900");
  ok(recovered === "FM-1_900" && rescue.bad === 0, "fm1ota.js: recovery mode -> loader -> normal firmware");
  const failedBoot = new FakeFM1(image, { finalIdentity: "FM-1_000" });
  const rescueSteps = [];
  const bootError = await new Updater(failedBoot.access).install(image, "FM-1_900", (k) => rescueSteps.push(k)).then(() => null, (e) => e);
  ok(bootError?.code === "mismatch" && !rescueSteps.includes("done"), "fm1ota.js: boot into recovery is not reported as successful installation");

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");
  {   /* an FM-1 running CLIPPER (USB name "Clipper", SLOOP's sister firmware) installs SLOOP like any FM-1 */
    const cl = new FakeFM1(image, { name: "Clipper", identity: "FM-1_901" });
    const g = await new Updater(cl.access).install(image, "FM-1_900");
    ok(g === "FM-1_900" && cl.bad === 0, "fm1ota.js: an FM-1 under CLIPPER (port 'Clipper') -> found and installed");
    /* other gear is never probed, and "not found" names the MIDI ports there are */
    const other = new FakeFM1(image, { name: "Arturia KeyStep Pro" });
    const e6 = await new Updater(other.access).install(image, "FM-1_900").then(() => null, (x) => x);
    ok(e6 && e6.code === "notfound" && other.served === 0 && /Arturia KeyStep Pro/.test(e6.detail),
       `fm1ota.js: a port of other gear is left alone; 'not found' lists it${e6 ? e6.detail : ""}`);
    ok(["FM-1", "Felucca", "Felucca Update", "Clipper", "SLOOP", "MIDIIN2 (Clipper)", "USB-MIDI"].every((n) => FM1_PORT.test(n)) &&
       !["Arturia KeyStep Pro", "loopMIDI Port", "Microsoft GS Wavetable Synth"].some((n) => FM1_PORT.test(n)),
       "fm1ota.js: FM1_PORT takes the FM-1's names (SLOOP, CLIPPER, the loader, the stock firmware), not other gear");
  }
  const stock = new FakeFM1(image);
  stock.boot("ota-FM-1_015", "FM-1 Update");
  const e3 = await new Updater(stock.access).resume(image).then(() => null, (x) => x);
  ok(e3 && e3.code === "foreign" && e3.detail === "ota-FM-1_015" && stock.served === 0, "fm1ota.js: another firmware's loader is never resumed ('foreign')");
  const back = new FakeFM1(image, { finalIdentity: "FM-1_015" });   /* the return to the official V15, interrupted */
  back.boot("ota-FM-1_015", "FM-1 Update");
  const st = [];
  const r4 = await new Updater(back.access).resume(image, (k) => st.push(k), { product: "FM-1_015" });
  ok(r4 === true && back.bad === 0 && st.at(-1) === "done", "fm1ota.js: return to official: its own loader resumed, V15 checked when back");
  const wrong = new FakeFM1(image, { finalIdentity: "FM-1_900" });
  wrong.boot("ota-FM-1_015", "FM-1 Update");
  const e5 = await new Updater(wrong.access).resume(image, null, { product: "FM-1_015" }).then(() => null, (x) => x);
  ok(e5 && e5.code === "mismatch", "fm1ota.js: return to official: another firmware coming back is not 'done'");
  const pk = await import(join(HERE, "fm1pkg.js"));
  const notStock = new Uint8Array(pk.STOCK_V15_SIZE);
  const e6 = await pk.validateStockPackage(notStock).then(() => null, (x) => x);
  ok(e6 && /official FM-1 V15/.test(e6.message), "fm1pkg.js: only the exact official V15 is accepted (SHA-256)");
}


await editorMock();
await editorLive();
await editorSetGate();
await editorV5();
await editorV7();
await editorV8();
await editorBackup();
await editorSongs();
editorPlay();
editorSed();
editorMeter();
await editorPedalIn();
await editorSong();
await editorDsyn();
editorTabs();
editorIcons();
samplesMatch();
await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
