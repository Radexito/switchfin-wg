# WireGuard tunnel for Switchfin (Nintendo Switch)

This fork adds an **in-process WireGuard tunnel** to Switchfin, so the app can reach a Jellyfin
server that is not exposed to the internet: a home server behind NAT, reachable only through a
WireGuard peer.

No sysmodule, no Horizon socket hooks, no Android boot. The tunnel lives inside the Switchfin
process and only carries Switchfin's own traffic.

## How it works

```
Switchfin (borealis app)
  |  connects to http://127.0.0.1:<LocalPort>
  v
lwIP relay (inside the same process)
  |  resolves the TCP connection into the tunnel
  v
WireGuard (wg-nx, userland)  ->  Endpoint (your WG server)
  |  server side: forwards to Target:TargetPort
  v
Jellyfin
```

Enter this as the server address in Switchfin:

```
http://127.0.0.1:18096
```

`18096` is the default `LocalPort`. The tunnel starts automatically at app start when the config
file below exists.

## Configuration

The app reads `sdmc:/switch/Switchfin/wireguard.conf` (`/switch/Switchfin/wireguard.conf` on the
card). It is a small ini file, not a full `wg-quick` config:

```ini
[Interface]
PrivateKey = <base64 private key of the console peer>
Address    = 10.10.0.2
Target     = 192.168.1.5
TargetPort = 8096
LocalPort  = 18096
MTU        = 1200

[Peer]
PublicKey  = <base64 public key of the WireGuard server>
Endpoint   = vpn.example.com:51820
PersistentKeepalive = 25
```

| key | meaning |
| --- | --- |
| `PrivateKey` | the console's own WireGuard private key |
| `Address` | the console's address inside the tunnel (`/32` suffix is accepted and stripped) |
| `Target` | the Jellyfin server's address **as seen from the WG server** |
| `TargetPort` | Jellyfin port, normally `8096` |
| `LocalPort` | local port the relay listens on, `18096` by default |
| `MTU` | tunnel MTU, `1200` by default (see below) |
| `PublicKey` | the WireGuard server's public key |
| `Endpoint` | your WG server, hostname:port. A DDNS name works everywhere |
| `PersistentKeepalive` | keepalive seconds, `25` is fine behind NAT |

On the WG server the console needs its own `[Peer]` entry with `AllowedIPs = 10.10.0.2/32`, and
the server must route/forward `Target:TargetPort` for that peer.

## Settings screen

The tunnel can be configured without touching the SD card: **Settings → WireGuard** in the app.
The screen edits the same `/switch/Switchfin/wireguard.conf`:

- **Enable WireGuard tunnel** — turns the tunnel on or off. Disabling stops the running tunnel
  immediately; enabling takes effect on the next launch of the app.
- **Endpoint**, **Private key**, **Server public key**, **Tunnel address**,
  **Jellyfin address**, **Jellyfin port**, **Local relay port**, **Tunnel MTU** — every value from
  the config file, validated (ports must be 1-65535, MTU 1000-1400).

## MTU: why 1200 and not 1420

WireGuard adds 60 bytes per packet (IPv4 header, UDP header, WireGuard header and tag). Mobile
carriers frequently hand out a 1280-byte path, so a 1420 or even 1280 tunnel MTU black-holes the
larger packets: handshakes and small API calls work, video stalls.

Measured on a phone hotspot (ping inside the tunnel, `-M do`):

```
inner 1208 / outer 1268  -> passes
inner 1228 / outer 1288  -> 100% loss
```

So the path carried 1268 bytes and dropped everything above. `MTU = 1200` keeps the outer packet
at 1260 bytes and works on every path tested, including the carrier. Raise it to 1400 on a
wired/Ethernet-only path if you want a few more bytes per packet.

## Build

Same as upstream, plus the tunnel sources which are already wired into `CMakeLists.txt`:

```sh
cmake -B build -DPLATFORM_SWITCH=ON
cmake --build build --target Switchfin.nro
```

Cross-compiling needs `devkitPro` (`devkitA64`, `libnx`, `switch-curl`, `switch-mpv`,
`switch-ffmpeg`). A container image such as `devkitpro/devkita64` with the extra switch packages
works.

`library/wg-nx` (userland WireGuard + lwIP relay) is vendored here because the relay was patched:

- the relay dials **`TargetPort`**, not its own listen port
- a stale descriptor inside `poll()` no longer kills the relay loop (`EBADF` used to end playback
  mid-session, visibly when seeking)
- the wake socket is dropped instead of being treated as fatal

## Limits

- Only Switchfin uses the tunnel. Other homebrew, the system menu and games are unaffected.
- The tunnel does not route console traffic in general, so it is not a system-wide VPN.
- Nothing is sent to Jellyfin through a system DNS: the app talks to `127.0.0.1`.

## Troubleshooting

The app appends a log to `/switch/Switchfin/wg-log.txt` (truncated on each start). Points to look
at:

| log line | meaning |
| --- | --- |
| `krok 3a: wg_connect zwrocil 0` | handshake with the endpoint succeeded |
| `krok 4a: MTU tunelu = ...` | MTU actually in force |
| `relay laczy sie z portem docelowym` | relay targets `TargetPort` (fixed build) |
| `poll failed: errno=9, czyszcze deskryptory` | stale fd was recovered from, playback continues |
| `handshake nieudany (kod N)` | endpoint unreachable: wrong host/port, blocked UDP, wrong keys |

A quick external check that the handshake really happened, run on the WG server:

```sh
sudo wg show <interface> | grep -A4 <peer>
```

A fresh `latest handshake` plus growing `transfer` means the console is talking to the server;
if the app still fails, the fault is above the tunnel (target address/port or the relay).
