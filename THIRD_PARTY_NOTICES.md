# Third-Party Notices

This SDK is a staged collection of headers and static libraries required to
link SF2000/HCRTOS applications. It contains multiple third-party and vendor
components under separate license terms. This document is a best-effort
inventory based on visible headers, embedded archive strings, and known
component names.

It is not legal advice and is not a complete source-code offer.

## Known Components

### FFmpeg Libraries

Archives:

- `lib/core/libavcodec.a`
- `lib/core/libavformat.a`
- `lib/core/libavutil.a`
- `lib/core/libswscale.a`

Detected version: FFmpeg 4.4-era headers/libraries.

Detected license strings: LGPL version 2.1 or later. The embedded configure
string includes `--disable-gpl` and `--disable-nonfree`.

### zlib

Archive:

- `lib/core/libz.a`

Detected version string: zlib 1.2.11.

### FreeRTOS / FreeRTOS POSIX Headers

Headers under:

- `include/hcrtos/freertos/`
- `include/hcrtos/FreeRTOS_POSIX*`

Detected notices include Amazon/FreeRTOS copyright headers and older FreeRTOS
GPL-with-exception text in some port headers.

### Linux / MIPS Syscall Headers

Headers under:

- `include/hcrtos/asm/`
- `include/hcrtos/asm-generic/`

Detected SPDX notices include `GPL-2.0 WITH Linux-syscall-note`.

### Apache-2.0 Style Headers

Some HCRTOS/NuttX-derived headers include Apache Software Foundation license
notices, including headers under:

- `include/hcrtos/nuttx/`
- selected POSIX/sys headers under `include/hcrtos/`

### Newlib Headers and C Library

Headers under:

- `include/newlib/`

Archive:

- `lib/core/libc.a`

Newlib is a collection of permissively licensed components with file-specific
copyright/license notices.

### Audio Codec Libraries

Archives under:

- `lib/plugins/audio/`

These include wrappers and/or codec implementations for AAC, MP3, FLAC, Opus,
Vorbis/Tremor, WMA, RealAudio, and PCM. Some embedded symbols identify known
projects such as FLAC, Opus, and Tremor. Exact license status needs deeper
source/provenance review before redistribution.

### Vendor / HCRTOS Device Libraries

Archives under:

- `lib/vendor/`
- selected `lib/core/` HCRTOS archives

Examples:

- `lib/vendor/libffplayer.a`
- `lib/vendor/libge.a`
- `lib/vendor/libviddrv*.a`
- `lib/vendor/libauddrv.a`
- `lib/vendor/libmmc*.a`
- `lib/core/libkernel.a`
- `lib/core/libpthread.a`

These appear to be HCRTOS/vendor-specific static libraries. Source and license
terms are not fully established in this staged SDK. Treat them as separately
licensed vendor components.

## Redistribution Notes

Because this SDK includes LGPL libraries, GPL-with-exception/ syscall-note
headers, permissively licensed components, and opaque vendor libraries, do not
represent this repository as MIT licensed.

Applications that link these static libraries may have additional obligations,
especially for LGPL components. If publishing binaries, keep build scripts,
object files, source/provenance references, and license texts sufficient for
the relevant licenses.
