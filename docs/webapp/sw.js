// SPDX-License-Identifier: GPL-3.0-only
// SLOOP 2.6: the site without internet (webapp/sw.js, made by web/make_site.py). Opened once online, the
// installer, the editor and the firmware they install are kept here; offline they come from this copy, still on
// https, so Web MIDI asks as usual (a downloaded file on Android opens as content://, where Chrome refuses MIDI).
// Online the network always wins (the page as published, never an old one); the copy is only the fallback.
// A new site (any file changed) is a new sw.js: the new copy replaces the old one.
const STAMP = "1.0-ada0e25a1ead";                          // version + a hash of the files
const CACHE = "backbeat-site-" + STAMP;
const FILES = ["../firmware/backbeat-1.0.fwsc", "backbeat.webmanifest", "editor/", "editor/FUKIAI-LICENSE.txt", "editor/fukiai.ttf", "editor/gp2song.py", "editor/kit/ATTRIBUTION.txt", "editor/kit/CLAP_D#2.wav", "editor/kit/COWBELL_G#3.wav", "editor/kit/CRASH_C#3.wav", "editor/kit/HAT_F#2-soft.wav", "editor/kit/HAT_F#2.wav", "editor/kit/HITOM_C3.wav", "editor/kit/KICK2_B1-soft.wav", "editor/kit/KICK2_B1.wav", "editor/kit/KICK_C2-soft.wav", "editor/kit/KICK_C2.wav", "editor/kit/LOWTOM_G2.wav", "editor/kit/MIDTOM_A2.wav", "editor/kit/OPENHAT_A#2-soft.wav", "editor/kit/OPENHAT_A#2.wav", "editor/kit/PEDAL_G#2-soft.wav", "editor/kit/PEDAL_G#2.wav", "editor/kit/RIDE_D#3.wav", "editor/kit/RIM_C#2.wav", "editor/kit/SHAKER_A#4.wav", "editor/kit/SNARE2_E2-soft.wav", "editor/kit/SNARE2_E2.wav", "editor/kit/SNARE_D2-soft.wav", "editor/kit/SNARE_D2.wav", "editor/realkit/ATTRIBUTION.txt", "editor/realkit/CLAP-1a.wav", "editor/realkit/COWBELL-1a.wav", "editor/realkit/CRASH-1a.wav", "editor/realkit/HAT-1a.wav", "editor/realkit/HAT-2a.wav", "editor/realkit/HAT-3a.wav", "editor/realkit/HAT-4a.wav", "editor/realkit/HITOM-1a.wav", "editor/realkit/HITOM-2a.wav", "editor/realkit/KICK-1a.wav", "editor/realkit/KICK-1b.wav", "editor/realkit/KICK-2a.wav", "editor/realkit/KICK-2b.wav", "editor/realkit/KICK-3a.wav", "editor/realkit/KICK-3b.wav", "editor/realkit/KICK-4a.wav", "editor/realkit/KICK2-1a.wav", "editor/realkit/LOWTOM-1a.wav", "editor/realkit/LOWTOM-2a.wav", "editor/realkit/MIDTOM-1a.wav", "editor/realkit/MIDTOM-2a.wav", "editor/realkit/OPENHAT-1a.wav", "editor/realkit/OPENHAT-2a.wav", "editor/realkit/PEDAL-1a.wav", "editor/realkit/PEDAL-2a.wav", "editor/realkit/RIDE-1a.wav", "editor/realkit/RIDE-2a.wav", "editor/realkit/RIM-1a.wav", "editor/realkit/RIM-2a.wav", "editor/realkit/SHAKER-1a.wav", "editor/realkit/SNARE-1a.wav", "editor/realkit/SNARE-1b.wav", "editor/realkit/SNARE-1c.wav", "editor/realkit/SNARE-2a.wav", "editor/realkit/SNARE-2b.wav", "editor/realkit/SNARE-2c.wav", "editor/realkit/SNARE-3a.wav", "editor/realkit/SNARE-3b.wav", "editor/realkit/SNARE-3c.wav", "editor/realkit/SNARE-4a.wav", "editor/realkit/SNARE-4b.wav", "editor/realkit/SNARE-4c.wav", "editor/realkit/SNARE2-1a.wav", "editor/realkit/kit.json", "icon-192.png", "icon-512.png", "icon-maskable-512.png", "installer/"];                          // relative to this file
const KEEP = new Set(FILES.map((f) => new URL(f, self.location).href));

const key = (url) => {                              // the copy's name of a request: no query, index.html = the folder
  const u = new URL(url);
  u.search = "";
  u.hash = "";
  if (u.pathname.endsWith("/index.html")) u.pathname = u.pathname.slice(0, -10);
  return u.href;
};

self.addEventListener("install", (e) => {
  e.waitUntil((async () => {
    const c = await caches.open(CACHE);
    await c.addAll(FILES.map((f) => new Request(f, { cache: "reload" })));   // all or nothing: never half a site
    await self.skipWaiting();
  })());
});

self.addEventListener("activate", (e) => {
  e.waitUntil((async () => {
    for (const k of await caches.keys())
      if (k.startsWith("backbeat-site-") && k !== CACHE) await caches.delete(k);
    await self.clients.claim();
  })());
});

self.addEventListener("fetch", (e) => {
  const req = e.request;
  if (req.method !== "GET") return;
  const k = key(req.url);
  if (!KEEP.has(k)) return;                         // not ours: the browser as usual
  e.respondWith((async () => {
    try {
      const r = await fetch(req.url, { cache: "no-cache" });   // online: what is published now (by its URL: a
                                                    // page's own request, mode navigate, takes no options)
      if (r.ok) {
        const copy = r.clone();
        e.waitUntil(caches.open(CACHE).then((c) => c.put(k, copy)).catch(() => {}));
      }
      return r;
    } catch (err) {                                 // offline: the copy
      const hit = await caches.match(k, { cacheName: CACHE });
      if (hit) return hit;
      throw err;
    }
  })());
});
