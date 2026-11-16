"""The saved game's check made on a thread (port/linux/src/saved_game_check.c): the same answer as the game's
check (the header, and the whole file's sum against the header's checksum), made once for a file as it is, ahead of
the menus that ask, and again once the file changes."""
from pathlib import Path
import subprocess

from test_load_trace import build

ROOT = Path(__file__).resolve().parents[1]


def test_saved_game_check_matches_the_games_and_follows_the_file(tmp_path):
    text = (ROOT / "port/linux/src/saved_game_check.c").read_text()
    code = text[text.index("/* the longest header"):]
    source = r'''
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct posix_file_information
{
    unsigned long flags, size_low, size_high, modification_seconds, modification_nanoseconds;
    unsigned long access_seconds, access_nanoseconds, creation_seconds, creation_nanoseconds;
};
static int posix_stat(const char *path, struct posix_file_information *information)
{
    struct stat status;
    if (stat(path, &status) != 0)
        return -1;
    memset(information, 0, sizeof(*information));
    information->size_low = (unsigned long)status.st_size;
    information->modification_seconds = (unsigned long)status.st_mtime;
    return 0;
}
static void platform_translate_path(const char *xbox_path, char *host_path, unsigned long size)
{
    snprintf(host_path, size, "%s", xbox_path);
}
static void platform_log(const char *format, ...) { (void)format; }
static unsigned long long ticks;
static unsigned long long SDL_GetTicks(void) { return ticks; }
static int crc_calls;
static void crc_new(unsigned long *crc) { *crc = 0xFFFFFFFF; }
static void crc_checksum_buffer(unsigned long *crc, void const *buffer, long size)
{
    const unsigned char *bytes = buffer;
    long index;
    int bit;
    __atomic_add_fetch(&crc_calls, 1, __ATOMIC_SEQ_CST);
    for (index = 0; index < size; index++)
    {
        *crc ^= bytes[index];
        for (bit = 0; bit < 8; bit++)
            *crc = (*crc >> 1) ^ (0xEDB88320UL & -(*crc & 1));
    }
}
#define HEADER 64
#define CHECKSUM 60
#define SIZE (1L << 20)

static void write_save(const char *path, unsigned char fill, int good, long seconds)
{
    unsigned char *data = calloc(1, SIZE);
    unsigned long sum;
    unsigned int stored;
    struct timespec times[2];
    FILE *file = fopen(path, "wb");
    memset(data + HEADER, fill, SIZE - HEADER);
    strcpy((char *)data, "levels\\a10\\a10");
    crc_new(&sum);
    crc_checksum_buffer(&sum, data, SIZE);
    if (!good)
        sum ^= 1;
    stored = (unsigned int)sum;
    memcpy(data + CHECKSUM, &stored, sizeof(stored));
    fwrite(data, 1, SIZE, file);
    fclose(file);
    free(data);
    times[0].tv_sec = times[1].tv_sec = seconds;
    times[0].tv_nsec = times[1].tv_nsec = 0;
    utimensat(AT_FDCWD, path, times, 0);
}
''' + code + r'''
int main(void)
{
    const char *path = "SAVE";
    unsigned char header[HEADER];
    int corrupted, calls;

    /* no file: none, at once */
    assert(!saved_game_check(path, header, HEADER, CHECKSUM, SIZE, &corrupted) && !corrupted);
    write_save(path, 7, 1, 1000);
    /* asked for ahead, then by the menu: one check of the file */
    saved_game_check_ahead(path, HEADER, CHECKSUM, SIZE);
    assert(saved_game_check(path, header, HEADER, CHECKSUM, SIZE, &corrupted) && !corrupted);
    assert(!strcmp((char *)header, "levels\\a10\\a10"));
    calls = crc_calls;
    assert(saved_game_check(path, header, HEADER, CHECKSUM, SIZE, &corrupted));
    ticks += 5000;
    saved_game_check_ahead(path, HEADER, CHECKSUM, SIZE);
    assert(saved_game_check(path, header, HEADER, CHECKSUM, SIZE, &corrupted));
    assert(crc_calls == calls);
    /* the file changed (saved again, damaged): checked again */
    write_save(path, 9, 0, 2000);
    ticks += 5000;
    saved_game_check_ahead(path, HEADER, CHECKSUM, SIZE);
    assert(!saved_game_check(path, header, HEADER, CHECKSUM, SIZE, &corrupted) && corrupted);
    assert(crc_calls > calls);
    write_save(path, 9, 1, 3000);
    assert(saved_game_check(path, header, HEADER, CHECKSUM, SIZE, &corrupted) && !corrupted);
    unlink(path);
    return 0;
}
'''
    executable = build(tmp_path, source)
    subprocess.run([str(executable)], check=True, timeout=30, cwd=tmp_path)
    assert not (tmp_path / "SAVE").exists()
