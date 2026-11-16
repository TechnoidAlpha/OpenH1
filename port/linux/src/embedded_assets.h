/*
EMBEDDED_ASSETS.H

The bytes of the assets tools/embed_assets.py lists (the high-res HUD's,
titles' and buttons' textures, the fonts and the menus' files: about 15 MB,
which the guest images have no room for) are in a file beside the game,
openh1.assets, which the builds put there as they do brokers.txt (beside
config.toml; the Linux release's share/openh1; the macOS and iOS apps and
the APK copy theirs beside config.toml at each start). Their tables are
compiled in (hud_hires.h, text_hires.h, menu_files.h), each entry's bytes an
offset in that file.

The file: "OH1A", its form (1), its size and 0, 32 bits each; then the
assets, each at a 16-byte boundary. It is read whole the first time an
asset is asked for, and kept; one whose size or CRC-32 is not the build's
(embedded_assets_size, embedded_assets_crc: another build's) is not used.
The game's start stops with an error naming the file when it is missing or
not this build's (embedded_assets_check); a dedicated server, which draws
nothing, starts without it.
*/

#ifndef EMBEDDED_ASSETS_H
#define EMBEDDED_ASSETS_H

#define EMBEDDED_ASSETS_FILE "openh1.assets"

extern const unsigned int embedded_assets_size;
extern const unsigned int embedded_assets_crc;

/* an asset's bytes (its offset and size in the file), or NULL where the file
is missing or not this build's */
const void *embedded_assets_data(unsigned int offset, unsigned int size);
/* the file read (if it was not yet): NULL when it is this build's, else why
not ("<path> is missing", "... is not this build's"); the game's start
stops on it with that said (sdl_platform.c), as it does on missing maps */
const char *embedded_assets_check(void);

#endif
