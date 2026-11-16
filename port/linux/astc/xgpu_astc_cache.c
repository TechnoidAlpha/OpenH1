/*
XGPU_ASTC_CACHE.C

The workers that encode the DXT blocks transcoding leaves, and the disk
cache of what they encoded (xgpu_astc.h).

A job is a texture's DXT blocks, copied when it is submitted (the guest may
let go of them or write them meanwhile). A worker hashes them, and looks
for the file of that hash in the cache's folder: a file that is whole, of
this version, for blocks of the same format and count, whose blocks to
encode are the ones transcoding leaves now, gives them; else the worker
encodes them (astc-encoder's "fast" preset), checks the texture against its
DXT texels, and writes the file. A texture that comes out worse than 35 dB
of PSNR (GUARD_ERROR; noise: the rasterizer's "video noise" did, at 29.5
dB) is better left decoded: the job fails, and its file says so, so that
the texture is not encoded again.

Being named by the hash of the texture's blocks, a file is the blocks' own
whatever map, mod or Custom Edition map they came from, and a texture
changed is another file: nothing is to be invalidated. A file of another
version (FILE_VERSION: raise it when the transcoding or the encoding
changes) is a miss and written again. The folder is kept under its most
bytes by deleting its least recently used files (by their times, set when
a file is read), looked over once at the workers' start and whenever a
write takes it past the most.

The file: a header (FILE_MAGIC, FILE_VERSION, the format, the blocks'
count, the count encoded, flags, the hash of the DXT blocks, the hash of
the encoded blocks), then the encoded blocks in their order in the texture.
The others are transcoded again when it is read, which takes far less than
reading them would.
*/

#include "xgpu_astc.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

/* ---------- constants */

#define FILE_MAGIC 0x41314f48u /* "HO1A" */
#define FILE_VERSION 1
/* the file says the texture is better decoded */
#define FILE_FLAG_DECODED 1
/* a texture whose ASTC blocks are further than this from its DXT texels
is left decoded: the mean squared difference over its texels' channels of
a PSNR of 35 dB, 255 * 255 / 10^3.5 */
#define GUARD_ERROR 20.563
/* the workers, at most */
#define WORKERS_MOST 2
#define PATH_BYTES 1024
/* the encoded blocks kept in memory too, to this many bytes: a map's
textures are read again at other addresses (the game's texture cache
moves them), each a texture of its own to the backend, and found here
without a worker (a map's are a few MB) */
#define MEMORY_BYTES (32u << 20)
#define MEMORY_BUCKETS 1024

/* ---------- structures */

struct file_header
{
	uint32_t magic, version;
	uint32_t kind, count, encoded, flags;
	uint64_t key[2];
	uint64_t payload_hash;
};

struct xgpu_astc_job
{
	struct xgpu_astc_job *next;
	int kind;
	uint32_t count;
	uint8_t *dxt, *astc;
	void *owner;
	int cancelled, failed;
};

/* a texture's encoded blocks (those transcoding leaves, in their order),
or that it is better decoded, kept in memory */
struct remembered
{
	struct remembered *next, *older, *newer;
	uint64_t key[2];
	int kind, decoded;
	uint32_t count, encoded;
	uint8_t *blocks;
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t work, idle;
	struct xgpu_astc_job *pending, *pending_last, *finished, *finished_last;
	pthread_t threads[WORKERS_MOST];
	int thread_count, stopping, busy;
	char folder[PATH_BYTES];
	uint64_t most_bytes;
	int scanned;
	struct xgpu_astc_statistics statistics;
	/* the textures' encoded blocks in memory: by their key, and from the
	most recently used (newest) to the least */
	struct remembered *buckets[MEMORY_BUCKETS];
	struct remembered *newest, *oldest;
	uint64_t memory_bytes;
} cache = { .lock = PTHREAD_MUTEX_INITIALIZER, .work = PTHREAD_COND_INITIALIZER, .idle = PTHREAD_COND_INITIALIZER };

/* ---------- hashes */

/* a 64-bit hash of the bytes, a multiply and rotate a word as wyhash's
mixing has it */
static uint64_t hash_bytes(const uint8_t *bytes, size_t size, uint64_t seed)
{
	uint64_t hash = seed ^ (size * 0x9e3779b97f4a7c15ull);
	size_t at;

	for (at = 0; at + 8 <= size; at += 8)
	{
		uint64_t word;

		memcpy(&word, bytes + at, 8);
		hash ^= word * 0xff51afd7ed558ccdull;
		hash = (hash << 31 | hash >> 33) * 0xc4ceb9fe1a85ec53ull;
	}
	for (; at < size; at++)
		hash = (hash ^ bytes[at]) * 0x100000001b3ull;
	hash ^= hash >> 33;
	hash *= 0xff51afd7ed558ccdull;
	hash ^= hash >> 33;
	return hash;
}

/* a texture's key: two 64-bit hashes of its blocks and its format, in one
pass (two chains of multiplies apart run at once); its 128 bits name its
file */
static void texture_key(int kind, const uint8_t *dxt, uint32_t count, uint64_t key[2])
{
	size_t size = (size_t)count * xgpu_astc_dxt_block_bytes(kind), at;
	uint64_t first = (1 + (uint64_t)kind) ^ (size * 0x9e3779b97f4a7c15ull);
	uint64_t second = (0x5bd1e995u + (uint64_t)kind) ^ (size * 0xc2b2ae3d27d4eb4full);

	/* (blocks are 8 or 16 bytes: no bytes past the words) */
	for (at = 0; at + 8 <= size; at += 8)
	{
		uint64_t word;

		memcpy(&word, dxt + at, 8);
		first ^= word * 0xff51afd7ed558ccdull;
		first = (first << 31 | first >> 33) * 0xc4ceb9fe1a85ec53ull;
		second ^= word * 0x9fb21c651e98df25ull;
		second = (second << 27 | second >> 37) * 0x87c37b91114253d5ull;
	}
	first ^= first >> 33;
	first *= 0xff51afd7ed558ccdull;
	key[0] = first ^ (first >> 33);
	second ^= second >> 29;
	second *= 0xc4ceb9fe1a85ec53ull;
	key[1] = second ^ (second >> 32);
}

/* ---------- in memory

With the lock held. */

static struct remembered **remembered_bucket(const uint64_t key[2])
{
	return &cache.buckets[(key[0] ^ key[1] >> 17) % MEMORY_BUCKETS];
}

static struct remembered *remembered_find(int kind, uint32_t count, const uint64_t key[2])
{
	struct remembered *found;

	for (found = *remembered_bucket(key); found; found = found->next)
	{
		if (found->key[0] == key[0] && found->key[1] == key[1] && found->kind == kind && found->count == count)
			return found;
	}
	return NULL;
}

static void remembered_unlink(struct remembered *entry)
{
	if (entry->newer)
		entry->newer->older = entry->older;
	else
		cache.newest = entry->older;
	if (entry->older)
		entry->older->newer = entry->newer;
	else
		cache.oldest = entry->newer;
	entry->older = entry->newer = NULL;
}

static void remembered_newest(struct remembered *entry)
{
	entry->older = cache.newest;
	entry->newer = NULL;
	if (cache.newest)
		cache.newest->newer = entry;
	cache.newest = entry;
	if (!cache.oldest)
		cache.oldest = entry;
}

static void remembered_forget(struct remembered *entry)
{
	struct remembered **link = remembered_bucket(entry->key);

	while (*link != entry)
		link = &(*link)->next;
	*link = entry->next;
	remembered_unlink(entry);
	cache.memory_bytes -= (uint64_t)entry->encoded * XGPU_ASTC_BLOCK_BYTES + sizeof(*entry);
	free(entry->blocks);
	free(entry);
}

/* a job's blocks remembered (decoded: that it is better so): the encoded
ones, gathered by its flags; the least recently used let go past
MEMORY_BYTES */
static void remember(const struct xgpu_astc_job *job, const uint64_t key[2], const uint8_t *encode, uint32_t left,
	int decoded)
{
	struct remembered *entry;
	uint32_t index, at;

	if (remembered_find(job->kind, job->count, key) || (uint64_t)left * XGPU_ASTC_BLOCK_BYTES > MEMORY_BYTES / 4)
		return;
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return;
	if (!decoded && left && !(entry->blocks = malloc((size_t)left * XGPU_ASTC_BLOCK_BYTES)))
	{
		free(entry);
		return;
	}
	for (index = at = 0; entry->blocks && index < job->count; index++)
	{
		if (encode[index])
			memcpy(entry->blocks + (size_t)at++ * XGPU_ASTC_BLOCK_BYTES, job->astc + (size_t)index * XGPU_ASTC_BLOCK_BYTES,
				XGPU_ASTC_BLOCK_BYTES);
	}
	entry->key[0] = key[0];
	entry->key[1] = key[1];
	entry->kind = job->kind;
	entry->count = job->count;
	entry->encoded = decoded ? 0 : left;
	entry->decoded = decoded;
	entry->next = *remembered_bucket(key);
	*remembered_bucket(key) = entry;
	remembered_newest(entry);
	cache.memory_bytes += (uint64_t)entry->encoded * XGPU_ASTC_BLOCK_BYTES + sizeof(*entry);
	while (cache.memory_bytes > MEMORY_BYTES && cache.oldest != entry)
		remembered_forget(cache.oldest);
}

int xgpu_astc_remembered(int kind, const uint8_t *dxt, uint32_t count, uint8_t *astc, const uint8_t *encode,
	uint32_t left)
{
	struct remembered *found;
	uint64_t key[2];
	int result = 0;

	texture_key(kind, dxt, count, key);
	pthread_mutex_lock(&cache.lock);
	found = remembered_find(kind, count, key);
	if (found && (found->decoded || found->encoded == left))
	{
		uint32_t index, at;

		for (index = at = 0; !found->decoded && index < count; index++)
		{
			if (encode[index])
				memcpy(astc + (size_t)index * XGPU_ASTC_BLOCK_BYTES, found->blocks + (size_t)at++ * XGPU_ASTC_BLOCK_BYTES,
					XGPU_ASTC_BLOCK_BYTES);
		}
		remembered_unlink(found);
		remembered_newest(found);
		cache.statistics.memory_hits++;
		result = found->decoded ? -1 : 1;
	}
	pthread_mutex_unlock(&cache.lock);
	return result;
}

/* ---------- the folder */

static void file_path(char *path, size_t size, const uint64_t key[2])
{
	snprintf(path, size, "%s/%016llx%016llx.astc", cache.folder, (unsigned long long)key[0],
		(unsigned long long)key[1]);
}

/* each folder of path made, as mkdir -p; nonzero if it is there */
static int folder_make(const char *path)
{
	char folder[PATH_BYTES];
	char *slash;

	snprintf(folder, sizeof(folder), "%s", path);
	for (slash = strchr(folder + 1, '/'); slash; slash = strchr(slash + 1, '/'))
	{
		*slash = 0;
		mkdir(folder, 0755);
		*slash = '/';
	}
	return mkdir(folder, 0755) == 0 || errno == EEXIST;
}

/* a file's time of modification in nanoseconds (to the second, several
files written or read in one would be in no order) */
static int64_t modified(const struct stat *status)
{
#ifdef __APPLE__
	return (int64_t)status->st_mtimespec.tv_sec * 1000000000 + status->st_mtimespec.tv_nsec;
#else
	return (int64_t)status->st_mtim.tv_sec * 1000000000 + status->st_mtim.tv_nsec;
#endif
}

struct cached_file
{
	char name[64];
	uint64_t size;
	/* its last use, in nanoseconds */
	int64_t time;
};

static int cached_file_older(const void *a, const void *b)
{
	const struct cached_file *first = a, *second = b;

	return first->time < second->time ? -1 : first->time > second->time ? 1 : strcmp(first->name, second->name);
}

/* the folder's files' bytes counted, and the least recently used deleted
until they are no more than keep (keep 0: none deleted); called on a worker
with the lock not held */
static uint64_t folder_trim(uint64_t keep, uint32_t *deleted)
{
	DIR *directory = opendir(cache.folder);
	struct dirent *found;
	struct cached_file *files = NULL;
	size_t count = 0, capacity = 0, index;
	uint64_t total = 0;

	*deleted = 0;
	if (!directory)
		return 0;
	while ((found = readdir(directory)) != NULL)
	{
		char path[PATH_BYTES];
		struct stat status;
		size_t length = strlen(found->d_name);

		/* (the cache's own: the hash and .astc, or a write not finished) */
		if (length < 6 || length >= sizeof(files->name) || (strcmp(found->d_name + length - 5, ".astc") &&
			!strstr(found->d_name, ".astc.")))
			continue;
		snprintf(path, sizeof(path), "%s/%s", cache.folder, found->d_name);
		if (stat(path, &status) || !S_ISREG(status.st_mode))
			continue;
		if (count == capacity)
		{
			struct cached_file *grown = realloc(files, (capacity = capacity ? capacity * 2 : 256) * sizeof(*files));

			if (!grown)
				break;
			files = grown;
		}
		memcpy(files[count].name, found->d_name, length + 1);
		files[count].size = (uint64_t)status.st_size;
		files[count].time = modified(&status);
		total += files[count].size;
		count++;
	}
	closedir(directory);
	if (keep && total > keep && files)
	{
		qsort(files, count, sizeof(*files), cached_file_older);
		for (index = 0; index < count && total > keep; index++)
		{
			char path[PATH_BYTES];

			snprintf(path, sizeof(path), "%s/%s", cache.folder, files[index].name);
			if (!unlink(path))
			{
				total -= files[index].size;
				(*deleted)++;
			}
		}
	}
	free(files);
	return total;
}

uint64_t xgpu_astc_cache_files(const char *folder, int clear)
{
	DIR *directory = folder ? opendir(folder) : NULL;
	struct dirent *found;
	uint64_t total = 0;

	if (!directory)
		return 0;
	while ((found = readdir(directory)) != NULL)
	{
		char path[PATH_BYTES];
		struct stat status;
		size_t length = strlen(found->d_name);

		/* (the cache's own, as folder_trim takes them) */
		if (length < 6 || (strcmp(found->d_name + length - 5, ".astc") && !strstr(found->d_name, ".astc.")))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, found->d_name);
		if (stat(path, &status) || !S_ISREG(status.st_mode))
			continue;
		total += (uint64_t)status.st_size;
		if (clear)
			unlink(path);
	}
	closedir(directory);
	return total;
}

/* the folder kept under its most bytes (a quarter below it once trimmed, so
that a trim is not due at every write after) */
static void folder_bound(void)
{
	uint64_t total, most;
	uint32_t deleted = 0;
	int scan;

	pthread_mutex_lock(&cache.lock);
	most = cache.most_bytes;
	scan = !cache.scanned || cache.statistics.cache_bytes > most;
	cache.scanned = 1;
	pthread_mutex_unlock(&cache.lock);
	if (!*cache.folder || !scan)
		return;
	total = folder_trim(0, &deleted);
	if (most && total > most)
		total = folder_trim(most - most / 4, &deleted);
	pthread_mutex_lock(&cache.lock);
	cache.statistics.cache_bytes = total;
	cache.statistics.evicted += deleted;
	pthread_mutex_unlock(&cache.lock);
}

/* ---------- a job's work */

/* the encoded blocks of the file of key into astc, the job's blocks
transcoded first (encode: their flags); 1 found and whole, -1 found saying
the texture is better decoded, 0 none */
static int file_read(struct xgpu_astc_job *job, const uint64_t key[2], uint8_t *encode)
{
	char path[PATH_BYTES];
	struct file_header header;
	FILE *file;
	uint8_t *encoded = NULL;
	uint32_t left, index, at;
	int result = 0;

	if (!*cache.folder)
		return 0;
	file_path(path, sizeof(path), key);
	file = fopen(path, "rb");
	if (!file)
		return 0;
	if (fread(&header, sizeof(header), 1, file) == 1 && header.magic == FILE_MAGIC &&
		header.version == FILE_VERSION && header.kind == (uint32_t)job->kind && header.count == job->count &&
		header.key[0] == key[0] && header.key[1] == key[1] && header.encoded <= job->count)
	{
		left = xgpu_astc_transcode(job->kind, job->dxt, job->count, job->astc, encode);
		if (header.flags & FILE_FLAG_DECODED)
		{
			result = header.encoded == left ? -1 : 0;
		}
		else if (header.encoded == left &&
			(encoded = malloc((size_t)left * XGPU_ASTC_BLOCK_BYTES + 1)) != NULL &&
			fread(encoded, XGPU_ASTC_BLOCK_BYTES, left, file) == left &&
			hash_bytes(encoded, (size_t)left * XGPU_ASTC_BLOCK_BYTES, 3) == header.payload_hash)
		{
			for (index = at = 0; index < job->count; index++)
			{
				if (encode[index])
					memcpy(job->astc + (size_t)index * XGPU_ASTC_BLOCK_BYTES,
						encoded + (size_t)at++ * XGPU_ASTC_BLOCK_BYTES, XGPU_ASTC_BLOCK_BYTES);
			}
			result = 1;
		}
	}
	fclose(file);
	free(encoded);
	/* (used now: the last to be deleted) */
	if (result)
		utimes(path, NULL);
	return result;
}

/* the file of key written (decoded: saying the texture is better so), by a
temporary file renamed, so that no reader finds it half written */
static void file_write(const struct xgpu_astc_job *job, const uint64_t key[2], const uint8_t *encode, uint32_t left,
	int decoded)
{
	char path[PATH_BYTES], temporary[PATH_BYTES + 32];
	struct file_header header;
	uint8_t *encoded = NULL;
	uint32_t index, at;
	FILE *file;
	size_t size;
	int written;

	if (!*cache.folder)
		return;
	if (!decoded && left)
	{
		encoded = malloc((size_t)left * XGPU_ASTC_BLOCK_BYTES);
		if (!encoded)
			return;
		for (index = at = 0; index < job->count; index++)
		{
			if (encode[index])
				memcpy(encoded + (size_t)at++ * XGPU_ASTC_BLOCK_BYTES, job->astc + (size_t)index * XGPU_ASTC_BLOCK_BYTES,
					XGPU_ASTC_BLOCK_BYTES);
		}
	}
	memset(&header, 0, sizeof(header));
	header.magic = FILE_MAGIC;
	header.version = FILE_VERSION;
	header.kind = (uint32_t)job->kind;
	header.count = job->count;
	header.encoded = left;
	header.flags = decoded ? FILE_FLAG_DECODED : 0;
	header.key[0] = key[0];
	header.key[1] = key[1];
	size = decoded ? 0 : (size_t)left * XGPU_ASTC_BLOCK_BYTES;
	header.payload_hash = hash_bytes(encoded ? encoded : (const uint8_t *)"", size, 3);
	file_path(path, sizeof(path), key);
	snprintf(temporary, sizeof(temporary), "%s.%lx", path, (unsigned long)(uintptr_t)job);
	file = fopen(temporary, "wb");
	written = file && fwrite(&header, sizeof(header), 1, file) == 1 && (!size || fwrite(encoded, 1, size, file) == size);
	if (file && fclose(file))
		written = 0;
	if (written && !rename(temporary, path))
	{
		pthread_mutex_lock(&cache.lock);
		cache.statistics.bytes_written += sizeof(header) + size;
		cache.statistics.cache_bytes += sizeof(header) + size;
		pthread_mutex_unlock(&cache.lock);
	}
	else
	{
		unlink(temporary);
	}
	free(encoded);
}

/* the mean squared difference of the texture's texels from its DXT texels
(the transcoded blocks', within a third of an 8-bit step, taken as none) */
static double encoded_error(struct xgpu_astc_encoder *encoder, const struct xgpu_astc_job *job,
	const uint8_t *encode, uint32_t left)
{
	unsigned block_bytes = xgpu_astc_dxt_block_bytes(job->kind);
	uint8_t *blocks = malloc((size_t)left * XGPU_ASTC_BLOCK_BYTES), *texels = malloc((size_t)left * 64);
	uint8_t reference[64];
	double squares = 0.0;
	uint32_t index, at;
	int texel;

	if (!blocks || !texels)
	{
		free(blocks);
		free(texels);
		return 0.0;
	}
	for (index = at = 0; index < job->count; index++)
	{
		if (encode[index])
			memcpy(blocks + (size_t)at++ * XGPU_ASTC_BLOCK_BYTES, job->astc + (size_t)index * XGPU_ASTC_BLOCK_BYTES,
				XGPU_ASTC_BLOCK_BYTES);
	}
	if (xgpu_astc_decode(encoder, blocks, left, texels))
	{
		for (index = at = 0; index < job->count; index++)
		{
			if (!encode[index])
				continue;
			xgpu_astc_decode_dxt(job->kind, job->dxt + (size_t)index * block_bytes, reference);
			for (texel = 0; texel < 64; texel++)
			{
				double difference = (double)texels[(size_t)at * 64 + texel] - reference[texel];

				squares += difference * difference;
			}
			at++;
		}
	}
	free(blocks);
	free(texels);
	return squares / ((double)job->count * 64.0);
}

static double seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

/* the blocks flagged to encode */
static uint32_t flags_count(const uint8_t *encode, uint32_t count)
{
	uint32_t index, flagged = 0;

	for (index = 0; index < count; index++)
		flagged += encode[index] != 0;
	return flagged;
}

static void job_work(struct xgpu_astc_encoder *encoder, struct xgpu_astc_job *job)
{
	uint64_t key[2];
	uint8_t *encode = malloc(job->count);
	uint32_t left;
	long encoded;
	double started;
	int found;

	job->astc = malloc((size_t)job->count * XGPU_ASTC_BLOCK_BYTES);
	if (!encode || !job->astc || !encoder)
	{
		job->failed = 1;
		free(encode);
		return;
	}
	texture_key(job->kind, job->dxt, job->count, key);
	found = file_read(job, key, encode);
	if (found)
	{
		job->failed = found < 0;
		pthread_mutex_lock(&cache.lock);
		cache.statistics.cache_hits++;
		remember(job, key, encode, flags_count(encode, job->count), job->failed);
		pthread_mutex_unlock(&cache.lock);
		free(encode);
		return;
	}
	started = seconds();
	encoded = xgpu_astc_convert(encoder, job->kind, job->dxt, job->count, job->astc, encode);
	left = encoded > 0 ? (uint32_t)encoded : 0;
	if (encoded < 0)
	{
		job->failed = 1;
	}
	else if (left)
	{
		job->failed = encoded_error(encoder, job, encode, left) > GUARD_ERROR;
		file_write(job, key, encode, left, job->failed);
	}
	pthread_mutex_lock(&cache.lock);
	if (encoded >= 0 && left)
		remember(job, key, encode, left, job->failed);
	cache.statistics.blocks_encoded += left;
	cache.statistics.encode_seconds += seconds() - started;
	pthread_mutex_unlock(&cache.lock);
	free(encode);
}

/* ---------- the workers */

static void *worker(void *argument)
{
	struct xgpu_astc_encoder *encoder;

	(void)argument;
	/* (behind the game's threads) */
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
#endif
	encoder = xgpu_astc_encoder_create(XGPU_ASTC_ENCODER_QUALITY);
	folder_bound();
	for (;;)
	{
		struct xgpu_astc_job *job;
		int skip;

		pthread_mutex_lock(&cache.lock);
		while (!cache.pending && !cache.stopping)
			pthread_cond_wait(&cache.work, &cache.lock);
		if (!cache.pending)
		{
			pthread_mutex_unlock(&cache.lock);
			break;
		}
		job = cache.pending;
		cache.pending = job->next;
		if (!cache.pending)
			cache.pending_last = NULL;
		job->next = NULL;
		skip = job->cancelled;
		cache.busy++;
		pthread_mutex_unlock(&cache.lock);
		if (!skip)
			job_work(encoder, job);
		pthread_mutex_lock(&cache.lock);
		if (cache.finished_last)
			cache.finished_last->next = job;
		else
			cache.finished = job;
		cache.finished_last = job;
		cache.statistics.jobs++;
		cache.busy--;
		if (!cache.pending && !cache.busy)
			pthread_cond_broadcast(&cache.idle);
		pthread_mutex_unlock(&cache.lock);
		if (!skip)
			folder_bound();
	}
	xgpu_astc_encoder_destroy(encoder);
	return NULL;
}

/* the workers started, with the lock held */
static void workers_start(void)
{
	long processors = sysconf(_SC_NPROCESSORS_ONLN);
	int wanted = processors >= 6 ? WORKERS_MOST : 1;

	while (cache.thread_count < wanted)
	{
		pthread_attr_t attributes;
		int made;

		pthread_attr_init(&attributes);
		/* (astc-encoder's working memory is on the heap; its frames are
		small) */
		pthread_attr_setstacksize(&attributes, 1u << 20);
		made = pthread_create(&cache.threads[cache.thread_count], &attributes, worker, NULL) == 0;
		pthread_attr_destroy(&attributes);
		if (!made)
			break;
		cache.thread_count++;
	}
}

/* ---------- public code */

void xgpu_astc_cache_initialize(const char *folder, uint64_t most_bytes)
{
	pthread_mutex_lock(&cache.lock);
	*cache.folder = 0;
	if (folder && *folder && strlen(folder) < sizeof(cache.folder) - 64 && folder_make(folder))
		snprintf(cache.folder, sizeof(cache.folder), "%s", folder);
	cache.most_bytes = most_bytes;
	cache.scanned = 0;
	pthread_mutex_unlock(&cache.lock);
}

struct xgpu_astc_job *xgpu_astc_submit(int kind, const uint8_t *dxt, uint32_t count, void *owner)
{
	size_t size = (size_t)count * xgpu_astc_dxt_block_bytes(kind);
	struct xgpu_astc_job *job;

	if (!count || (kind != XGPU_ASTC_DXT1 && kind != XGPU_ASTC_DXT3 && kind != XGPU_ASTC_DXT5))
		return NULL;
	job = calloc(1, sizeof(*job));
	if (!job)
		return NULL;
	job->dxt = malloc(size);
	if (!job->dxt)
	{
		free(job);
		return NULL;
	}
	memcpy(job->dxt, dxt, size);
	job->kind = kind;
	job->count = count;
	job->owner = owner;
	pthread_mutex_lock(&cache.lock);
	workers_start();
	if (!cache.thread_count)
	{
		pthread_mutex_unlock(&cache.lock);
		free(job->dxt);
		free(job);
		return NULL;
	}
	if (cache.pending_last)
		cache.pending_last->next = job;
	else
		cache.pending = job;
	cache.pending_last = job;
	pthread_cond_signal(&cache.work);
	pthread_mutex_unlock(&cache.lock);
	return job;
}

void xgpu_astc_cancel(struct xgpu_astc_job *job)
{
	if (!job)
		return;
	pthread_mutex_lock(&cache.lock);
	job->cancelled = 1;
	pthread_mutex_unlock(&cache.lock);
}

struct xgpu_astc_job *xgpu_astc_finished(void)
{
	for (;;)
	{
		struct xgpu_astc_job *job;

		pthread_mutex_lock(&cache.lock);
		job = cache.finished;
		if (job)
		{
			cache.finished = job->next;
			if (!cache.finished)
				cache.finished_last = NULL;
			job->next = NULL;
		}
		pthread_mutex_unlock(&cache.lock);
		if (!job || !job->cancelled)
			return job;
		xgpu_astc_job_free(job);
	}
}

void *xgpu_astc_job_owner(const struct xgpu_astc_job *job)
{
	return job->owner;
}

const uint8_t *xgpu_astc_job_blocks(const struct xgpu_astc_job *job)
{
	return job->failed ? NULL : job->astc;
}

void xgpu_astc_job_free(struct xgpu_astc_job *job)
{
	if (!job)
		return;
	free(job->dxt);
	free(job->astc);
	free(job);
}

void xgpu_astc_cache_wait(void)
{
	pthread_mutex_lock(&cache.lock);
	while (cache.pending || cache.busy)
		pthread_cond_wait(&cache.idle, &cache.lock);
	pthread_mutex_unlock(&cache.lock);
}

void xgpu_astc_cache_shutdown(void)
{
	struct xgpu_astc_job *job;
	int index, count;

	pthread_mutex_lock(&cache.lock);
	cache.stopping = 1;
	pthread_cond_broadcast(&cache.work);
	count = cache.thread_count;
	pthread_mutex_unlock(&cache.lock);
	for (index = 0; index < count; index++)
		pthread_join(cache.threads[index], NULL);
	pthread_mutex_lock(&cache.lock);
	while ((job = cache.finished) != NULL)
	{
		cache.finished = job->next;
		xgpu_astc_job_free(job);
	}
	cache.finished_last = NULL;
	while (cache.oldest)
		remembered_forget(cache.oldest);
	cache.thread_count = 0;
	cache.stopping = 0;
	cache.scanned = 0;
	memset(&cache.statistics, 0, sizeof(cache.statistics));
	pthread_mutex_unlock(&cache.lock);
}

void xgpu_astc_statistics(struct xgpu_astc_statistics *statistics)
{
	pthread_mutex_lock(&cache.lock);
	*statistics = cache.statistics;
	pthread_mutex_unlock(&cache.lock);
}
