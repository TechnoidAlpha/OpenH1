/*
HALO_PORT_CAPACITY.H

Memory capacity of the native builds (Windows, Linux, Android), sized for the
session limits in halo_port_limits.h, which includes this file. The Xbox
sizes are given in parentheses below.

Every machine in a session must be built with the same values: the
distributed netcode names objects and players by their datum index, the
same on every machine (port/linux/game/network_objects.c tracks
MAXIMUM_TRACKED_OBJECTS = HALO_PORT_MAXIMUM_OBJECTS_PER_MAP objects, and a
client's own objects take the upper half of the object array).
*/

#ifndef __HALO_PORT_CAPACITY_H
#define __HALO_PORT_CAPACITY_H

/* ---------- game state

The Xbox game state is 0x345000 bytes at 0x80061000 and ends where the tag
cache begins (0x803A6000). Cache files are linked to that tag cache address,
so the game state cannot grow in place. The native builds put a 16 MB game
state above the tag cache (which ends at 0x819A6000), inside the Xbox memory
window (0x80000000-0xA0000000: port/linux/src/platform.h) and below
everything the window hands out top-down (texture and sound caches,
Direct3D resources).

The CPU part holds about 17.2 MB of pools at the sizes below (the Xbox pools
fill 3,165,260 of its 0x305000 bytes); the GPU part holds only the decal
vertices, as on the Xbox. A change to a pool's size changes the game state's
layout: saved games of builds before it no longer load. */

#define HALO_PORT_GAME_STATE_BASE_ADDRESS 0x81A00000 /* (0x80061000) */
#define HALO_PORT_GAME_STATE_CPU_SIZE 0x13C0000 /* (0x305000) */
#define HALO_PORT_GAME_STATE_GPU_SIZE 0x40000 /* (0x40000) */
#define HALO_PORT_GAME_STATE_SIZE (HALO_PORT_GAME_STATE_CPU_SIZE+HALO_PORT_GAME_STATE_GPU_SIZE)

/* ---------- textures

The texture cache holds the textures being drawn in 16 KB pages, 22 MB of
them on the Xbox, which Xbox maps were made to fit. Halo Custom Edition maps
were made for Halo PC, which has no such bound: a texture that does not fit
is drawn as the default one ("YOU GOT STABBED" in debug.txt; Elite_Alpha_Siege
did at 22 MB), and a frame of bigass_v3 draws more than 64 MB (DamnationCE's
measurement). The cache is 256 MB of every build's 512 MB memory window
(port/linux/src/platform.h), whose pages are backed as they are used; it
fills before it lets go of a texture (lruv_block_new), so a map uses as much
of it as its textures take, up to the size it plays with
(texture_cache_size_for_map, xbox_texture_cache.c):

- the desktop builds (x86-64, Windows, and macOS, whose Macs all have the
  8 GB that gives a Custom Edition map the whole below): the whole, for every
  map. A map's first frame is drawn through all of its textures as it loads
  (render_enumerate.c); in 22 MB an Xbox campaign level's were read and sent
  to the GPU again pass after pass (a10's 518 textures, 570 MB read), which
  took most of a second of its loading and left more of them on the GPU;
- the other AArch64 guests (Android, iOS, 64-bit ARM Linux and the Steam
  Frame): an Xbox map the Xbox's 22 MB, which it was made for, so it uses no
  more memory than it did when the cache was that size; a Custom Edition
  map, or one a mod's bitmaps are played over (mod_bitmaps.c), a 32nd of the
  computer's memory, at least 64 MB and at most the whole: 64 MB at 2 GB,
  128 at 4, 192 at 6, the whole 256 from 8 GB (the Steam Frame).
  The GPU holds what the cache does once more (as BC or ASTC blocks, the
  same size or twice it: xgpu_vulkan_textures.c, xgpu_metal_textures.c), so
  a 32nd keeps the two under a tenth of a phone's memory; at 4 GB it is
  twice the 64 MB a frame of bigass_v3 was measured to pass.
  The memory is sysinfo's (the guests' hosts': Linux's, or hw.memsize on
  macOS and iOS, port/macos/host_syscall.c); unknown, it is the least.

The texture cache is not in the game state (its globals and blocks are the C
heap's, xbox_texture_cache.c), and the game state stays where it is: saved
games load whatever these sizes. */

#define HALO_PORT_TEXTURE_CACHE_PAGE_COUNT 0x4000 /* (0x580) */
#define HALO_PORT_TEXTURE_CACHE_SIZE (HALO_PORT_TEXTURE_CACHE_PAGE_COUNT*0x4000) /* (0x1600000) */
/* a Custom Edition map's: the computer's memory over this, in pages from the
least to the whole */
#define HALO_PORT_TEXTURE_CACHE_MEMORY_SHARE 32
#if defined(HALO_GUEST) && defined(__aarch64__)
#if defined(HALO_MACOS) && !defined(HALO_IOS)
#define HALO_PORT_TEXTURE_CACHE_XBOX_MAP_PAGE_COUNT HALO_PORT_TEXTURE_CACHE_PAGE_COUNT
#define HALO_PORT_TEXTURE_CACHE_LEAST_PAGE_COUNT HALO_PORT_TEXTURE_CACHE_PAGE_COUNT
#else
#define HALO_PORT_TEXTURE_CACHE_XBOX_MAP_PAGE_COUNT 0x580 /* (0x580) */
#define HALO_PORT_TEXTURE_CACHE_LEAST_PAGE_COUNT 0x1000
#endif
#else
#define HALO_PORT_TEXTURE_CACHE_XBOX_MAP_PAGE_COUNT HALO_PORT_TEXTURE_CACHE_PAGE_COUNT
#define HALO_PORT_TEXTURE_CACHE_LEAST_PAGE_COUNT HALO_PORT_TEXTURE_CACHE_PAGE_COUNT
#endif

/* ---------- sounds

The sound cache holds the sounds being played in 4 KB pages, 4 MB of them on
the Xbox. Halo Custom Edition maps' sounds, converted when they load
(port/linux/game/custom_edition_sounds.c), and those of sound tags loaded
over a map's by mods (mod_sounds.c), are longer than the Xbox maps' and fill it (a
campaign map's dialogue and music did, "SOUND CACHE BLOWN" in debug.txt).
The desktop builds' cache is 16 MB (the x86-64 guest's too); the AArch64
guests keep the Xbox's, as an Xbox map fills a larger one before it lets
go of a sound, and its size is the same for every map. */

#if defined(HALO_GUEST) && defined(__aarch64__)
#define HALO_PORT_SOUND_CACHE_SIZE 0x400000 /* (0x400000) */
#else
#define HALO_PORT_SOUND_CACHE_SIZE 0x1000000 /* (0x400000) */
#endif

/* ---------- the cache files of decompressed maps

The Xbox copied a map from the disc to one of six cache files on its hard
disk, fixed in size and kind (two campaign levels, the main menu, three
multiplayer maps), before it played it. The native builds keep up to 32
(z:\cacheNNN.map: cache_files_windows.c), any map in any of them, each the
size of its map, within a budget of the disk's space (game.map_cache_megabytes:
port/linux/src/map_cache.c), the least recently played given up first; the
maps likely to be played next are copied into them ahead, in the background
(port/linux/game/map_cache_prefill.c), so that a load does not wait for one to
be decompressed. 32 hold the 24 stock maps and some of the player's own. */

#define HALO_PORT_CACHED_MAP_FILES 32 /* (6) */

/* ---------- particles and lights a frame draws

What one view draws in a frame, past which the rest is not drawn: the runs
of particles that share a definition, and the dynamic lights (an assault
rifle's flashlight, each plasma bolt's light) found in the clusters it sees.
A busy multiplayer fight spends the Xbox's 512 and 128, and plasma bolts,
charge glows and flashlights stop drawing. The desktop builds (x86-64 and
the Mac's) draw four times as many. Only what is drawn changes: these are
rebuilt every frame, outside the game state, and nothing of them reaches
the network, so machines with other limits still play the same game.
Android's and Linux arm64's (boards and phones' cores)
keep the Xbox's. (The transparent geometry groups: below.)

The lights found past the rasterizer's 128 are not lit (MAXIMUM_LIGHTS_PER_
WINDOW, and the enhanced lights' 128-bit masks): more of them are more
lights to choose the 128 from (dynamic_lights.c ranks them, most visible
first) and more lens flares (a plasma bolt's glow), each an occlusion test
and a quad.

The Steam Frame (Linux arm64 with HALO_VR): twice the Xbox's of each, a
first guess to tune on the headset. What each costs, from the code: a
particle run is a draw (build_sprites, the transparent geometry's sort, the
front end's state, the stream, and a Vulkan draw on the render thread, which
on the Frame's cores is likely some 5 to 10 us on each of the two threads);
its particles cost the same either way (8192 in the pool everywhere), and
their effect pixels are the VR effects' (vulkan/README.md: some 0.07 ms a
million soft, 0.13 thick). The runs are split by definition and cluster, so
their number grows slowly with a fight's size: 1024 holds the overflow a
fight shows past 512 and caps the worst case at about 512 more draws
(some 3 to 5 ms, against the 8.3 ms of a 120 Hz frame) rather than 1536.
A light past 128 costs the game's look at it each frame (its function,
color and radius) and its lens flare; 256 doubles the ranking's choice for
128 more of those at most. Paul: in a busy b40 or Blood Gulch fight with
HALO_VR_GPU_TIME=1 and debug.gpu_stats, compare the frame times at these
and at 512/128; raise them if the frame has room, lower the runs if not. */

#if defined(HALO_ARM64_GUEST) && !defined(HALO_MACOS) && !defined(HALO_VR)
#define HALO_PORT_MAXIMUM_RENDERED_PARTICLE_GROUPS 512 /* (512) */
#define HALO_PORT_MAXIMUM_RENDERED_LIGHTS 128 /* (128) */
#elif defined(HALO_ARM64_GUEST) && defined(HALO_VR)
#define HALO_PORT_MAXIMUM_RENDERED_PARTICLE_GROUPS 1024 /* (512) */
#define HALO_PORT_MAXIMUM_RENDERED_LIGHTS 256 /* (128) */
#else
#define HALO_PORT_MAXIMUM_RENDERED_PARTICLE_GROUPS 2048 /* (512) */
#define HALO_PORT_MAXIMUM_RENDERED_LIGHTS 512 /* (128) */
#endif

/* ---------- AI

Network co-op adds enemies for its players (port/linux/game/coop_enemies.c):
the actors, and their knowledge of the units about them (props: with many
players, more each), have room for several times a level's own. */

#define HALO_PORT_MAXIMUM_ACTORS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_PROPS 8192 /* (768) */
#define HALO_PORT_MAXIMUM_SWARMS 128 /* (32) */
#define HALO_PORT_MAXIMUM_SWARM_COMPONENTS 1024 /* (256) */

/* ---------- objects */

#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 8192 /* (2048) */
#define HALO_PORT_OBJECT_MEMORY_POOL_SIZE 0x800000 /* (0x100000) */
/* each of the two reference lists of every cluster partition (collideable
objects, noncollideable objects, lights) */
#define HALO_PORT_MAXIMUM_CLUSTER_REFERENCES 8192 /* (2048) */
#define HALO_PORT_MAXIMUM_RENDERED_OBJECTS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES 1024 /* (256) */
/* objects one explosion can damage */
#define HALO_PORT_MAXIMUM_AREA_OF_EFFECT_OBJECTS 256 /* (64) */
/* script object lists, and the object references they all share. The
lists of a tick's scripts are freed after it (object_list_gc): a Halo PC
map whose scripts test (players) in many places a tick took more than the
Xbox's 48 (coldsnap's), and its game halted */
#define HALO_PORT_MAXIMUM_OBJECT_LISTS_PER_MAP 1024 /* (48) */
#define HALO_PORT_MAXIMUM_LISTED_OBJECTS_PER_MAP 8192 /* (128) */
/* widgets (light volumes, antennas, flags, glows, lightning), each made with
its object and kept for its life: an assault rifle's flashlight beam, held or
dropped, and a plasma bolt's light volume; a full pool draws the object
without its widget */
#define HALO_PORT_MAXIMUM_WIDGETS 2048 /* (64) */
#define HALO_PORT_MAXIMUM_LIGHT_VOLUMES 2048 /* (256) */

/* ---------- effects, particles, lights and sounds */

#define HALO_PORT_MAXIMUM_EFFECTS 2048 /* (256) */
#define HALO_PORT_MAXIMUM_EFFECT_LOCATIONS 4096 /* (512) */
#define HALO_PORT_MAXIMUM_PARTICLES 8192 /* (1024) */
#define HALO_PORT_MAXIMUM_PARTICLE_SYSTEMS 256 /* (64) */
#define HALO_PORT_MAXIMUM_SYSTEM_PARTICLES 4096 /* (512) */
#define HALO_PORT_MAXIMUM_CONTRAILS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_CONTRAIL_POINTS 8192 /* (1024) */
#define HALO_PORT_MAXIMUM_LIGHTS_PER_MAP 4096 /* (896) */
#define HALO_PORT_MAXIMUM_GAME_LOOPING_SOUNDS 4096 /* (1024) */

/* ---------- rasterizer

A frame's transparent geometry groups: each transparent part of a model drawn,
each widget drawn (light volumes, antennas, flags, glows), each batch of
dynamic geometry (particles, contrails), each transparent surface of the
structure. A full pool draws the frame without the
rest ("too many transparent geometry groups" in debug.txt): a b30 co-op game
with eight times the enemies asked for 464 in a frame. Each group is 160
bytes */
#define HALO_PORT_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS 2048 /* (384) */

#endif /* __HALO_PORT_CAPACITY_H */
