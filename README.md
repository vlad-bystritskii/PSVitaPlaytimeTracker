<div align="center">

# 🎮 PSVitaPlaytimeTracker

**Know what you played on your PS Vita — and for how long — collected straight into Home Assistant.**

An on-console taiHEN plugin that quietly logs every play session and pushes it to the
same [Playtime Collector] add-on that already tracks your PS3. The Vita just shows up
as another platform.

![platform](https://img.shields.io/badge/platform-PS%20Vita%20HENkaku%20%2F%20taiHEN-003791)
![type](https://img.shields.io/badge/type-kernel%20%2B%20user%20plugin-444)
![license](https://img.shields.io/badge/license-GPLv3-blue)

</div>

[Playtime Collector]: https://github.com/vlad-bystritskii/ha-ps3-addon

---

## What it does

The Vita has no always-on file server (unlike the PS3's webMAN), so instead of being
polled, it **pushes**. Two small modules:

- **`playtime_k.skprx`** (kernel) — hooks the process-event handler, times each game
  you play, and appends finished sessions to a local queue. Reliable, catches suspend,
  works with no network.
- **`playtime_u.suprx`** (user, in SceShell) — drains that queue, looks up each game's
  name, and POSTs the session to your Home Assistant box. If you're offline (Vita out
  of the house), sessions wait in the queue and upload when you're back.

```
   ┌────────────────────── your PS Vita (taiHEN) ──────────────────────┐
   │  play a game ─► playtime_k (kernel)  ─► ux0:data/VitaPlaytime/     │
   │                 hooks proc events,       └─ pending.jsonl (queue)  │
   │                 times the session                  │              │
   │                                                    ▼              │
   │                 playtime_u (SceShell) ─► resolves title, POSTs ───┼──► Home Assistant
   └───────────────────────────────────────────────────────────────────┘     /ingest  (psvita)
```

## ✨ Highlights

- 🕵️ **Per-session, per-game** playtime, timed on the console itself.
- 🔌 **Offline-safe** — no network needed to *record*; uploads catch up later.
- 🧩 **Drops into your existing setup** — uses the collector's `POST /ingest`; no add-on
  change required. Sits next to PS3 data under platform `psvita`.
- 🪶 **Tiny** — a kernel hook + one SceShell thread.
- ⚙️ **Shareable** — host/token/name live in a plain `config.txt`; anyone can install it.

## 📥 Install

1. **Build** the two modules — see **[docs/BUILD.md](docs/BUILD.md)** (one `docker build`),
   or grab them from [Releases](../../releases).
2. Copy them to `ux0:data/VitaPlaytime/`.
3. Copy **[config.example.txt](config.example.txt)** → `ux0:data/VitaPlaytime/config.txt`
   and set your HA host, port, token and a name for this Vita.
4. Add to `ur0:tai/config.txt`:
   ```
   *KERNEL
   ux0:data/VitaPlaytime/playtime_k.skprx
   *main
   ux0:data/VitaPlaytime/playtime_u.suprx
   ```
5. Reboot. Play something, return to the LiveArea — it shows up in the collector
   dashboard within ~30 s.

> ⚠️ Requires a Vita on HENkaku/Ensō (taiHEN). For homebrew / personal use only.

## ⚙️ Config (`config.txt`)

```
ha_host=192.168.1.106
ha_port=3301
auth_token=          # must match the add-on's auth_token (blank = none)
account=vita         # the player/console label shown in Home Assistant
```

## 🔗 Pairs with

- **[Playtime Collector](https://github.com/vlad-bystritskii/ha-ps3-addon)** — the HA
  add-on that stores and displays everything (web dashboard + JSON API).
- **[PS3PlaytimeTracker](https://github.com/vlad-bystritskii/PS3PlaytimeTracker)** — the
  same idea for PS3. The collector merges both.

## 🙏 Credits & license

Kernel session-detection (the `SceProcessmgr` event hook) adapts
[**BetterTrackPlug**](https://github.com/FMudanyali/BetterTrackPlug) by FMudanyali
(GPLv3). Licensed under **GPLv3** — see [LICENSE](LICENSE).

See **[docs/DESIGN.md](docs/DESIGN.md)** for the full design and roadmap.
