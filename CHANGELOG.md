# Changelog

## v0.1.0 — initial scaffold (unbuilt)

- Two-module design: kernel `playtime_k.skprx` (session detection via the
  `SceProcessmgr` event hook, adapted from BetterTrackPlug) + user `playtime_u.suprx`
  (SceShell uploader).
- Kernel writes finished sessions to `ux0:data/VitaPlaytime/pending.jsonl`
  (`{titleId,seconds}`); user module resolves the title from `param.sfo` and POSTs to
  the Playtime Collector add-on's `/ingest` (platform `psvita`).
- File-queue handoff (`pending` → `sending` rename) doubles as the offline buffer.
- Config via `ux0:data/VitaPlaytime/config.txt` (host, port, token, account).
- CMake + Dockerfile (`vitasdk/vitasdk`) build for both modules.
- **Not yet compiled** — no VitaSDK toolchain on the build host; first build will need
  the usual stub/header convergence (see docs/BUILD.md).
