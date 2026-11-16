/*
SAVED_GAME_WRITER.C

A checkpoint's file (z:\savegame.bin, saved games/game_state_xbox.c) written
on a thread of its own. A checkpoint wrote the whole game state to it on the
game's thread, in the middle of a frame; now the game's thread copies the
part of the game state in use and goes on, and this thread writes the copy.

The copy is written beside the file (<file>.new), put on the disk and
renamed over the file, so that the file is always a whole image, the last
one or the one before it, even if the game stops in the middle of a write.
One write at a time: the next checkpoint, and whatever reads the file (a
revert), wait for the one before (saved_game_writer_wait). If the thread
cannot be started, the write is made at once on the caller's.
*/

#include "port_config.h"
#include "load_trace.h"
#include "saved_game_writer.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

enum
{
	_saved_game_writer_not_started,
	_saved_game_writer_running,
	_saved_game_writer_unavailable
};

static struct
{
	pthread_mutex_t lock;
	/* a write handed to the thread, or finished */
	pthread_cond_t changed;
	int thread_state;
	/* the write handed to the thread, not finished yet */
	int pending;
	/* the last write's errno (0: it succeeded) */
	int error;
	int tracing;
	char path[1024];
	void const *image;
	unsigned long size;
} saved_game_writer = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

static double saved_game_writer_now(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return -1.0;
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

/* the image written to the file whole, or the file left as it was: 0, or
the errno that stopped it */
static int saved_game_write_whole(char const *path, void const *image, unsigned long size)
{
	char temporary[sizeof(saved_game_writer.path) + 8];
	unsigned long written = 0;
	int descriptor;
	int error = 0;

	if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary))
		return ENAMETOOLONG;
	descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (descriptor < 0)
		return errno;
	while (!error && written < size)
	{
		ssize_t result = write(descriptor, (char const *)image + written, size - written);

		if (result > 0)
			written += (unsigned long)result;
		else if (result == 0)
			error = EIO;
		else if (errno != EINTR)
			error = errno;
	}
	/* (on the disk before it is renamed: the file is never one whose
	rename reached the disk and whose contents did not) */
	if (!error && fsync(descriptor) != 0)
		error = errno;
	if (close(descriptor) != 0 && !error)
		error = errno;
	if (!error && rename(temporary, path) != 0)
		error = errno;
	if (error)
		unlink(temporary);
	return error;
}

static int saved_game_write_traced(char const *path, void const *image, unsigned long size, int tracing)
{
	double started = tracing ? saved_game_writer_now() : 0.0;
	int error = saved_game_write_whole(path, image, size);

	if (tracing)
		load_trace_log("SAVED_GAME_WRITE elapsed=%.6f size=%lu error=%d path=%s",
			saved_game_writer_now() - started, size, error, path);
	return error;
}

static void *saved_game_writer_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&saved_game_writer.lock);
	for (;;)
	{
		int error;

		while (!saved_game_writer.pending)
			pthread_cond_wait(&saved_game_writer.changed, &saved_game_writer.lock);
		/* (what was handed over stays as it is while the write is pending) */
		pthread_mutex_unlock(&saved_game_writer.lock);
		error = saved_game_write_traced(saved_game_writer.path, saved_game_writer.image, saved_game_writer.size,
			saved_game_writer.tracing);
		pthread_mutex_lock(&saved_game_writer.lock);
		saved_game_writer.error = error;
		saved_game_writer.pending = 0;
		pthread_cond_broadcast(&saved_game_writer.changed);
	}
	return NULL;
}

void saved_game_writer_begin(char const *xbox_path, void const *image, unsigned long size)
{
	char path[sizeof(saved_game_writer.path)];
	int tracing = config_boolean("debug.load_trace") != 0;

	platform_translate_path(xbox_path, path, sizeof(path));
	pthread_mutex_lock(&saved_game_writer.lock);
	while (saved_game_writer.pending)
		pthread_cond_wait(&saved_game_writer.changed, &saved_game_writer.lock);
	if (saved_game_writer.thread_state == _saved_game_writer_not_started)
	{
		pthread_t thread;

		saved_game_writer.thread_state = pthread_create(&thread, NULL, saved_game_writer_thread, NULL) == 0 ?
			_saved_game_writer_running : _saved_game_writer_unavailable;
		if (saved_game_writer.thread_state == _saved_game_writer_running)
			pthread_detach(thread);
	}
	if (saved_game_writer.thread_state != _saved_game_writer_running)
	{
		saved_game_writer.error = saved_game_write_traced(path, image, size, tracing);
		pthread_mutex_unlock(&saved_game_writer.lock);
		return;
	}
	snprintf(saved_game_writer.path, sizeof(saved_game_writer.path), "%s", path);
	saved_game_writer.image = image;
	saved_game_writer.size = size;
	saved_game_writer.tracing = tracing;
	saved_game_writer.error = 0;
	saved_game_writer.pending = 1;
	pthread_cond_broadcast(&saved_game_writer.changed);
	pthread_mutex_unlock(&saved_game_writer.lock);
}

int saved_game_writer_wait(void)
{
	int error;

	pthread_mutex_lock(&saved_game_writer.lock);
	while (saved_game_writer.pending)
		pthread_cond_wait(&saved_game_writer.changed, &saved_game_writer.lock);
	error = saved_game_writer.error;
	pthread_mutex_unlock(&saved_game_writer.lock);
	return error;
}
