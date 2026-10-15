#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer
  firmware/felucca-VER.fwsc   the package
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  webapp/editor/backbeat-editor.html  the editor in one file, for offline use (SLOOP 2.6): its icon font inlined,
                              its links to the installer and the font licence online; opened from the disk in
                              Chrome / Edge (file:// is a secure context: Web MIDI asks for permission)
  webapp/sw.js, webapp/backbeat.webmanifest, webapp/icon-*.png  the site without internet (SLOOP 2.6): an app to
                              install; opened once online, the installer, the editor and the firmware are kept
                              by the browser and work offline, still on https (on Android a downloaded file opens
                              as content://, where Chrome refuses Web MIDI)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR [--beta]
  (--beta: web/beta_banner.html at the top of the installer: the beta channel, docs/beta)

The package identity (FM-1_9xx) is read from the package; the device reports it
after the install.
"""
import base64
import hashlib
import json
import re
import shutil
import sys
import urllib.parse
from pathlib import Path

HERE = Path(__file__).resolve().parent
ONLINE = "https://isod89.github.io/sloop-fm1/"                 # (the offline editor's links)


EMBED_TYPES = {".wav": "audio/wav", ".json": "application/json", ".txt": "text/plain", ".py": "text/x-python"}


def offline_assets():
    """the files the page fetches next to it, for the offline file: the layered kit (realkit/), the pads' kit (kit/),
    the factory grooves (grooves/) and the importer (gp2song.py): {path as fetched: (type, base64)}"""
    out = {}
    for folder, src in (("realkit", HERE.parent / "assets" / "realkit"), ("kit", HERE / "kit"),
                        ("grooves", HERE.parent / "grooves")):
        for f in sorted(src.iterdir()):
            if f.is_file() and f.suffix in EMBED_TYPES:
                out[f"{folder}/{f.name}"] = (EMBED_TYPES[f.suffix], base64.b64encode(f.read_bytes()).decode())
    out["gp2song.py"] = (EMBED_TYPES[".py"], base64.b64encode((HERE.parent / "tools" / "gp2song.py").read_bytes()).decode())
    return out


def offline_fetch(files):
    """a script for the offline file's head: the page's fetch of one of those paths answered from inside the file (as
    the site would serve it), anything else as usual"""
    return ("<script>/* the offline file (web/make_site.py): the kit, the grooves and the importer inside it */\n"
            "(() => { const F = " + json.dumps(files, separators=(",", ":")) + ", net = window.fetch.bind(window);\n"
            "  window.fetch = (u, o) => { let k = typeof u === \"string\" ? u.replace(/^\\.\\//, \"\") : \"\";\n"
            "    try { k = decodeURIComponent(k); } catch (e) { /* as it is */ }\n"
            "    const f = F[k]; if (!f) return net(u, o);\n"
            "    const b = Uint8Array.from(atob(f[1]), (c) => c.charCodeAt(0));\n"
            "    return Promise.resolve(new Response(b, { status: 200, headers: { \"Content-Type\": f[0] } })); }; })();\n"
            "</script>\n")


def offline_editor(html):
    """editor.html as one file for the disk: the icon font inlined, the kit, the grooves and the importer inside it
    (offline_fetch), the download link out, the relative links online"""
    font = base64.b64encode((HERE / "fukiai.ttf").read_bytes()).decode()
    for a, b in (('url("fukiai.ttf")', 'url(data:font/ttf;base64,' + font + ')'),
                 ('href="../installer/"', 'href="' + ONLINE + 'webapp/installer/" target="_blank" rel="noopener"'),
                 ('href="FUKIAI-LICENSE.txt"', 'href="' + ONLINE + 'webapp/editor/FUKIAI-LICENSE.txt" target="_blank" rel="noopener"')):
        if html.count(a) != 1:
            raise SystemExit(f"editor.html must contain {a} once; update make_site.py")
        html = html.replace(a, b)
    for pat, what in ((r"<!--DL-->.*?<!--/DL-->", "<!--DL-->...<!--/DL-->"),       # (the file has no site
                      (r"<!--PWA-->.*?<!--/PWA-->", "<!--PWA-->...<!--/PWA-->"),    # around it: no app to
                      (r"/\*PWA\*/.*?/\*/PWA\*/", "/*PWA*/.../*/PWA*/"),           # install, no copy kept)
                      (r'<button id="install"[^>]*></button>\s*', '<button id="install">')):
        html, n = re.subn(pat, "", html, flags=re.S)
        if n != 1:
            raise SystemExit(f"editor.html must contain {what} once; update make_site.py")
    if html.count("</head>") != 1:
        raise SystemExit("editor.html must contain </head> once; update make_site.py")
    return html.replace("</head>", offline_fetch(offline_assets()) + "</head>")


ICONS = ("icon-192.png", "icon-512.png", "icon-maskable-512.png")


def app_files(out, version, fw_name):
    """webapp/: the manifest, the icons and sw.js with the list of what it keeps (relative to webapp/) and a
    stamp of their contents, so any change makes a new sw.js and the browser takes the new site"""
    app = out / "webapp"
    for f in ICONS:
        shutil.copy(HERE / f, app / f)
    manifest = {
        "name": "BackBeat", "short_name": "BackBeat", "id": "editor/", "start_url": "editor/", "scope": "./",
        "display": "standalone", "background_color": "#000000", "theme_color": "#000000",
        "description": "BackBeat, the drummer for the M-VAVE FM-1: the editor and the installer, also without internet",
        "icons": [{"src": "icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any"},
                  {"src": "icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any"},
                  {"src": "icon-maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable"}],
    }
    (app / "backbeat.webmanifest").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    keep = {"installer/": app / "installer" / "index.html", "editor/": app / "editor" / "index.html",
            "../firmware/" + fw_name: out / "firmware" / fw_name, "backbeat.webmanifest": app / "backbeat.webmanifest"}
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (app / "editor" / f).exists():
            keep["editor/" + f] = app / "editor" / f
    for f in ICONS:
        keep[f] = app / f
    keep["editor/gp2song.py"] = app / "editor" / "gp2song.py"                  # (fm1-drummer: the importer and the
    for f in sorted((app / "editor" / "kit").iterdir()):                       # player's kit offline too; Pyodide
        keep["editor/kit/" + f.name] = f                                       # itself still needs the internet once)
    for f in sorted((app / "editor" / "realkit").iterdir()):                   # (the layered kit: the player's first)
        keep["editor/realkit/" + f.name] = f
    for f in sorted((app / "editor" / "grooves").iterdir()):                   # (the factory grooves: the Setlist tab's)
        keep["editor/grooves/" + f.name] = f
    h = hashlib.sha256()
    for k in sorted(keep):
        h.update(k.encode() + b"\0" + keep[k].read_bytes())
    sw = (HERE / "sw.js").read_text(encoding="utf-8")
    for mark in ("/*STAMP*/", "/*FILES*/[]"):
        if sw.count(mark) != 1:
            raise SystemExit(f"sw.js must contain {mark} once; update make_site.py")
    # the list as URLs: a "#" in a name (the kit's CLAP_D#2.wav) would start a fragment, the request going to CLAP_ (404,
    # and addAll is all or nothing: the worker never installed, the site never offline)
    urls = [urllib.parse.quote(k, safe="/.") for k in sorted(keep)]
    sw = sw.replace("/*STAMP*/", f"{version}-{h.hexdigest()[:12]}").replace("/*FILES*/[]", json.dumps(urls))
    (app / "sw.js").write_text(sw, encoding="utf-8")
    return sorted(keep)
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
        raise SystemExit(f"{pkg}: no Felucca update loader in it")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"backbeat-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})   # the page checks the download against it
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    logo = HERE.parent / "assets" / "logo" / "backbeat-logo.svg"  # the BackBeat logo (tools/gen_logo.py), inline
    html = html.replace("<!--LOGO-->", logo.read_text(encoding="utf-8") if logo.exists() else "<b>BackBeat</b>")
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    guide = HERE.parent / "output/pdf/Studio-0.3-guide-rapide-FR.pdf"
    guide_link = ""
    if guide.exists() and "--studio-guide" in sys.argv:   # LIVE: that guide describes the removed RYTHME tab
        shutil.copy(guide, out / guide.name)
        guide_link = '<p lang="fr"><a href="../../' + guide.name + '">Guide rapide illustré (PDF, 4 pages)</a></p>'
    html = html.replace("<!--STUDIO_GUIDE-->", guide_link)
    banner = ""
    if "--beta" in sys.argv:                       # the beta channel (docs/beta): what it is, what it implies
        notes = HERE / "beta_banner.html"
        banner = notes.read_text(encoding="utf-8") if notes.exists() else "<p><b>BETA</b></p>"
    html = html.replace("<!--BANNER-->", banner)
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("backbeat-*.fwsc")):   # one package: the current one
        old.unlink()
    shutil.copy(pkg, fw / name)
    ed_html = (HERE / "editor.html").read_text(encoding="utf-8")
    dl = f'download="backbeat-editor-{re.sub(r"[^A-Za-z0-9.-]", "-", version)}.html"'
    if ed_html.count('download="backbeat-editor.html"') != 1 or html.count('download="backbeat-editor.html"') != 1:
        raise SystemExit('editor.html and index_pkg.html must link download="backbeat-editor.html" once; update make_site.py')
    (ed / "index.html").write_text(ed_html.replace('download="backbeat-editor.html"', dl), encoding="utf-8")
    (ed / "backbeat-editor.html").write_text(offline_editor(ed_html), encoding="utf-8")
    (ed / "gp2song.py").write_text((HERE.parent / "tools" / "gp2song.py").read_text(encoding="utf-8"), encoding="utf-8")   # (fm1-drummer:
                                                                                                                            # the editor's From Guitar Pro)
    shutil.copytree(HERE / "kit", ed / "kit", dirs_exist_ok=True)                                                           # (its player's kit)
    shutil.copytree(HERE.parent / "assets" / "realkit", ed / "realkit", dirs_exist_ok=True)                                  # (the device's
                                                                                                                            # layered kit, layers and takes)
    shutil.copytree(HERE.parent / "grooves", ed / "grooves", dirs_exist_ok=True)                                            # (the factory
                                                                                                                            # grooves, read-only)
    (inst / "index.html").write_text(html.replace('download="backbeat-editor.html"', dl), encoding="utf-8")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    app_files(out, version, name)
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>BackBeat</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">BackBeat installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a not in ("--studio-guide", "--beta")]
    if len(args) != 3:
        sys.exit(__doc__)
    main(*args)
