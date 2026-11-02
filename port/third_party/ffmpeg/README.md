# FFmpeg

[FFmpeg](https://ffmpeg.org) 8.1.3 (`ffmpeg-8.1.3.tar.xz` from
<https://ffmpeg.org/releases/>, SHA-256
`7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3`), for the
Xbox's Bink movies (`display.movies`: `port/linux/src/bink_ffmpeg.c`, which
`port/linux/src/bink_movie.c` plays as the Bink SDK did). GNU Lesser General
Public License, version 2.1 or later: `COPYING.LGPLv2.1`, `LICENSE.md`.

Only the Bink demuxer and the `bink`, `binkaudio_rdft` and `binkaudio_dct`
decoders are built, in plain C: no assembly, no threads, no hardware
decoding, no network or protocols (the caller opens the file), no encoders,
no muxers, no filters, nothing under the GPL (configure's `--enable-gpl` and
`--enable-version3` are off). `sources.txt` lists the 84 files the builds
compile (`ffmpeg_sources` in `tools/build_common.py`), those the Bink
components need of `libavutil`, `libavcodec` and `libavformat`; the other
files here are the headers they include, and `compat/atomics/dummy`'s
`stdatomic.h`, FFmpeg's own for a build without threads. They are copied
from the release unchanged.

`config/` is what FFmpeg's `configure` writes for such a build
(`config.h`, `config_components.h`, `libavutil/avconfig.h`,
`libavutil/ffversion.h` and the components' lists), written by
`tools/ffmpeg_vendor.py` without running it: every `HAVE_`, `CONFIG_` and
`ARCH_` name the files test is 0 but the components above, what they select
(`blockdsp`, `hpeldsp`, `wma_freqs`), the safe bitstream reader and the C
library's and maths' functions the guests have (musl). The equivalent
configure line is

    ./configure --disable-everything --disable-programs --disable-doc \
        --disable-network --disable-asm --disable-pthreads --disable-autodetect \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
        --enable-small --enable-decoder=bink,binkaudio_rdft,binkaudio_dct \
        --enable-demuxer=bink

The files are compiled into the game's guest image on every platform (the
Linux builds, Windows, macOS, iOS and Android), with `-DHAVE_AV_CONFIG_H`
and FFmpeg's warnings off; they add about 850 KB of code and data to it.

To take a new release: unpack it, set `VERSION` in `tools/ffmpeg_vendor.py`,
run `python3 tools/ffmpeg_vendor.py <the release's folder>`, build, and add to
`sources.txt` the files whose functions the link then misses.

## The licence

The LGPL 2.1 lets this library be combined with OpenH1's
`AGPL-3.0-or-later` (its section 3 lets its terms be the GPL's, version 2 or
later, and the GPL 3.0 combines with the AGPL 3.0). The game links it
statically: section 6 asks that a person be able to change the library and
link the game again, which the game's complete source, published with every
release (`build-<n>` tags), and its build allow. The release downloads carry
the licence as `ffmpeg-COPYING.LGPLv2.1.txt` (`tools/linux_package.py`).
