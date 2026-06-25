# Building & installing

Two modules are produced:

| File | Loads as | Role |
|---|---|---|
| `playtime_k.skprx` | kernel (`*KERNEL`) | session detection → queue |
| `playtime_u.suprx` | user (`*main`/SceShell) | upload queue → Home Assistant |

## Build with Docker (recommended)

The official [`vitasdk/vitasdk`](https://hub.docker.com/r/vitasdk/vitasdk) image has the
whole toolchain, so nothing pollutes your host.

```sh
docker build -t vitaplaytime .
docker run --rm -v "$PWD/dist:/out" vitaplaytime
# -> dist/playtime_k.skprx  dist/playtime_u.suprx
```

> **Windows:** Docker Desktop needs the WSL2 backend. If `wsl` is not installed yet:
> run `wsl --install` in an **admin** PowerShell, reboot, then install Docker Desktop.
> In Git Bash use `"$PWD/dist:/out"`; in PowerShell use `"${PWD}\dist:/out"`.

## Build without Docker (native VitaSDK)

Install VitaSDK (https://vitasdk.org — `vdpm`), then:

```sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
cmake -S . -B build && cmake --build build
# -> build/kernel/playtime_k.skprx  build/user/playtime_u.suprx
```

On Windows this is easiest inside WSL2 (Ubuntu): `wsl --install`, then install VitaSDK
in the Ubuntu shell and build there.

## First build may need fixups

These modules were written against the documented VitaSDK APIs but not yet compiled on
this machine (no toolchain installed here). Expect the first build to surface a few
stub-name / header tweaks — that's the normal "converge the build" step. Likely spots:

- **Kernel link list** (`kernel/CMakeLists.txt`) — if a `ksce*` symbol is undefined,
  add the matching `*ForDriver_stub` (e.g. `ksceKernelGetProcessTitleId` →
  `SceProcessmgrForDriver_stub`).
- **User sysmodule / http enums** — `SCE_SYSMODULE_HTTP`, `SCE_NETCTL_STATE_CONNECTED`,
  `sceHttp*` signatures; check against `$VITASDK/arm-vita-eabi/include`.

## Install on the Vita

1. Copy the two modules anywhere on the SD, e.g.:
   ```
   ux0:data/VitaPlaytime/playtime_k.skprx
   ux0:data/VitaPlaytime/playtime_u.suprx
   ```
2. Create the config from the example:
   ```
   ux0:data/VitaPlaytime/config.txt      (see config.example.txt)
   ```
3. Edit `ur0:tai/config.txt` (or `ux0:tai/config.txt`) and add:
   ```
   *KERNEL
   ux0:data/VitaPlaytime/playtime_k.skprx

   *main
   ux0:data/VitaPlaytime/playtime_u.suprx
   ```
   `*main` is SceShell — the uploader stays resident there.
4. Reboot, or refresh taiHEN (HENkaku Settings → reload / molecularShell). Then play a
   game, return to the LiveArea, and within `POLL_SEC` (30 s) the session is POSTed to
   the collector — it appears in the add-on dashboard under platform `psvita`.

### Quick checks

- Kernel writing sessions: `ux0:data/VitaPlaytime/pending.jsonl` grows after you quit a game.
- Uploader working: that file (and `sending.jsonl`) drains to empty once the POST lands.
- HA side: `GET http://<ha_host>:3301/stats` lists games with no PS3 profile id but
  `platform":"psvita"`.
