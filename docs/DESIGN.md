# VitaPlaytimeTracker — Design

An on-console plugin for HENkaku/taiHEN PS Vita that records **what was played and for
how long**, and pushes each finished session to the **[Playtime Collector] Home
Assistant add-on** — the same collector that already ingests PS3 playtime. The Vita
shows up as just another platform (`psvita`) next to the PS3.

[Playtime Collector]: https://github.com/vlad-bystritskii/ha-ps3-addon

## Why two modules

Reliable game detection on the Vita lives in the **kernel** (catching launch / exit /
suspend events the way [BetterTrackPlug] does), but networking from the kernel is
painful and rarely done. So the work is split, each half doing what it is good at:

[BetterTrackPlug]: https://github.com/FMudanyali/BetterTrackPlug

| Module | Where | Job |
|---|---|---|
| **`playtime_k.skprx`** | kernel (`*KERNEL`) | Hook the process-event handler, time each session, append it to a local queue file. No network. |
| **`playtime_u.suprx`** | user, in `SceShell` (`*main`) | A background thread drains the queue: resolve the game's name, POST each session to the collector over `sceHttp`, drop what was accepted. |

The two communicate through a **file queue** on `ux0:` — which doubles as the
**offline buffer**: if the Vita has no network (taken out of the house), sessions pile
up in the queue and upload later, exactly like the PS3 plugin's offline log. The
kernel module is the source of truth; the uploader never invents playtime.

This mirrors the PS3 setup (plugin = source of truth, collector reads it) — except the
"collector" half here runs on the Vita itself and pushes, because the Vita has no
always-on HTTP file server like the PS3's webMAN.

## Kernel module — `playtime_k.skprx`

Modeled on BetterTrackPlug's detection (GPLv3), but session-based instead of
cumulative counters.

- Hooks `SceProcessmgr` import **NID `0x414CC813`** (`ksceKernelInvokeProcEventHandler`)
  via `taiHookFunctionImportForKernel`. The handler fires with an event code:
  - **1 = Startup**, **5 = Resume** → a game came to the foreground: remember its
    `titleId` (`ksceKernelGetProcessTitleId`) and start tick (`ksceKernelGetSystemTimeWide`, µs).
  - **3 = Exit**, **4 = Suspend** → the game left: `seconds = (now − start) / 1e6`;
    if it ran for >0 s, append one session line to the queue.
- **System processes are skipped** — a `titleId` starting with `main` (SceShell) or
  `NPXS` (system apps: settings, store, etc.).
- Sleep splits a play into two sessions (one ends at Suspend, the next starts at
  Resume); the total is preserved.

### Why no timestamps / RTC in the kernel

The collector's `POST /ingest` stamps the session time itself (`db.now_iso()` on the
HA box) and only needs `seconds`. So the kernel doesn't compute wall-clock ISO times —
it just needs an accurate duration, which the monotonic system clock gives. (A later
phase can carry a real `endedAt` end-to-end; see Roadmap.)

### Queue format — `ux0:data/VitaPlaytime/pending.jsonl`

Append-only, one finished session per line, written with `snprintf` (kernel links
`SceLibc_stub`, so `snprintf` is available):

```json
{"titleId":"PCSE00120","seconds":4187}
```

- `titleId` — Vita title id (e.g. `PCSE00120`); alphanumeric, no JSON escaping needed.
- `seconds` — session duration.

Kept to `%s`/`%u` only — kernel `snprintf` (SceSysclib) can't be trusted with `%llu`.
The user module enriches each line with `platform`, `account`, and the resolved
`title` before sending.

## User module — `playtime_u.suprx`

A taiHEN user plugin injected into **`SceShell`** (`*main`), so it is always resident
and has the network stack the shell already brought up. On load it spawns one worker
thread and returns immediately.

Worker loop (every `POLL_SEC`):

1. **Config** — read `ux0:data/VitaPlaytime/config.txt` (`ha_host`, `ha_port`,
   `auth_token`, `account`). Re-read each pass so edits apply without a reboot.
2. **Claim the queue atomically** — if `sending.jsonl` does not exist and
   `pending.jsonl` does, `sceIoRename(pending → sending)`. The kernel keeps appending
   to a fresh `pending.jsonl`; the uploader works on the snapshot in `sending.jsonl`.
   This avoids a read/rewrite race without locks.
3. **Drain `sending.jsonl`** — for each line: parse `titleId` + `seconds`, resolve the
   display `title`, POST to the collector. Keep the lines that failed.
4. **Rewrite** `sending.jsonl` with only the failures (or delete it when all sent).
5. Sleep, repeat.

### Title resolution

The display name is read from the game's own SFO:
`ux0:app/<titleId>/sce_sys/param.sfo`, key `TITLE`. A minimal SFO parser handles this
(magic `\0PSF`, key table + data table). If the SFO is missing (e.g. a system title
id), the `titleId` is sent as the title and the collector can map it via its
`title_overrides`.

### Upload — `POST /ingest`

```
POST http://<ha_host>:<ha_port>/ingest
X-Auth-Token: <auth_token>
Content-Type: application/json

{"platform":"psvita","account":"<account>","titleId":"PCSE00120","title":"Persona 4 Golden","seconds":4187}
```

Plain HTTP over the LAN (no TLS) via `sceHttp`. A `2xx` status means accepted → the
line is dropped from the queue. Anything else (or a network error) keeps the line for
the next pass. This is exactly the body the add-on's existing `/ingest` endpoint
expects — no add-on change is required for basic ingestion.

## Storage layout (`ux0:data/VitaPlaytime/`)

| File | Writer | Purpose |
|---|---|---|
| `config.txt` | user (you) | collector host/port, token, account name |
| `pending.jsonl` | kernel | live append queue of finished sessions |
| `sending.jsonl` | user | in-flight snapshot being uploaded (transient) |

## Roadmap

1. **Toolchain + hello** — build both modules, confirm they load via `config.txt` and
   write a marker file. (Validates the VitaSDK build + taiHEN load chain first.)
2. **Kernel MVP** — event hook, session timing, `pending.jsonl`.
3. **Uploader MVP** — queue drain, SFO title, `sceHttp` POST, offline retry.
4. **Polish** — Adrenaline (PSP/PS1) titles, real `endedAt` carried to `/ingest`,
   a tiny status notification.

## Build

Reproducible via the official `vitasdk/vitasdk` Docker image (so it builds the same on
Windows/macOS/Linux). See [BUILD.md](BUILD.md).

## License

GPLv3 — it adapts GPLv3 kernel-detection code from BetterTrackPlug (itself derived from
webMAN-scene work). See [LICENSE](../LICENSE).
