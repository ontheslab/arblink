# ArbLink — AmiExpress rlogin Door

An AmiExpress BBS door that bridges a caller to an external door server over the
`rlogin` protocol. Written in C and cross-compiled on Windows for m68k-AmigaOS.

A standalone command-line rlogin client (`rlogin`) is also included, built from
the same shared transport layer (work in progress).

---

## Status

| Component | Status |
|---|---|
| `arblink` door | Working — validated under live AmiExpress/WinUAE testing |
| `rlogin` CLI client | Working — console input confirmed on hardware |

### Background

The project began as my attempt to get my head around Amiga E programming again (`old_e/`).
That version is only partially working. It has been retired and used as a reference in the new "C" adventure.

The current C rewrite corrects all of the issues I was having and has been confirmed working
through AmiExpress as an XIM door.

---

## Requirements

- AmiExpress with AEDoor available
- `bsdsocket.library` (AmiTCP-style TCP stack)

---

## Build

Cross-compile on Windows using vbcc for m68k-AmigaOS:

```batch
build.bat
```

| Output | Source | Purpose |
|---|---|---|
| `arblink` | `arblink.c` + modules | AmiExpress door binary |
| `rlogin` | `rlogin.c` + shared transport | Standalone CLI rlogin client |

The build script requires vbcc (`vc`) on the system PATH and the local AmigaOS
include trees at:

```
C:\amiga-dev\targets\m68k-amigaos\include
C:\amiga-dev\targets\m68k-amigaos\posix\include
```

---

## Installation

Copy `arblink` to the "DOORS" location on the Amiga and set it as an XIM command
in AmiExpress. Place `arblink.cfg` in the same drawer (`PROGDIR:`).
Create an appropriate ".info" file in your "COMMANDS" folder to launch it.

---

## Configuration

Copy `arblink.cfg.example` to `arblink.cfg` and edit as needed.

| Key | Default | Description |
|---|---|---|
| `host` | — | Remote rlogin server hostname or IP |
| `port` | `513` | Remote port (`login` is accepted as an alias for `513`) |
| `username_prefix` | — | Prefix prepended to the caller name before sending — see note below |
| `remote_user` | — | Override remote user name entirely; leave blank to use the prefixed caller name |
| `terminal_type` | `ansi` | Terminal type sent in the rlogin handshake |
| `terminal_speed` | `19200` | Terminal speed sent in the handshake |
| `terminal_columns` | `80` | Terminal width |
| `terminal_rows` | `24` | Terminal height |
| `disable_paging` | `0` | Set to `1` to suppress AmiExpress paging prompts during the session |
| `debug_enabled` | `0` | Set to `1` to enable file-based debug logging |
| `debug_log` | `arblink.log` | Log file path (e.g. `RAM:arblink.log`) |

**Username prefix note:** The prefix is designed for shared multi-BBS door server
setups where multiple BBS systems connect to the same remote door server. The prefix
acts as a unique tag per BBS so the server can distinguish callers across systems.
For example, with `username_prefix=V4S`, a caller named `Alex` is sent to the remote
server as `V4SAlex`.

---

## Source Files

| File | Responsibility |
|---|---|
| `arblink.c` | Top-level control flow: load config, open AEDoor, connect, run the live session, restore |
| `door_config.c/.h` | `key=value` config loader |
| `aedoor_bridge.c/.h` | AEDoor library lifecycle, caller identity, paging suppression, key polling |
| `rlogin_client.c/.h` | Shared rlogin socket transport: resolve, connect, handshake |
| `terminal_session.c/.h` | Live caller-to-remote and remote-to-caller bridge loop |
| `rlogin.c` | Standalone CLI client using the same transport layer |
| `doorlog.c/.h` | File-based debug logging |
| `aedoor_inline.h` | Local AEDoor inline stubs |
| `socket_inline_local.h` | Local socket inline wrapper |
| `door_version.h` | Version strings for both `arblink` and `rlogin` |

---

## Research References

| Reference | Used for |
|---|---|
| [RFC 1282 — BSD Rlogin](https://datatracker.ietf.org/doc/html/rfc1282) | Handshake structure, urgent-byte control codes, window-size reply, server acknowledgement |
| telser 1.40 — Stefan Stuntz et al. | Cursor-key translation approach, carrier-loss handling, `"login"` port alias convention |
| AmiXDoors — MultiRelayChat source | Confirmed correct `GETKEY` + `HotKey('')` polling pattern and `RAWARROW` usage |
| AmiExpress source | Confirmed `BB_NONSTOPTEXT`, `NODE_DEVICE`/`NODE_UNIT` behaviour, and device ownership model |
| AEDoor 2.8 SDK — Mathias Mischler | Core door interface: `CreateComm`, `GetString`, `GetDT`, `SetDT`, `WriteStr`, `HotKey` |

---

## Acknowledgements

AI assistance (Claude, Anthropic) was used during development for:

- Diagnosing the unresolved issues in the original E prototype — in particular the blocking
  `HotKey()` call, the unsafe handshake buffer, and inconsistent socket library ownership
- Proposing and extraction of `rlogin` as a standalone side project
  built from the shared transport layer

---

## Version

`arblink 1.34.02` / `rlogin 1.16.03` — source files renamed to match the binary names
throughout. Config renamed to `arblink.cfg`; legacy `rlogindoor.cfg` still accepted.
