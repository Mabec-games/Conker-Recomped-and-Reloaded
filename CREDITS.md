# Credits

*Conker: Recomped & Reloaded* is an unofficial, non-commercial fan project. It is not affiliated with or endorsed by
Rare, Microsoft or Xbox Game Studios. *Conker* and *Conker: Live & Reloaded* are trademarks of their owners. No part
of the game is included: every player builds it from their own disc.

The launcher's About & credits section shows this list, and its *Licence texts* button shows the full licences that
ship with it.

## The game

| What | Who | Licence | Where |
|---|---|---|---|
| *Conker: Live & Reloaded* (2005) | Rare / Microsoft Game Studios | proprietary, the player's own copy | — |
| **Live & Recompiled**, the native port the game is built with | **giacomozama**, with **TGP482** | GPL-3.0 | https://github.com/giacomozama/live-and-recompiled |
| Our modified port, the `launcher` branch: no in-game settings menu, Yes / No quit prompt, Back kept from Xbox Live, online room keys, traffic report, a grass-wind settings fix | Mabec Games | GPL-3.0 | https://github.com/Mabec-games/live-and-recompiled/tree/launcher |
| Xbox XBE Recomp, the recompiler and runtime Live & Recompiled is built on | **TGP482**, with Joshhhuaaa | MIT | https://github.com/TGP482/XboxXBERecomp |
| xemu and Cxbx-Reloaded, references for the Xbox's hardware and APIs | their teams | GPL-2.0 | https://xemu.app · https://github.com/Cxbx-Reloaded/Cxbx-Reloaded |
| ConkerXbox60FPSWidescreen patch, information about the game's code | its author | — | https://www.moddb.com/downloads/conkerxbox60fpswidescreen |
| SMAA | Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez | MIT | http://www.iryoku.com/smaa/ |
| SDL 3, Dear ImGui, Vulkan-Headers, volk, Vulkan Memory Allocator, glslang, Zydis, toml++, miniz, FLTK, Roboto | their authors | zlib, MIT, Apache-2.0, BSD, LGPL with static-linking exception | listed with full texts in `THIRD_PARTY.txt` (from the port) |

## The decensor mod

| What | Who | Licence | Where |
|---|---|---|---|
| Conker: Live & Reloaded Decensor | **nastys** (Italian, French), **Vaani** (English), **Theconker64** (Spanish) and contributors | none stated | https://github.com/nastys/conker-live-and-reloaded-decensor |

The launcher never redistributes the mod. When a player turns it on, the launcher downloads the official release
from the authors' GitHub and checks it against the checksum GitHub lists.

## The launcher

| What | Who | Licence | Where |
|---|---|---|---|
| The launcher | Mabec Games | GPL-3.0 | https://github.com/Mabec-games/Conker-Recomped-and-Reloaded |
| Artwork, "Conker: Recomped & Reloaded" | Mabec Games | — | — |
| Qt 6 (Core, Gui, Widgets, Network, DBus), linked dynamically | The Qt Company and contributors | LGPL-3.0 | https://www.qt.io · source: https://download.qt.io |
| libarchive | Tim Kientzle and contributors | BSD-2-Clause | https://libarchive.org |
| toml++ | Mark Gillard | MIT | https://github.com/marzer/tomlplusplus |
| miniupnpc, which asks the host's router to open the system link port | Thomas Bernard | BSD-3-Clause | https://miniupnp.tuxfamily.org |
| Room code words: the EFF Short Wordlist 2.0 (`resources/roomwords.txt`, "yo-yo" written "yoyo") | Electronic Frontier Foundation | CC BY 3.0 US | https://www.eff.org/dice |
| STUN public-address lookup (only when the router doesn't say), a service of Cloudflare | Cloudflare | — | stun.cloudflare.com |
| OpenSSL, for HTTPS | The OpenSSL Project | Apache-2.0 | https://www.openssl.org |
| zlib-ng, xz (liblzma), zstd, bzip2, lz4, brotli, libxml2, expat, ICU, HarfBuzz, FreeType, fontconfig, libpng, GLib, D-Bus, xkbcommon and other system libraries the AppImage carries | their authors | zlib, 0BSD/public domain, BSD, MIT, Unicode, LGPL | each with its licence in the AppImage |
| Zig 0.16.0, the compiler that builds the game on the player's machine | Zig Software Foundation and contributors | MIT | https://ziglang.org |
| LLVM, Clang, LLD, libc++, libc++abi, libunwind (inside Zig) | LLVM Project | Apache-2.0 WITH LLVM-exception | https://llvm.org |
| mingw-w64 and C library headers (inside Zig) | their projects | ZPL, MIT, LGPL, public domain | https://www.mingw-w64.org |
| linuxdeploy, AppImage runtime (packaging) | AppImage project | MIT | https://appimage.org |

## Source

- The launcher: this repository, GPL-3.0.
- The game it builds: [Mabec-games/live-and-recompiled](https://github.com/Mabec-games/live-and-recompiled/tree/launcher),
  branch `launcher`, a fork of [giacomozama/live-and-recompiled](https://github.com/giacomozama/live-and-recompiled),
  GPL-3.0. It is the `live-and-recompiled` submodule here.
- Every licence text, and a list of the system libraries the AppImage carries, are in the AppImage under
  `usr/share/doc/conker-launcher/licenses` and in the launcher's *Licence texts* window.
