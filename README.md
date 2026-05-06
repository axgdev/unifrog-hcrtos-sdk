# HCRTOS SDK

This repository is the HCRTOS SDK used by UniFrog. It keeps the headers,
vendor archives, and source-buildable kernel subset needed to build the native
firmware without depending on the original full HCRTOS tree.

## Layout

```text
include/hcrtos/          HCRTOS and kernel-facing headers
include/newlib/          C library and FFmpeg public headers
include/generated/       Generated configuration headers included as <generated/...>
include/vendor/          Vendor media/GE headers
kernel/source/           Source-buildable HCRTOS kernel subset
kernel/objects.mk        Kernel source list used to build libkernel.a
lib/core/                Newlib-adjacent, FFmpeg, zlib, pthread archives
lib/vendor/              Vendor device/media/filesystem archives
lib/plugins/audio/       Audio codec plugin archives
```

`lib/core/libkernel.a` is intentionally not tracked. `make` rebuilds it from
`kernel/source` before consumers link against the SDK.

## Build

```sh
make
make check
make clean
```

The default MIPS toolchain is `/opt/mipsel-mti-elf`. Override it with
`TOOLCHAIN=/path/to/mipsel-mti-elf` when needed.

## Licensing

This SDK contains third-party headers, source, and binary vendor archives. It
is not MIT licensed as a whole. See `THIRD_PARTY_NOTICES.md` for currently
known license information.

UniFrog depends on this SDK as an external component so the frontend repository
can stay small and permissively licensed.

## Provenance

The SDK started as a staged subset from the local HCRTOS/SF2000 work tree and
now carries the kernel source needed by UniFrog directly. Archives that remain
under `lib/` are vendor or third-party binaries for which this repository does
not currently carry matching buildable source.
