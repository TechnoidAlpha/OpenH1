/*
REPORT.C

Save Report: a zip of what helps find a problem, saved in reports/ beside
maps/ (reports/<date>_<time>.zip; where it cannot be written, the home
folder's OpenH1-report-<date>_<time>.zip, its whole path told) for the
player to send to the developers.
Asked for by controls.report (F8, in a game too: xinput_sdl.c) or Settings >
Profiles > REPORT A PROBLEM (menu_functions.c), both report_request, and
saved at the next frame presented (d3d8_device.c: report_write), whose
picture is its screenshot: the moment the key was pressed. Then a new issue
on OpenH1's GitHub repository opens in the browser, its title and body
filled in with report.md, and (on a computer) the reports folder, at its
whole path's file: address (the data root is "." when the game starts in
its own folder): the player says what happened, drags the zip in and
submits it; a browser that does not open, the zip's whole path told. Nothing is
sent by the game itself. Its issue is public, so what could say who the
player is never goes in: settings.txt leaves the private settings out, and
every text in it, and the issue's body, is scrubbed (report_scrub: the
home folder, links, addresses, chat lines, names). Asked for in a game
(report_game_playing), with the key, a note is asked for first; and the
issue waits until the main menu is up (report_game_menus), so that nothing
is opened over the game. A second ask within REPORT_AGAIN_SECONDS of one is
let go (a key pressed twice opens one issue).

The zip holds:
  report.md      a GitHub issue's body, its first lines for the player to
                 fill in (report_issue_url): the build, the platform, the GPU and its driver (the
                 renderer's host's: xgpu_backend_device), the processor, the
                 display's settings, the map and the game, how long the game
                 has run, where the game is (report_where_text: the level, its
                 time and BSP, the player, the network game, the console's
                 lines that look from the camera's place), and the frame times of the last REPORT_SECONDS
                 (report_frame_summary: the frame rate, its 1% and 0.1% lows,
                 the longest frames and when; report_gpu_summary: the GPU's
                 time a frame, how busy the GPU, the game and the renderer's
                 thread were, and what held the frames back)
  log.txt        this run's log (platform_log's lines), its last
                 REPORT_LOG_SIZE bytes, kept here: on Linux and Android the
                 log is not otherwise in a file
  halo.log       the host's log file, where it has one (Windows beside
                 halo.exe, the macOS app's in ~/Library/Logs/OpenH1), its end
  host.log       where it has none (Linux, Android, the macOS executable
                 run by itself): the renderer's host's last lines, which go
                 only to the standard error stream or logcat (its Vulkan
                 device, validation errors, the swapchain's problems), kept
                 by the host (host_log_recent, port/android/host/host_common.c)
  debug.txt      the game's own log, in the data folder
  settings.txt   the settings changed from their defaults (not config.toml:
                 never the passwords, this machine's addresses, keys)
  input.txt      the input trace (input_trace.c): the keyboard's, mouse's
                 and gamepads' events, the window's focus, and what the game
                 read of them, over the last INPUT_TRACE_SECONDS
  screenshot.png the frame shown when it was asked for
  crash.txt      a crash's report: what the host wrote of it, with that
                 run's log, or what is known of a run that did not exit

A crash ends the game before F8 can be pressed, so the next start offers to
report it. Each session writes reports/.running as it starts (its build,
when it began, debug.txt's reference address: report_session_start) and
removes it when the game exits (atexit: the window closed, Quit,
debug.exit_after) or the updater starts the new build, which looks for it
while this one is still quitting (report_session_handover); a crash leaves
it. The host, at a crash, writes
reports/.crash.txt: the crash's lines as it logs them (the signal, the
address, the registers, the frames) and then this run's log kept here, the
ring below, which a signal handler can write as it is (host_crash_record,
port/android/host/host_common.c); a fatal error's halt writes it too
(report_session_halted, main.c), and keeps both at the exit that follows.
The next start finds them, keeps them as
reports/.crashed and .crashed.txt until they are answered, and at the main
menu asks once, "OpenH1 closed unexpectedly last time. Report it?"
(pc_menu_crash_question_open, menu_functions.c): REPORT saves a report
headed by the crash (report_crash_summary), with crash.txt in its zip, and
opens its issue; SKIP never asks of that crash again. On Android and iOS,
whose systems end an app in the background without a word, a crash is one
the host wrote; on a computer, a session that did not exit is one (killed,
or a crash the host could not write). A run nobody watches does not ask: it
logs what the report would say, and debug.crash_answer answers for a test.

A frame's time costs a clock reading and six stores (report_frame_presented,
on the render thread, as report_write is); a log line a copy into the ring.
*/

#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "report.h"
#include "input_trace.h"
#include "xgpu_backend.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int mkdir(const char *path, unsigned int mode);
/* errors.c's: debug.txt's reference address is its own */
void write_to_error_file(char *string, unsigned char date);
/* menu_functions.c's: the crash question opened over the main menu, if it
was drawn last; 1 when it was opened, or cannot be */
int pc_menu_crash_question_open(void);
#ifdef HALO_GUEST
/* port/android/guest/runtime/guest_host.h: the host's last lines, and its
record of a crash */
unsigned int host_log_recent(char *buffer, unsigned int size);
void host_crash_record(const char *path, const char *log, unsigned int log_size,
	const unsigned long long *log_written);
#endif
/* updater.c's: the build, in a few words */
void updater_build_text(char *text, size_t size);
/* p2p_discord.c's: the network game this machine is in, as the game told */
void p2p_discord_game(int *hosting, int *playing, char *description, int size, int *player_count,
	int *maximum_player_count);

/* the frames kept (a power of 2: at 240 frames a second, 4.5 minutes) */
#define REPORT_FRAMES 65536
/* the frames summed: the last 5 minutes */
#define REPORT_SECONDS 300
/* a frame this long (milliseconds) or longer is a hitch */
#define REPORT_HITCH_MS 50
/* (a stall: debug.stall_ms's default, whose samples the host logs:
port/android/host/host_stall.c) */
#define REPORT_STALL_MS 100
/* the longest frames listed */
#define REPORT_LONGEST 8
/* a frame's GPU time not measured (the backend gives none) */
#define REPORT_GPU_UNKNOWN 0xFFFFFFFFu
/* the share of the frames' time (percent) the game waits for vsync or the
frame cap for them to be what holds the frames back, unless a part is busy
for REPORT_BUSY_PERCENT of it (it would hold them back without them) */
#define REPORT_PACED_PERCENT 20
#define REPORT_BUSY_PERCENT 95
/* the fewest frames of GPU time a verdict is given on */
#define REPORT_GPU_FEWEST 30
#define REPORT_LOG_SIZE (512 * 1024)
/* the most of a log file's end taken */
#define REPORT_FILE_MOST (4 * 1024 * 1024)
/* the most of the host's kept lines asked for (the host keeps 64 KB) */
#define REPORT_HOST_LOG_MOST (64 * 1024)
#define REPORT_ZIP_FILES 10
#define REPORT_AGAIN_SECONDS 5
/* the most issues waiting for the game's end */
#define REPORT_PENDING 4

static struct
{
	uint32_t micros[REPORT_FRAMES];
	uint32_t at_ms[REPORT_FRAMES];
	/* the work in each (xgpu_backend_frame_times; the GPU's
	REPORT_GPU_UNKNOWN where not measured) */
	struct report_frame_work work[REPORT_FRAMES];
	unsigned long count;
	long long start_ns, last_ns;
	/* the backend's counts at the last frame, and whether the GPU's was */
	struct xgpu_backend_frame_times last;
	int gpu_last_known;
} report_frames;

static struct
{
	pthread_mutex_t lock;
	char text[REPORT_LOG_SIZE];
	unsigned long long written;
} report_log = { PTHREAD_MUTEX_INITIALIZER, { 0 }, 0 };

/* (1: asked for by a menu's row; 2: by the keyboard's key, which can type
a note) */
static volatile int report_requested;
static int report_asked_by_key;
/* the issues of the reports asked for in a game, opened once it is left
(their addresses, malloc()'s, and the folders their zips are in) */
static struct
{
	char *urls[REPORT_PENDING];
	char folders[REPORT_PENDING][1200];
	short count;
} report_pending;
/* (the last ask let through: another this soon after it is let go) */
static long long report_last_ns;

static int report_open_issue(const char *url, const char *folder);
static int report_unattended(void);

/* the crash of the last run, found as this one started (report_session_start):
its session's file and the host's record of it (NULL if it wrote none),
malloc()'s; whether it is to be asked, has been, and its report wanted */
static struct
{
	int found;
	char *session;
	char *record;
	int asked;
	int wanted;
} report_crash;

static void report_crash_question(void);
static void report_finish_poll(void);

/* the issues waiting opened: the game left, or quit (report_at_exit) */
static void report_open_pending(void)
{
	short index;

	for (index = 0; index < report_pending.count; index++)
	{
		if (!report_open_issue(report_pending.urls[index], report_pending.folders[index]))
			platform_log("report: its issue could not be opened: the report is in %s", report_pending.folders[index]);
		free(report_pending.urls[index]);
	}
	report_pending.count = 0;
}

static long long report_now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/* ---------- debug.frame_log

A line for each frame presented once the scenario's simulation has started
(sim_trace_scenario_seconds: the clock of debug.test_input's scripts), for a
benchmark (tools/bench.py): the scenario's seconds, then microseconds of the
frame's time, the GPU's (-1 not measured), the render thread's playing its
records, the game's waits for the renderer and the display, and the game
thread's CPU (CLOCK_THREAD_CPUTIME_ID, -1 not known). With
debug.frame_log_seconds the game quits once the scenario's clock reaches it,
so that every build runs the same ticks and then stops. */
static FILE *frame_log;
static int frame_log_opened;
static double frame_log_seconds;
static long long frame_log_cpu_ns = -1;

/* port/linux/game/sim_trace.c's */
extern double sim_trace_scenario_seconds(void);

static long long report_thread_cpu_ns(void)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
	struct timespec now;

	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
		return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
#endif
	return -1;
}

static void report_frame_log(long long micros, const struct report_frame_work *work)
{
	long long cpu_ns, cpu;
	double seconds;

	if (!frame_log_opened)
	{
		const char *path = config_string("debug.frame_log");

		frame_log_opened = 1;
		frame_log_seconds = config_real("debug.frame_log_seconds");
		if (path && path[0])
		{
			frame_log = fopen(path, "w");
			if (frame_log)
				fprintf(frame_log, "# frame log 1: seconds frame gpu render waited cpu (microseconds)\n");
			else
				platform_log("debug.frame_log: cannot write %s", path);
		}
	}
	if (!frame_log)
		return;
	cpu_ns = report_thread_cpu_ns();
	cpu = cpu_ns >= 0 && frame_log_cpu_ns >= 0 ? (cpu_ns - frame_log_cpu_ns) / 1000 : -1;
	frame_log_cpu_ns = cpu_ns;
	seconds = sim_trace_scenario_seconds();
	if (seconds < 0.0 || micros < 0)
		return;
	fprintf(frame_log, "%.6f %lld %ld %lu %lu %lld\n", seconds, micros,
		work->gpu == REPORT_GPU_UNKNOWN ? -1L : (long)work->gpu, (unsigned long)work->render,
		(unsigned long)work->waited, cpu);
	if (frame_log_seconds > 0.0 && seconds >= frame_log_seconds - 1e-9)
	{
		fclose(frame_log);
		frame_log = NULL;
		platform_log("debug.frame_log: %.3f seconds of the scenario logged", seconds);
		exit(EXIT_SUCCESS);
	}
}

/* ---------- what is kept */

void report_frame_presented(void)
{
	long long now = report_now_ns();
	struct xgpu_backend_frame_times times = {0};
	int gpu_known = xgpu_backend_frame_times(&times);

	/* (the issues of reports asked for in a game, once the menus are up:
	not the game's end, its loading screens, the next level's) */
	report_finish_poll();
	if (report_pending.count && report_game_menus())
		report_open_pending();
	/* (the last run's crash, asked of at the main menu) */
	if (report_crash.found && !report_crash.asked)
		report_crash_question();

	if (report_frames.last_ns)
	{
		unsigned long index = report_frames.count & (REPORT_FRAMES - 1);
		long long micros = (now - report_frames.last_ns) / 1000;
		struct report_frame_work *work = &report_frames.work[index];

		report_frames.micros[index] = (uint32_t)(micros > 0xFFFFFFFFLL ? 0xFFFFFFFFLL : micros);
		report_frames.at_ms[index] = (uint32_t)((now - report_frames.start_ns) / 1000000);
		/* (the counts' changes since the last frame, modulo 2^32) */
		work->gpu = (uint32_t)times.gpu - (uint32_t)report_frames.last.gpu;
		if (!gpu_known || !report_frames.gpu_last_known)
			work->gpu = REPORT_GPU_UNKNOWN;
		work->render = (uint32_t)times.render - (uint32_t)report_frames.last.render;
		work->waited = (uint32_t)times.waited - (uint32_t)report_frames.last.waited;
		work->paced = (uint32_t)times.paced - (uint32_t)report_frames.last.paced;
		report_frames.count++;
		report_frame_log(micros, work);
	}
	else
		report_frames.start_ns = now;
	report_frames.last_ns = now;
	report_frames.last = times;
	report_frames.gpu_last_known = gpu_known;
}

void report_log_line(const char *line)
{
	size_t length = strlen(line), index;

	pthread_mutex_lock(&report_log.lock);
	for (index = 0; index <= length; index++)
	{
		report_log.text[report_log.written % REPORT_LOG_SIZE] = index < length ? line[index] : '\n';
		report_log.written++;
	}
	pthread_mutex_unlock(&report_log.lock);
}

void report_request(int keyboard)
{
	long long now = report_now_ns();

	if (report_last_ns && now - report_last_ns < REPORT_AGAIN_SECONDS * 1000000000LL)
		return;
	report_last_ns = now;
	report_requested = keyboard ? 2 : 1;
}

int report_take_request(void)
{
	int requested = report_requested;

	report_requested = 0;
	if (requested)
		report_asked_by_key = requested == 2;
	return requested != 0;
}

/* ---------- the frame times */

static int report_compare_micros(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* the frame rate of the slowest share (per mille) of the frames sorted: their
mean time's */
static double report_low_fps(const uint32_t *sorted, unsigned long count, unsigned long per_mille)
{
	unsigned long slowest = count * per_mille / 1000, index;
	double total = 0;

	if (slowest < 1)
		slowest = 1;
	for (index = count - slowest; index < count; index++)
		total += sorted[index];
	return total > 0 ? 1000000.0 * (double)slowest / total : 0;
}

void report_frame_summary(const uint32_t *micros, const uint32_t *at_ms, unsigned long count, uint32_t now_ms,
	char *text, size_t size)
{
	uint32_t *sorted;
	unsigned long index, hitches = 0, stalls = 0, shown;
	double total = 0;
	size_t used = 0;
	unsigned long longest[REPORT_LONGEST];

	if (!size)
		return;
	text[0] = 0;
	if (!count)
	{
		snprintf(text, size, "No frames yet.\n");
		return;
	}
	sorted = malloc(count * sizeof(*sorted));
	if (!sorted)
	{
		snprintf(text, size, "No memory to sum the frames.\n");
		return;
	}
	for (index = 0; index < count; index++)
	{
		sorted[index] = micros[index];
		total += micros[index];
		if (micros[index] >= REPORT_HITCH_MS * 1000)
			hitches++;
		if (micros[index] >= REPORT_STALL_MS * 1000)
			stalls++;
	}
	qsort(sorted, count, sizeof(*sorted), report_compare_micros);
#define REPORT_PRINT(...) \
	(used += (size_t)snprintf(text + (used < size ? used : size - 1), used < size ? size - used : 1, __VA_ARGS__))
	REPORT_PRINT("Frames: %lu in the last %.1f seconds\n", count, total / 1000000.0);
	REPORT_PRINT("Average: %.1f fps (%.2f ms a frame)\n", total > 0 ? 1000000.0 * (double)count / total : 0,
		total / 1000.0 / (double)count);
	REPORT_PRINT("Median frame: %.2f ms\n", sorted[count / 2] / 1000.0);
	REPORT_PRINT("1%% low: %.1f fps\n", report_low_fps(sorted, count, 10));
	REPORT_PRINT("0.1%% low: %.1f fps\n", report_low_fps(sorted, count, 1));
	REPORT_PRINT("Frames of %d ms or more: %lu\n", REPORT_HITCH_MS, hitches);
	/* (where the game was in each: the host log's stall: lines) */
	REPORT_PRINT("Frames of %d ms or more: %lu%s\n", REPORT_STALL_MS, stalls,
		stalls ? " (the stall: lines of halo.log or host.log say where the game was)" : "");
	/* (the longest, the latest first among equals: those of twice the
	median's time or more, a stall) */
	for (shown = 0; shown < REPORT_LONGEST && shown < count; shown++)
	{
		unsigned long best = count;

		for (index = count; index-- > 0;)
		{
			unsigned long taken;

			for (taken = 0; taken < shown && longest[taken] != index; taken++)
				;
			if (taken < shown)
				continue;
			if (best == count || micros[index] > micros[best])
				best = index;
		}
		if (micros[best] < 2 * (uint64_t)sorted[count / 2])
			break;
		longest[shown] = best;
	}
	REPORT_PRINT(shown ? "Longest frames:\n" : "Longest frames: none twice the median or longer\n");
	for (index = 0; index < shown; index++)
	{
		unsigned long frame = longest[index];

		REPORT_PRINT("  %.1f ms, %.1f s ago\n", micros[frame] / 1000.0,
			(now_ms >= at_ms[frame] ? now_ms - at_ms[frame] : 0) / 1000.0);
	}
#undef REPORT_PRINT
	free(sorted);
}

void report_frames_log(double seconds)
{
	uint32_t now_ms = report_frames.start_ns ? (uint32_t)((report_now_ns() - report_frames.start_ns) / 1000000) : 0;
	unsigned long count = report_frames.count < REPORT_FRAMES ? report_frames.count : REPORT_FRAMES;
	unsigned long first = report_frames.count - count, index;
	uint32_t *micros, *at_ms;
	struct report_frame_work *work;
	char *summary;

	while (count && report_frames.at_ms[first & (REPORT_FRAMES - 1)] + (uint32_t)(seconds * 1000.0) < now_ms)
	{
		first++;
		count--;
	}
	micros = malloc((count ? count : 1) * sizeof(*micros));
	at_ms = malloc((count ? count : 1) * sizeof(*at_ms));
	work = malloc((count ? count : 1) * sizeof(*work));
	summary = malloc(4096);
	if (micros && at_ms && work && summary)
	{
		char *line, *next;

		for (index = 0; index < count; index++)
		{
			micros[index] = report_frames.micros[(first + index) & (REPORT_FRAMES - 1)];
			at_ms[index] = report_frames.at_ms[(first + index) & (REPORT_FRAMES - 1)];
			work[index] = report_frames.work[(first + index) & (REPORT_FRAMES - 1)];
		}
		for (index = 0; index < 2 && (index == 0 || count); index++)
		{
			if (index == 0)
				report_frame_summary(micros, at_ms, count, now_ms, summary, 4096);
			else
				report_gpu_summary(micros, work, count, summary, 4096);
			for (line = summary; *line; line = next)
			{
				next = strchr(line, '\n');
				if (next)
					*next++ = 0;
				else
					next = line + strlen(line);
				platform_log("frames: %s", line);
			}
		}
	}
	free(summary);
	free(micros);
	free(at_ms);
	free(work);
}

/* What held the frames back, from their times (micros) and the work in
each (work: the GPU's time on it, REPORT_GPU_UNKNOWN where not measured;
the renderer's thread's, playing its records; the game's waits for the
renderer and the display, and of those the display's pacing and
display.max_fps's). Each part's share of the time: the GPU's, its time
over the measured frames' time; the game's (its thread on the CPU), a
frame's time less its waits; the renderer's thread's, its time; the
pacing's, its waits; each over all the frames' time. The verdict: limited
by vsync or the frame cap if the pacing is REPORT_PACED_PERCENT of the
time or more and no part is busy for REPORT_BUSY_PERCENT of it; else the
busiest of the GPU, the game and the renderer's thread (GPU-bound,
CPU-bound: the game, or the renderer's thread). No verdict on fewer than
REPORT_GPU_FEWEST measured frames. */
void report_gpu_summary(const uint32_t *micros, const struct report_frame_work *work, unsigned long count,
	char *text, size_t size)
{
	uint32_t *sorted;
	unsigned long index, measured = 0;
	double total = 0, measured_total = 0, gpu_total = 0, game_total = 0, render_total = 0, paced_total = 0,
		gpu_share, game_share, render_share, paced_share, busiest;
	size_t used = 0;
	const char *verdict;

	if (!size)
		return;
	text[0] = 0;
	sorted = malloc((count ? count : 1) * sizeof(*sorted));
	if (!sorted)
	{
		snprintf(text, size, "No memory to sum the GPU's times.\n");
		return;
	}
	for (index = 0; index < count; index++)
	{
		uint32_t waited = work[index].waited < micros[index] ? work[index].waited : micros[index];

		total += micros[index];
		game_total += micros[index] - waited;
		paced_total += work[index].paced < waited ? work[index].paced : waited;
		render_total += work[index].render < micros[index] ? work[index].render : micros[index];
		if (work[index].gpu == REPORT_GPU_UNKNOWN)
			continue;
		sorted[measured++] = work[index].gpu;
		measured_total += micros[index];
		gpu_total += work[index].gpu;
	}
#define REPORT_PRINT(...) \
	(used += (size_t)snprintf(text + (used < size ? used : size - 1), used < size ? size - used : 1, __VA_ARGS__))
	if (measured < REPORT_GPU_FEWEST || !(total > 0) || !(measured_total > 0))
	{
		REPORT_PRINT("GPU time: not measured\n");
		free(sorted);
		return;
	}
	qsort(sorted, measured, sizeof(*sorted), report_compare_micros);
	gpu_share = 100.0 * gpu_total / measured_total;
	if (gpu_share > 100.0)
		gpu_share = 100.0;
	game_share = 100.0 * game_total / total;
	render_share = 100.0 * render_total / total;
	paced_share = 100.0 * paced_total / total;
	busiest = gpu_share > game_share ? gpu_share : game_share;
	busiest = busiest > render_share ? busiest : render_share;
	if (paced_share >= REPORT_PACED_PERCENT && busiest < REPORT_BUSY_PERCENT)
		verdict = "limited by vsync or the frame cap";
	else if (gpu_share >= busiest)
		verdict = "GPU-bound";
	else if (game_share >= busiest)
		verdict = "CPU-bound (the game)";
	else
		verdict = "CPU-bound (the renderer's thread)";
	REPORT_PRINT("GPU time: %.2f ms a frame, %.2f ms at the 99th percentile\n", gpu_total / 1000.0 / (double)measured,
		sorted[(measured - 1) * 99 / 100] / 1000.0);
	REPORT_PRINT("Busy: the GPU %.0f%%, the game %.0f%%, the renderer's thread %.0f%% of the time\n", gpu_share,
		game_share, render_share);
	REPORT_PRINT("Waiting for vsync or the frame cap: %.0f%% of the time\n", paced_share);
	REPORT_PRINT("Verdict: %s\n", verdict);
#undef REPORT_PRINT
	free(sorted);
}

/* ---------- the zip (stored, not compressed: the PNG is already) */

uint32_t report_crc32(uint32_t crc, const unsigned char *data, size_t size)
{
	static uint32_t table[256];
	size_t index;

	if (!table[1])
	{
		uint32_t value, bit, entry;

		for (entry = 0; entry < 256; entry++)
		{
			value = entry;
			for (bit = 0; bit < 8; bit++)
				value = value & 1 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
			table[entry] = value;
		}
	}
	crc = ~crc;
	for (index = 0; index < size; index++)
		crc = table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

struct report_zip
{
	FILE *file;
	unsigned short time, date;
	int count;
	struct
	{
		char name[32];
		uint32_t crc, size, offset;
	} entries[REPORT_ZIP_FILES];
	int failed;
};

static void report_put16(FILE *file, unsigned int value)
{
	fputc((int)(value & 0xFF), file);
	fputc((int)((value >> 8) & 0xFF), file);
}

static void report_put32(FILE *file, uint32_t value)
{
	report_put16(file, value & 0xFFFF);
	report_put16(file, value >> 16);
}

static void report_zip_add(struct report_zip *zip, const char *name, const void *data, size_t size)
{
	size_t length = strlen(name);
	long offset = ftell(zip->file);

	if (zip->count >= REPORT_ZIP_FILES || length >= sizeof(zip->entries[0].name) || offset < 0 ||
		size > 0x7FFFFFFF)
	{
		zip->failed = 1;
		return;
	}
	snprintf(zip->entries[zip->count].name, sizeof(zip->entries[0].name), "%s", name);
	zip->entries[zip->count].crc = report_crc32(0, data, size);
	zip->entries[zip->count].size = (uint32_t)size;
	zip->entries[zip->count].offset = (uint32_t)offset;
	report_put32(zip->file, 0x04034B50u);
	report_put16(zip->file, 10);
	report_put16(zip->file, 0);
	report_put16(zip->file, 0);
	report_put16(zip->file, zip->time);
	report_put16(zip->file, zip->date);
	report_put32(zip->file, zip->entries[zip->count].crc);
	report_put32(zip->file, (uint32_t)size);
	report_put32(zip->file, (uint32_t)size);
	report_put16(zip->file, (unsigned int)length);
	report_put16(zip->file, 0);
	fwrite(name, 1, length, zip->file);
	if (size && fwrite(data, 1, size, zip->file) != size)
		zip->failed = 1;
	zip->count++;
}

static int report_zip_end(struct report_zip *zip)
{
	long start = ftell(zip->file), end;
	int index;

	for (index = 0; index < zip->count; index++)
	{
		size_t length = strlen(zip->entries[index].name);

		report_put32(zip->file, 0x02014B50u);
		report_put16(zip->file, 20);
		report_put16(zip->file, 10);
		report_put16(zip->file, 0);
		report_put16(zip->file, 0);
		report_put16(zip->file, zip->time);
		report_put16(zip->file, zip->date);
		report_put32(zip->file, zip->entries[index].crc);
		report_put32(zip->file, zip->entries[index].size);
		report_put32(zip->file, zip->entries[index].size);
		report_put16(zip->file, (unsigned int)length);
		report_put16(zip->file, 0);
		report_put16(zip->file, 0);
		report_put16(zip->file, 0);
		report_put16(zip->file, 0);
		report_put32(zip->file, 0);
		report_put32(zip->file, zip->entries[index].offset);
		fwrite(zip->entries[index].name, 1, length, zip->file);
	}
	end = ftell(zip->file);
	report_put32(zip->file, 0x06054B50u);
	report_put16(zip->file, 0);
	report_put16(zip->file, 0);
	report_put16(zip->file, (unsigned int)zip->count);
	report_put16(zip->file, (unsigned int)zip->count);
	report_put32(zip->file, (uint32_t)(end - start));
	report_put32(zip->file, (uint32_t)start);
	report_put16(zip->file, 0);
	return !zip->failed && start >= 0 && !ferror(zip->file);
}

/* ---------- what is kept out of a report (its issue is public) */

/* whether a setting ("server.password") is one a report leaves out: a
password, an address or a name of this machine's or its owner's, a key, a
file's or a folder's path (the account's name in it), a server's words */
static int report_setting_private(const char *name)
{
	static const char *const named[] = {
		"network.address", "network.public_address", "server.master_publish_address", "server.web_bind",
		"server.web_trusted_proxy", "server.name", "server.motd", "server.admins", "server.instance",
		"network.broadcast", "network.front", "debug.update_key", "debug.update_url", "debug.update_from",
		"debug.friends", "debug.party", "debug.network_test_name", "debug.discord_socket", "debug.matchmaking_host",
		/* (a device's name may be its owner's: "Keyes's AirPods") */
		"audio.output_device", "audio.input_device",
		/* (files written, wherever they were put) */
		"server.vpn_list", "debug.trace", "debug.play_log", "debug.audio_record", "debug.latency_log",
		"debug.determinism_log",
	};
	size_t index, length = strlen(name);

	if (strstr(name, "password") || !strncmp(name, "paths.", 6) || (length > 5 && !strcmp(name + length - 5, "_file")) ||
		(length > 7 && !strcmp(name + length - 7, "_folder")) ||
		(length > 10 && !strcmp(name + length - 10, "_directory")))
		return 1;
	for (index = 0; index < sizeof(named) / sizeof(named[0]); index++)
	{
		if (!strcmp(name, named[index]))
			return 1;
	}
	return 0;
}

/* an IPv4 address at text (none of 127.0.0.0/8's, nor 0.0.0.0): its length,
0 if it is not one */
static size_t report_address_length(const char *text, size_t left)
{
	size_t at = 0;
	int part, value, digits, loopback = 0, zero = 1;

	for (part = 0; part < 4; part++)
	{
		if (part)
		{
			if (at >= left || text[at] != '.')
				return 0;
			at++;
		}
		for (value = 0, digits = 0; at < left && text[at] >= '0' && text[at] <= '9' && digits < 4; at++, digits++)
			value = value * 10 + (text[at] - '0');
		if (!digits || digits > 3 || value > 255)
			return 0;
		if (!part)
			loopback = value == 127;
		zero = zero && !value;
	}
	if (at < left && ((text[at] >= '0' && text[at] <= '9') || (text[at] == '.' && at + 1 < left &&
		text[at + 1] >= '0' && text[at + 1] <= '9')))
		return 0;
	return loopback || zero ? 0 : at;
}

static int report_hex_digit(char character)
{
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
		(character >= 'A' && character <= 'F');
}

static int report_word_character(char character)
{
	return report_hex_digit(character) || (character >= 'g' && character <= 'z') ||
		(character >= 'G' && character <= 'Z') || character == '_';
}

/* an IPv6 address at text: groups of 1 to 4 hex digits between colons, 8
of them, or 2 or more with a "::" (not ::1, nor ::); its length, 0 if it is
not one. A time (12:30:45) has too few groups, and an IPv4 address's end
("::ffff:1.2.3.4") is left to report_address_length */
static size_t report_address6_length(const char *text, size_t left)
{
	size_t at = 0, group_start = 0;
	int groups = 0, colons = 0, doubled = 0, digits;

	for (;;)
	{
		group_start = at;
		for (digits = 0; at < left && report_hex_digit(text[at]) && digits < 5; at++)
			digits++;
		if (digits > 4)
			return 0;
		if (digits)
			groups++;
		if (at + 1 < left && text[at] == ':' && report_hex_digit(text[at + 1]) && digits)
		{
			at++;
			colons++;
			continue;
		}
		if (at + 1 < left && text[at] == ':' && text[at + 1] == ':' && !doubled)
		{
			at += 2;
			colons += 2;
			doubled = 1;
			continue;
		}
		break;
	}
	/* (a word's start, or an IPv4 address's: not this address's) */
	if (at < left && (report_word_character(text[at]) || text[at] == '.'))
	{
		if (at < left && text[at] == '.' && groups > 0 && at > group_start)
		{
			at = group_start;
			groups--;
		}
		else
			return 0;
	}
	if (doubled ? groups < 2 : groups != 8 || colons != 7)
		return 0;
	return at;
}

/* a marker of what a log line goes on to name (a player, an account, a
code), and whether the rest of the line goes (1) or a word (0) */
static const struct
{
	const char *marker;
	int rest;
} report_markers[] = {
	/* (the host's notices, as the clients' chat and the host log them:
	their names) */
	{ "chat notice: ", 1 },
	{ "host notice: ", 1 },
	{ "chat filter: a line of ", 1 },
	/* (player_link.c: the Discord account linked, its link's code) */
	{ "Discord link: linked as ", 1 },
	{ "the page shows the code ", 0 },
	/* (p2p.c: the address this machine is offered at, which may be a
	host's name) */
	{ "network.public_address ", 0 },
	{ "network.public_address, ", 0 },
};

size_t report_whole_lines(const char *text, size_t size, int cut)
{
	size_t at = 0;

	if (!cut)
		return 0;
	while (at < size && text[at] != '\n')
		at++;
	return at < size ? at + 1 : size;
}

char *report_scrub(const char *text, size_t size, const char *home, size_t *scrubbed_size)
{
	size_t room = size * 2 + 4096, used = 0, at = 0, home_length = home ? strlen(home) : 0;
	char *out = malloc(room);
	/* the account's name, the home folder's last part: written as (user)
	where it is a part of a path outside the home folder too (a data
	folder on another disk, "-Users-<name>-" in a folder's name) */
	const char *account = NULL;
	size_t account_length = 0;

	*scrubbed_size = 0;
	if (!out)
		return NULL;
	if (home_length)
	{
		size_t last = home_length;

		while (last > 0 && (home[last - 1] == '/' || home[last - 1] == '\\'))
			last--;
		account_length = last;
		while (last > 0 && home[last - 1] != '/' && home[last - 1] != '\\')
			last--;
		account = home + last;
		account_length -= last;
		if (account_length < 2)
			account_length = 0;
	}
#define REPORT_PUT(data, length) 	do { if (used + (length) < room) { memcpy(out + used, (data), (length)); used += (length); } } while (0)
	while (at < size)
	{
		size_t end = at, index, marker;
		const char *line = text + at, *chat;

		while (end < size && text[end] != '\n')
			end++;
		/* (a chat line, as debug.txt logs it: "chat: name: text", "chat
		(team): ..."; not the chat's own lines, "chat: the on-screen
		keyboard opened", which name nobody) */
		chat = NULL;
		for (index = at; index + 6 <= end && !chat; index++)
		{
			if (!memcmp(text + index, "chat", 4) && (index == at || text[index - 1] == ' ') &&
				(!memcmp(text + index + 4, ": ", 2) || !memcmp(text + index + 4, " (", 2)))
			{
				size_t colon, colons = 0;

				for (colon = index + 4; colon + 2 <= end && colons < 2; colon++)
					colons += text[colon] == ':' && text[colon + 1] == ' ';
				if (colons == 2)
					chat = text + index;
			}
		}
		if (chat)
		{
			REPORT_PUT(line, (size_t)(chat - line));
			REPORT_PUT("chat: (a chat line, removed from the report)", 44);
			if (end < size)
				REPORT_PUT("\n", 1);
			at = end + 1;
			continue;
		}
		for (index = at; index < end;)
		{
			size_t length;

			for (marker = 0; marker < sizeof(report_markers) / sizeof(report_markers[0]); marker++)
			{
				length = strlen(report_markers[marker].marker);
				if (end - index >= length && !memcmp(text + index, report_markers[marker].marker, length))
					break;
			}
			/* (what a marker names) */
			if (marker < sizeof(report_markers) / sizeof(report_markers[0]))
			{
				REPORT_PUT(text + index, length);
				REPORT_PUT("(removed)", 9);
				index += length;
				while (index < end && (report_markers[marker].rest || (text[index] != ' ' && text[index] != '\t' &&
					text[index] != ')' && text[index] != ',' && text[index] != '"')))
					index++;
			}
			/* (the home folder: the account's name) */
			else if (home_length > 1 && end - index >= home_length && !memcmp(text + index, home, home_length))
			{
				REPORT_PUT("~", 1);
				index += home_length;
			}
			/* (the account's name, a part of a path elsewhere) */
			else if (account_length && index > at && (text[index - 1] == '/' || text[index - 1] == '\\' ||
				text[index - 1] == '-') && end - index >= account_length &&
				!memcmp(text + index, account, account_length) && (index + account_length == end ||
				text[index + account_length] == '/' || text[index + account_length] == '\\' ||
				text[index + account_length] == '-'))
			{
				REPORT_PUT("(user)", 6);
				index += account_length;
			}
			/* (an invite's or a party's link) */
			else if (end - index >= 9 && !memcmp(text + index, "openh1://", 9))
			{
				REPORT_PUT("openh1://(removed)", 18);
				index += 9;
				while (index < end && text[index] != ' ' && text[index] != '\t' && text[index] != '"' &&
					text[index] != ')')
					index++;
			}
			else if ((index == at || !((text[index - 1] >= '0' && text[index - 1] <= '9') || text[index - 1] == '.')) &&
				(length = report_address_length(text + index, end - index)) != 0)
			{
				REPORT_PUT("x.x.x.x", 7);
				index += length;
			}
			else if ((index == at || !(report_word_character(text[index - 1]) || text[index - 1] == ':' ||
				text[index - 1] == '.')) && (length = report_address6_length(text + index, end - index)) != 0)
			{
				REPORT_PUT("x::x", 4);
				index += length;
			}
			else
			{
				REPORT_PUT(text + index, 1);
				index++;
			}
		}
		if (end < size)
			REPORT_PUT("\n", 1);
		at = end + 1;
	}
#undef REPORT_PUT
	/* (ended as a string too: the issue's body is one) */
	out[used] = 0;
	*scrubbed_size = used;
	return out;
}

/* the account's folder, which report_scrub writes as ~ */
static const char *report_home(void)
{
#ifdef HALO_WINDOWS
	if (getenv("USERPROFILE"))
		return getenv("USERPROFILE");
#endif
	return getenv("HOME");
}

/* a text file's part of the zip, scrubbed (report_scrub) */
static void report_zip_add_text(struct report_zip *zip, const char *name, const char *data, size_t size)
{
	size_t scrubbed_size;
	char *scrubbed;

	scrubbed = report_scrub(data, size, report_home(), &scrubbed_size);
	if (scrubbed)
	{
		report_zip_add(zip, name, scrubbed, scrubbed_size);
		free(scrubbed);
	}
}

/* settings.txt: each setting changed from its default, as config.toml has
it, but those report_setting_private names (never written); free() it */
static char *report_settings_text(size_t *size)
{
	struct config_setting_info info;
	size_t room = 64 * 1024, used = 0;
	char *text = malloc(room), value[300];
	int index;

	*size = 0;
	if (!text)
		return NULL;
	used = (size_t)snprintf(text, room, "# The settings changed from their defaults (config.toml), but the passwords, "
		"this machine's addresses, keys, names and paths, which a report leaves out\n");
	for (index = 0; config_setting_info(index, &info) && used < room; index++)
	{
		if (!info.name || report_setting_private(info.name) || !config_text(info.name, value, sizeof(value)) ||
			!strcmp(value, info.default_text))
			continue;
		/* (a number as its default, written another way: "0" for "0.0") */
		if (info.type && (!strcmp(info.type, "real") || !strcmp(info.type, "integer")) &&
			strtod(value, NULL) == strtod(info.default_text, NULL))
			continue;
		used += (size_t)snprintf(text + used, room - used, "%s = %s\n", info.name, value);
	}
	*size = used < room ? used : room - 1;
	return text;
}

/* ---------- the report's parts */

/* a file's end (its last most bytes), or NULL; free() it */
static char *report_file_end(const char *path, size_t most, size_t *size)
{
	FILE *file = fopen(path, "rb");
	long length;
	char *data;

	*size = 0;
	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0)
	{
		fclose(file);
		return NULL;
	}
	if ((size_t)length > most)
		fseek(file, length - (long)most, SEEK_SET);
	else
		fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length > most ? most : (size_t)length + 1);
	if (data)
		*size = fread(data, 1, (size_t)length > most ? most : (size_t)length, file);
	fclose(file);
	return data;
}

/* a log file's end (its last most bytes), or NULL, from its first whole
line (report_whole_lines); free() it */
static char *report_log_file_end(const char *path, size_t most, size_t *size)
{
	char *data = report_file_end(path, most, size);
	size_t skip;

	if (data && *size >= most)
	{
		skip = report_whole_lines(data, *size, 1);
		memmove(data, data + skip, *size - skip);
		*size -= skip;
	}
	return data;
}

/* the log kept, oldest first, from its first whole line; free() it */
static char *report_log_text(size_t *size)
{
	char *text;
	size_t kept, index;

	pthread_mutex_lock(&report_log.lock);
	kept = report_log.written < REPORT_LOG_SIZE ? (size_t)report_log.written : REPORT_LOG_SIZE;
	text = malloc(kept + 1);
	if (text)
	{
		for (index = 0; index < kept; index++)
			text[index] = report_log.text[(report_log.written - kept + index) % REPORT_LOG_SIZE];
	}
	pthread_mutex_unlock(&report_log.lock);
	*size = 0;
	if (text)
	{
		size_t skip = report_whole_lines(text, kept, kept == REPORT_LOG_SIZE);

		memmove(text, text + skip, kept - skip);
		*size = kept - skip;
	}
	return text;
}

static const char *report_platform(void)
{
#if defined(HALO_IOS)
	return "iOS";
#elif defined(HALO_MACOS)
	return "macOS";
#elif defined(HALO_ANDROID)
	return "Android";
#elif defined(HALO_WINDOWS)
	return "Windows";
#elif defined(HALO_VR) && defined(__aarch64__)
	return "Linux arm64, VR";
#elif defined(HALO_VR)
	return "Linux x86-64, VR";
#elif defined(__aarch64__)
	return "Linux arm64";
#else
	return "Linux x86-64";
#endif
}

/* the processor's name (Linux's and Android's /proc/cpuinfo) */
static void report_processor(char *text, size_t size)
{
	FILE *file = fopen("/proc/cpuinfo", "r");
	char line[256];
	long cores = sysconf(_SC_NPROCESSORS_ONLN);

	text[0] = 0;
	while (file && fgets(line, sizeof(line), file))
	{
		char *colon = strchr(line, ':');

		if (colon && (!strncmp(line, "model name", 10) || !strncmp(line, "Hardware", 8)))
		{
			colon++;
			while (*colon == ' ' || *colon == '\t')
				colon++;
			colon[strcspn(colon, "\r\n")] = 0;
			snprintf(text, size, "%s", colon);
			break;
		}
	}
	if (file)
		fclose(file);
	if (cores > 0)
	{
		size_t used = strlen(text);

		snprintf(text + used, size - used, "%s%ld logical cores", used ? ", " : "", cores);
	}
	if (!text[0])
		snprintf(text, size, "unknown");
}

/* whether a file of a maps folder is a map counted (a cache file, not a
resource map nor a download unfinished) */
int report_map_counted(const char *name)
{
	static const char *const resource_maps[] = { "bitmaps.map", "sounds.map", "loc.map" };
	size_t length = strlen(name), index, at;

	if (length <= 4 || name[0] == '.')
		return 0;
	for (index = 0; index < 4; index++)
	{
		char character = name[length - 4 + index];

		if ((character >= 'A' && character <= 'Z' ? character + 32 : character) != ".map"[index])
			return 0;
	}
	for (index = 0; index < sizeof(resource_maps) / sizeof(resource_maps[0]); index++)
	{
		const char *resource = resource_maps[index];

		for (at = 0; at < length && resource[at] &&
			(name[at] >= 'A' && name[at] <= 'Z' ? name[at] + 32 : name[at]) == resource[at]; at++)
			;
		if (at == length && !resource[at])
			return 0;
	}
	return 1;
}

/* how many maps a folder has (report_map_counted), -1 if it has none */
static long report_maps_in(const char *folder)
{
	void *directory = posix_directory_open(folder);
	char name[256];
	long count = 0;

	if (!directory)
		return -1;
	while (posix_directory_next(directory, name, sizeof(name)))
		count += report_map_counted(name);
	posix_directory_close(directory);
	return count;
}

/* report.md's "## Where": the game, its time, the player, the network game,
and the console's lines that take a developer there (the map loaded, its
BSP, the camera: debug_camera_set, camera_pose.c) */
void report_where_text(const struct report_where *where, char *text, size_t size)
{
	static const char *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };
	/* (game_difficulty_set's names: game.c's) */
	static const char *const difficulty_names[] = { "easy", "normal", "hard", "impossible" };
	size_t used = 0;
	int difficulty = where->difficulty >= 0 && where->difficulty < 4 ? where->difficulty : -1;
	int index;

	if (!size)
		return;
	text[0] = 0;
	if (!where->playing)
		return;
#define WHERE_PRINT(...) \
	(used += (size_t)snprintf(text + (used < size ? used : size - 1), used < size ? size - used : 1, __VA_ARGS__))
	WHERE_PRINT("\n## Where\n\n");
	WHERE_PRINT("- Game: %s", where->game[0] ? where->game : where->map);
	if (where->campaign && difficulty >= 0)
		WHERE_PRINT(", %s", difficulties[difficulty]);
	WHERE_PRINT("\n- Time: %ld ticks (%.1f s), BSP %d\n", where->ticks,
		where->ticks_per_second > 0 ? (double)where->ticks / where->ticks_per_second : 0.0, where->bsp);
	if (where->player)
	{
		if (!where->alive)
			WHERE_PRINT("- Player: dead\n");
		else
			WHERE_PRINT("- Player: at %.3f %.3f %.3f, facing %.1f yaw %.1f pitch, %s%s\n", where->position[0],
				where->position[1], where->position[2], where->yaw, where->pitch, where->vehicle[0] ? "in " : "on foot",
				where->vehicle);
	}
	if (where->network)
	{
		WHERE_PRINT("- Network: %s, ", where->network == 1 ? "hosting" : "joined");
		if (where->rate > 0)
			WHERE_PRINT("GOAT H1 %ld Tick", where->rate);
		else
			WHERE_PRINT("Legacy");
		WHERE_PRINT(", %d player%s", where->players, where->players == 1 ? "" : "s");
		if (where->ping >= 0)
			WHERE_PRINT(", ping %ld ms%s", where->ping, where->network == 1 ? " (the slowest client's)" : "");
		if (where->connection_problem)
			WHERE_PRINT(", the host silent");
		WHERE_PRINT("\n");
		if (where->build_kinds > 0 || where->builds_unknown > 0)
		{
			WHERE_PRINT("- Other machines' builds:");
			for (index = 0; index < where->build_kinds && index < REPORT_WHERE_BUILDS; index++)
			{
				if (where->builds[index] == REPORT_WHERE_OPENCE)
					WHERE_PRINT("%s OpenCE", index ? "," : "");
				else if (where->builds[index] > 0)
					WHERE_PRINT("%s build-%ld", index ? "," : "", where->builds[index]);
				else
					WHERE_PRINT("%s a local build", index ? "," : "");
				if (where->build_counts[index] > 1)
					WHERE_PRINT(" x%ld", where->build_counts[index]);
			}
			if (where->builds_unknown > 0)
				WHERE_PRINT("%s %d not told", where->build_kinds > 0 ? "," : "", where->builds_unknown);
			WHERE_PRINT("\n");
		}
	}
	if (where->camera && where->map[0])
	{
		/* (the script's camera takes a field of view under 90 degrees:
		camera_pose_set) */
		float field_of_view = where->camera_field_of_view > 0.0f && where->camera_field_of_view < 89.0f ?
			where->camera_field_of_view : 89.0f;

		WHERE_PRINT("\nTo look from there, in the developer console (`), the last two once the map has loaded "
			"(`debug_camera_set off` gives the camera back):\n\n```\n");
		if (where->campaign && difficulty >= 0)
			WHERE_PRINT("game_difficulty_set %s\n", difficulty_names[difficulty]);
		WHERE_PRINT("map_name %s\nswitch_bsp %d\ndebug_camera_set \"%.3f %.3f %.3f %.1f %.1f %.1f %.1f\"\n```\n",
			where->map, where->bsp, where->camera_position[0], where->camera_position[1],
			where->camera_position[2], where->camera_yaw, where->camera_pitch, where->camera_roll, field_of_view);
	}
#undef WHERE_PRINT
}

/* report.md: a GitHub issue's body (report_send pastes it); a crash's has
the system and settings alone, this run's time, game and frames being of no
help (report_crash_summary heads it) */
static char *report_text(const char *name, int crash)
{
	static const char *const settings[] = {
		"display.graphics", "display.mode", "display.resolution", "display.window_size", "display.resolution_scaling",
		"display.vsync", "display.max_fps", "display.low_latency", "display.anti_aliasing", "display.hdr",
		"display.refresh_rate", "game.sim_rate", "network.online",
	};
	size_t size = 64 * 1024, used = 0, index;
	char *text = malloc(size), line[2048], build[160];
	struct report_where where;
	uint32_t *micros, *at_ms;
	struct report_frame_work *work;
	unsigned long count, first;
	int hosting = 0, playing = 0, players = 0, most = 0;
	long long now = report_now_ns();
	uint32_t now_ms = report_frames.start_ns ? (uint32_t)((now - report_frames.start_ns) / 1000000) : 0;

	if (!text)
		return NULL;
	memset(&where, 0, sizeof(where));
	if (!crash)
		report_game_where(&where);
#define REPORT_PRINT(...) \
	(used += (size_t)snprintf(text + (used < size ? used : size - 1), used < size ? size - used : 1, __VA_ARGS__))
	REPORT_PRINT("## System\n\n");
	updater_build_text(build, sizeof(build));
	REPORT_PRINT("- Build: %s\n", build);
	REPORT_PRINT("- Platform: %s\n", report_platform());
	REPORT_PRINT("- GPU: %s\n", xgpu_backend_device()[0] ? xgpu_backend_device() : "unknown (no renderer)");
	report_processor(line, sizeof(line));
	REPORT_PRINT("- Processor: %s\n", line);
	if (!crash)
		REPORT_PRINT("- Running for: %.0f seconds\n", report_frames.start_ns ? (now - report_frames.start_ns) / 1e9 : 0.0);
	/* (in a game, its own section says where; in the menus, which, and a
	network game's lobby) */
	if (!crash && !where.playing)
	{
		report_game_describe(line, sizeof(line));
		REPORT_PRINT("- Where: %s\n", line);
		p2p_discord_game(&hosting, &playing, line, (int)sizeof(line), &players, &most);
		if (line[0])
			REPORT_PRINT("- Network game: %s, %s, %d of %d players\n", line, hosting ? "hosting" : "joined", players,
				most);
	}
	REPORT_PRINT("- Report: %s\n", name);
	report_where_text(&where, line, sizeof(line));
	REPORT_PRINT("%s", line);
	/* (the mods and the map: a problem one of them makes not taken for the
	game's; names only, never a folder's path) */
	{
		char *mods = malloc(4096), folder[1024], *map_line;
		long stock, custom;

		if (mods)
		{
			report_game_mods_and_maps(mods, 4096);
			/* (a crash's: not this run's map, the menus', which is not the
			one that crashed) */
			if (crash && (map_line = strstr(mods, "- Map: ")) != NULL)
			{
				char *rest = strchr(map_line, '\n');

				rest = rest ? rest + 1 : map_line + strlen(map_line);
				memmove(map_line, rest, strlen(rest) + 1);
			}
			REPORT_PRINT("\n## Mods and maps\n\n%s", mods);
			free(mods);
		}
		snprintf(folder, sizeof(folder), "%s/maps", platform_data_root());
		stock = report_maps_in(folder);
		/* (map_import_custom_maps_folder's choice; map_import.c is the
		desktop's, not Android's) */
		if (config_string("paths.custom_maps")[0])
			snprintf(folder, sizeof(folder), "%s", config_string("paths.custom_maps"));
		else
			snprintf(folder, sizeof(folder), "%s/custom_maps", platform_data_root());
		custom = report_maps_in(folder);
		REPORT_PRINT("- Maps: %ld in maps/, %ld in custom_maps/\n", stock > 0 ? stock : 0, custom > 0 ? custom : 0);
	}
	REPORT_PRINT("\n## Settings\n\nThe display's (the others changed: settings.txt in the zip)\n\n```\n");
	for (index = 0; index < sizeof(settings) / sizeof(settings[0]); index++)
	{
		if (config_text(settings[index], line, sizeof(line)))
			REPORT_PRINT("%s = %s\n", settings[index], line);
	}
	REPORT_PRINT("```\n");
	if (crash)
		return text;
	REPORT_PRINT("\n## Frame times\n\nThe last %d seconds, loading screens included\n\n```\n", REPORT_SECONDS);
	count = report_frames.count < REPORT_FRAMES ? report_frames.count : REPORT_FRAMES;
	first = report_frames.count - count;
	while (count && report_frames.at_ms[first & (REPORT_FRAMES - 1)] + REPORT_SECONDS * 1000u < now_ms)
	{
		first++;
		count--;
	}
	micros = malloc((count ? count : 1) * sizeof(*micros));
	at_ms = malloc((count ? count : 1) * sizeof(*at_ms));
	work = malloc((count ? count : 1) * sizeof(*work));
	if (micros && at_ms && work)
	{
		char *summary = malloc(4096);

		for (index = 0; index < count; index++)
		{
			micros[index] = report_frames.micros[(first + index) & (REPORT_FRAMES - 1)];
			at_ms[index] = report_frames.at_ms[(first + index) & (REPORT_FRAMES - 1)];
			work[index] = report_frames.work[(first + index) & (REPORT_FRAMES - 1)];
		}
		if (summary)
		{
			report_frame_summary(micros, at_ms, count, now_ms, summary, 4096);
			REPORT_PRINT("%s", summary);
			if (count)
			{
				report_gpu_summary(micros, work, count, summary, 4096);
				REPORT_PRINT("%s", summary);
			}
			free(summary);
		}
	}
	free(micros);
	free(at_ms);
	free(work);
	REPORT_PRINT("```\n");
#undef REPORT_PRINT
	return text;
}

/* ---------- the last run's crash */

/* the most of the host's lines of a crash, and of the log's last lines,
report.md shows (crash.txt has them all), and the longest line */
#define REPORT_CRASH_LINES 24
#define REPORT_CRASH_LOG_LINES 10
#define REPORT_CRASH_LINE_MOST 200
/* the record's heading of its log (port/android/host/host_common.c's
host_crash_record_end) */
#define REPORT_CRASH_LOG_HEADING "\n--- the log's last lines\n"

/* a session file's value of name (its "name: value" line), into value (""
if it has none) */
static void report_session_value(const char *session, const char *name, char *value, size_t size)
{
	size_t length = strlen(name);
	const char *line = session;

	value[0] = 0;
	while (line && *line)
	{
		const char *end = strchr(line, '\n');
		size_t line_length = end ? (size_t)(end - line) : strlen(line);

		if (line_length > length + 1 && !strncmp(line, name, length) && line[length] == ':')
		{
			size_t start = length + 1;

			while (start < line_length && line[start] == ' ')
				start++;
			snprintf(value, size, "%.*s", (int)(line_length - start), line + start);
			value[strcspn(value, "\r")] = 0;
			return;
		}
		line = end ? end + 1 : NULL;
	}
}

/* a line of text (its length), at most REPORT_CRASH_LINE_MOST of it, as
report.md quotes it: its backticks made quotes, so that none closes a block
early */
static const char *report_crash_line(const char *line, size_t length, char quoted[REPORT_CRASH_LINE_MOST + 1])
{
	size_t index;

	if (length > REPORT_CRASH_LINE_MOST)
		length = REPORT_CRASH_LINE_MOST;
	for (index = 0; index < length; index++)
		quoted[index] = line[index] == '`' ? '\'' : line[index];
	quoted[length] = 0;
	return quoted;
}

void report_crash_summary(const char *session, const char *record, char *text, size_t size)
{
	char build[160], started[64], reference[32], quoted[REPORT_CRASH_LINE_MOST + 1];
	const char *log = NULL, *crash_end = NULL, *line, *first_end = NULL;
	size_t used = 0;
	long count;

	if (!size)
		return;
	text[0] = 0;
	report_session_value(session ? session : "", "build", build, sizeof(build));
	report_session_value(session ? session : "", "started", started, sizeof(started));
	report_session_value(session ? session : "", "reference address", reference, sizeof(reference));
	if (record)
	{
		log = strstr(record, REPORT_CRASH_LOG_HEADING);
		crash_end = log ? log : record + strlen(record);
		if (log)
			log += sizeof(REPORT_CRASH_LOG_HEADING) - 1;
		first_end = memchr(record, '\n', (size_t)(crash_end - record));
		if (!first_end)
			first_end = crash_end;
	}
#define REPORT_PRINT(...) \
	(used += (size_t)snprintf(text + (used < size ? used : size - 1), used < size ? size - used : 1, __VA_ARGS__))
	REPORT_PRINT("## The crash\n\nGOAT H1 closed unexpectedly the last time it ran.\n\n");
	REPORT_PRINT("- Build: %s\n", build[0] ? build : "unknown");
	if (started[0])
		REPORT_PRINT("- Started: %s\n", started);
	if (record && first_end > record)
		REPORT_PRINT("- Crash: %s\n", report_crash_line(record, (size_t)(first_end - record), quoted));
	else
		REPORT_PRINT("- Crash: none written (the game was ended from outside, or stopped before it could write one)\n");
	if (reference[0])
		REPORT_PRINT("- Reference address: %s (debug.txt's, which places the game's addresses in its build)\n", reference);
	/* (the host's lines after the first, at most REPORT_CRASH_LINES) */
	if (record && first_end < crash_end)
	{
		REPORT_PRINT("\nThe crash (crash.txt in the zip has it whole, with the run's log):\n\n```\n");
		for (line = first_end + 1, count = 0; line < crash_end && count < REPORT_CRASH_LINES; count++)
		{
			const char *end = memchr(line, '\n', (size_t)(crash_end - line));
			size_t length = end ? (size_t)(end - line) : (size_t)(crash_end - line);

			REPORT_PRINT("%s\n", report_crash_line(line, length, quoted));
			line = end ? end + 1 : crash_end;
		}
		if (line < crash_end)
			REPORT_PRINT("(and more: crash.txt)\n");
		REPORT_PRINT("```\n");
	}
	/* (the log's last REPORT_CRASH_LOG_LINES lines) */
	if (log && *log)
	{
		const char *end = log + strlen(log);

		while (end > log && end[-1] == '\n')
			end--;
		line = end;
		for (count = 0; line > log && count < REPORT_CRASH_LOG_LINES; count++)
		{
			line--;
			while (line > log && line[-1] != '\n')
				line--;
		}
		REPORT_PRINT("\nThe log's last lines:\n\n```\n");
		while (line < end)
		{
			const char *next = memchr(line, '\n', (size_t)(end - line));
			size_t length = next ? (size_t)(next - line) : (size_t)(end - line);

			REPORT_PRINT("%s\n", report_crash_line(line, length, quoted));
			line = next ? next + 1 : end;
		}
		REPORT_PRINT("```\n");
	}
#undef REPORT_PRINT
}

/* a file (its last most bytes), NUL-terminated, or NULL; free() it */
static char *report_file_text(const char *path, size_t most)
{
	size_t size;
	char *data = report_file_end(path, most, &size), *text;

	if (!data)
		return NULL;
	text = realloc(data, size + 1);
	if (!text)
	{
		free(data);
		return NULL;
	}
	text[size] = 0;
	return text;
}

/* a file of the sessions' (in reports/) */
static void report_crash_path(const char *name, char *path, size_t size)
{
	snprintf(path, size, "%s/reports/%s", platform_data_root(), name);
}

/* the crash answered: never asked of again */
static void report_crash_forget(void)
{
	char path[1200];

	report_crash_path(".crashed", path, sizeof(path));
	remove(path);
	report_crash_path(".crashed.txt", path, sizeof(path));
	remove(path);
	free(report_crash.session);
	free(report_crash.record);
	report_crash.session = report_crash.record = NULL;
	report_crash.found = 0;
}

/* the session halted (report_session_halted): its files kept at the exit
that follows, as a crash's */
static int report_session_halt;

/* the game exited (not a crash, nor a halt): this session's file removed */
static void report_session_end(void)
{
	char path[1200];

	if (report_session_halt)
		return;
	report_crash_path(".running", path, sizeof(path));
	remove(path);
	report_crash_path(".crash.txt", path, sizeof(path));
	remove(path);
}

/* this session's file (reports/.running): what the next start needs to
report its crash; 1 if it was written */
static int report_session_write(void)
{
	static char when[64];
	char running[1200], build[160];
	FILE *file;

	/* (when this session began: the first writing's) */
	if (!when[0])
	{
		time_t now = time(NULL);
		struct tm local;

		localtime_r(&now, &local);
		strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
	}
	report_crash_path(".running", running, sizeof(running));
	updater_build_text(build, sizeof(build));
	file = fopen(running, "wb");
	if (!file)
		return 0;
	fprintf(file, "build: %s\nplatform: %s\nstarted: %s\nreference address: %x\n", build, report_platform(), when,
		(unsigned int)(uintptr_t)write_to_error_file);
	fclose(file);
	return 1;
}

/* (1 once this session's file is written: not a dedicated server's, whose
data folder a game may share; 1 while it is handed over to a new build's) */
static int report_session_begun, report_session_left;

void report_session_handover(int leaving)
{
	if (!report_session_begun)
		return;
	if (leaving)
	{
		report_session_end();
		report_session_left = 1;
		platform_log("report: this session ended for the new build's");
		return;
	}
	if (report_session_left && !report_session_halt)
		report_session_write();
	report_session_left = 0;
}

void report_session_start(void)
{
	static int started;
	char folder[1024], running[1200], record[1200], crashed[1200], crashed_record[1200];
	FILE *file;

	if (started)
		return;
	started = 1;
	snprintf(folder, sizeof(folder), "%s/reports", platform_data_root());
	mkdir(folder, 0755);
	report_crash_path(".running", running, sizeof(running));
	report_crash_path(".crash.txt", record, sizeof(record));
	report_crash_path(".crashed", crashed, sizeof(crashed));
	report_crash_path(".crashed.txt", crashed_record, sizeof(crashed_record));
	/* (the last session did not exit: its files kept as the crash's until
	it is answered, a crash not answered yet replaced by this newer one) */
	file = fopen(running, "rb");
	if (file)
	{
		fclose(file);
		remove(crashed_record);
		rename(record, crashed_record);
		rename(running, crashed);
	}
	else
		remove(record);
	report_crash.session = report_file_text(crashed, 64 * 1024);
	report_crash.record = report_crash.session ? report_file_text(crashed_record, REPORT_FILE_MOST) : NULL;
#if defined(HALO_ANDROID) || defined(HALO_IOS)
	/* (the system ends an app in the background without a word: only what
	the host wrote at a crash is one) */
	if (report_crash.session && !report_crash.record)
		report_crash_forget();
#endif
	report_crash.found = report_crash.session != NULL;
	if (report_crash.found)
		platform_log("report: the last run closed unexpectedly%s", report_crash.record ? "" : " (no crash report written)");
	/* (this session's: what the next start needs to report its crash) */
	if (report_session_write())
	{
		report_session_begun = 1;
		atexit(report_session_end);
	}
#ifdef HALO_GUEST
	host_crash_record(record, report_log.text, REPORT_LOG_SIZE, &report_log.written);
#endif
}

void report_session_halted(const char *reason)
{
	char path[1200], *log;
	size_t size;
	FILE *file;

	if (report_session_halt)
		return;
	report_session_halt = 1;
	/* (as the host writes a crash, host_common.c's host_crash_record_end:
	its lines, then the log after its heading) */
	report_crash_path(".crash.txt", path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
		return;
	fprintf(file, "halt: %s\n", reason && reason[0] ? reason : "a fatal error");
	log = report_log_text(&size);
	if (log)
	{
		fputs(REPORT_CRASH_LOG_HEADING, file);
		fwrite(log, 1, size, file);
		free(log);
	}
	fclose(file);
}

/* (each frame, until it is asked) the crash question at the main menu; a run
nobody watches logs what its report would say instead, and a test answers
it with debug.crash_answer ("ask" opens it all the same) */
static void report_crash_question(void)
{
	const char *answer = config_string("debug.crash_answer");
	/* ("ask": the question opened though nobody watches, for a test to
	answer in the menus) */
	int ask = !strcmp(answer, "ask");

	if (!ask && (report_unattended() || answer[0]))
	{
		char *summary = malloc(16 * 1024), *line, *next;

		report_crash.asked = 1;
		if (summary)
		{
			report_crash_summary(report_crash.session, report_crash.record, summary, 16 * 1024);
			platform_log("report: not asking about the last run's crash (nobody is watching); its report would say:");
			for (line = summary; *line; line = next)
			{
				next = strchr(line, '\n');
				if (next)
					*next++ = 0;
				else
					next = line + strlen(line);
				platform_log("report:   %s", line);
			}
			free(summary);
		}
		if (answer[0])
		{
			platform_log("report: answering %s (debug.crash_answer)", answer);
			report_crash_answer(!strcmp(answer, "yes"));
		}
		return;
	}
	if (!pc_menu_crash_question_open())
		return;
	report_crash.asked = 1;
	platform_log("report: asking about the last run's crash at the main menu");
}

void report_crash_answer(int report)
{
	if (!report_crash.found)
		return;
	if (!report)
	{
		platform_log("report: the last run's crash not reported (not asked about again)");
		report_crash_forget();
		return;
	}
	/* (saved at the next frame, as REPORT A PROBLEM is: report_write) */
	report_crash.wanted = 1;
	report_requested = 1;
}

/* ---------- the issue */

#define REPORT_ISSUES "https://github.com/OpenH1/goath1/issues/new"
/* GitHub's addresses: some 8 KB at most */
#define REPORT_URL_MOST 7800

static size_t report_percent_encode(const char *text, size_t length, char *out, size_t room)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t used = 0, index;

	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)text[index];
		int plain = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.' ||
			character == '~';

		if (used + (plain ? 1 : 3) >= room)
			return (size_t)-1;
		if (plain)
			out[used++] = (char)character;
		else
		{
			out[used++] = '%';
			out[used++] = hex[character >> 4];
			out[used++] = hex[character & 15];
		}
	}
	out[used] = 0;
	return used;
}

/* a folder's file: address (its path's separators kept, the rest encoded;
a Windows drive's path after a third slash) */
static void report_file_url(const char *folder, char *url, size_t size)
{
	size_t used = (size_t)snprintf(url, size, folder[0] == '/' ? "file://" : "file:///"), index;

	for (index = 0; folder[index] && used + 4 < size; index++)
	{
		char character = folder[index] == '\\' ? '/' : folder[index];

		if (character == '/' || character == ':')
			url[used++] = character;
		else
		{
			size_t written = report_percent_encode(&character, 1, url + used, size - used);

			if (written == (size_t)-1)
				break;
			used += written;
		}
	}
	url[used < size ? used : size - 1] = 0;
}

/* a path made whole: a relative one (the data root ".", where the game
started in its own folder finds its maps, as its menu entry and Steam start
it) after the current folder, current; one whole already (/..., \..., C:...)
as it is. A file: address needs it: file:///./reports is /reports */
static void report_whole_path(const char *path, const char *current, char *out, size_t size)
{
	size_t length;

	if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':') || !current || !current[0])
	{
		snprintf(out, size, "%s", path);
		return;
	}
	while (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
		path += 2;
	if (!strcmp(path, "."))
		path = "";
	length = strlen(current);
	snprintf(out, size, "%s%s%s", current, path[0] && current[length - 1] != '/' && current[length - 1] != '\\' ?
		"/" : "", path);
}

/* the reports folder (reports/ in the data root), whole */
static void report_folder(char *folder, size_t size)
{
	char relative[1024], current[1024];

	snprintf(relative, sizeof(relative), "%s/reports", platform_data_root());
	report_whole_path(relative, getcwd(current, sizeof(current)), folder, size);
}

/* where a report is written when reports/ cannot be (a folder not
writable, a disk full): the home folder, whole, or "" for none */
static void report_fallback_folder(char *folder, size_t size)
{
	const char *home = getenv("HOME");

#if defined(_WIN32) || defined(HALO_WINDOWS)
	if (!home || !home[0])
		home = getenv("USERPROFILE");
#endif
	snprintf(folder, size, "%s", home && home[0] ? home : "");
}

int report_issue_url(const char *title, const char *body, char *url, size_t most)
{
	size_t used, length = strlen(body), written;

	used = (size_t)snprintf(url, most, "%s?title=", REPORT_ISSUES);
	if (used >= most)
		return 0;
	written = report_percent_encode(title, strlen(title), url + used, most - used);
	if (written == (size_t)-1)
		return 0;
	used += written;
	if (used + 6 >= most)
		return 0;
	memcpy(url + used, "&body=", 7);
	used += 6;
	/* (the body cut after a line's end until it fits; a code block the cut
	leaves open closed after it) */
	for (;;)
	{
		size_t index, fences = 0;

		for (index = 0; index + 3 <= length; index++)
		{
			if ((!index || body[index - 1] == '\n') && !memcmp(body + index, "```", 3))
				fences++;
		}
		written = report_percent_encode(body, length, url + used, most - used);
		if (written != (size_t)-1 && (fences % 2 == 0 || report_percent_encode(length && body[length - 1] != '\n' ?
			"\n```\n" : "```\n", length && body[length - 1] != '\n' ? 5 : 4, url + used + written,
			most - used - written) != (size_t)-1))
		{
			return 1;
		}
		if (!length)
			return 0;
		length--;
		while (length && body[length - 1] != '\n')
			length--;
		if (!length)
			return 0;
	}
}

/* a run nobody watches (as the clipboard is left alone in it:
sdl_platform.c's platform_clipboard_left_alone): no browser opened */
static int report_unattended(void)
{
	return config_boolean("debug.no_clipboard") || config_boolean("debug.null_renderer") ||
		config_string("debug.test_input")[0] || config_boolean("debug.fixed_step") ||
		config_string("debug.network_test")[0] || config_real("debug.exit_after") > 0.0;
}

/* an issue opened in the browser, and the folder its zip is in beside it
(a run nobody watches leaves the desktop alone: the addresses logged); 0 if
the browser could not be opened */
static int report_open_issue(const char *url, const char *folder)
{
	char folder_url[1400];
	int opened;

	report_file_url(folder, folder_url, sizeof(folder_url));
	if (report_unattended())
	{
		platform_log("report: the issue's address: %s", url);
		platform_log("report: the folder's address: %s", folder_url);
		return 1;
	}
	opened = platform_open_url(url);
#if !defined(HALO_ANDROID) && !defined(HALO_IOS)
	/* (the folder, for the zip to be dragged into the issue) */
	platform_open_url(folder_url);
#endif
	if (opened)
		platform_log("report: opened a new issue for it, and %s", folder_url);
	return opened;
}

/* ---------- the report */

/* the report being finished: its name, its summary (report_text's, made as
it was asked for), its screenshot's file in reports/ (or none), whether its
issue is opened; between the key and the note typed in a game */
static struct
{
	int waiting;
	char name[64];
	char *text;
	char shot[96];
	int send;
	/* (the last run's crash's: report_crash) */
	int crash;
} report_capture;

/* a report's zip being written, on a thread of its own (a 7 MB screenshot
and the logs scrubbed take tens of milliseconds, too long for a frame): what
the game's thread gave it, and what it says once done (report_finish_done,
on the game's thread again, which then opens or holds the issue) */
static struct
{
	int running;
	volatile int done;
	pthread_t thread;
	char name[64], folder[1024], path[1200], shot[1200], host_log[1200], host_log_other[1200], debug_log[1200];
	/* where it is written if reports/ cannot be (report_fallback_folder's,
	or ""), and whether it was */
	char fallback_folder[1024], fallback[1200];
	int fell_back;
	unsigned short time, date;
	/* report.md, log.txt, settings.txt and input.txt (malloc()'s, or NULL) */
	char *issue, *log, *settings, *input;
	/* the host's kept lines, host.log where there is no halo.log
	(malloc()'s, or NULL) */
	char *host_lines;
	size_t host_lines_size;
	/* the last run's crash's: crash.txt (malloc()'s, or NULL) and the macOS
	app's host log of that run */
	int crash;
	char *crash_text, previous_log[1200];
	/* the issue's address (malloc()'s, or NULL: none to open) */
	char *url;
	int noted, opened, saved, count;
	double game_ms, thread_ms;
} report_job;

/* (the game quitting: nothing more said on its console) */
static int report_exiting;

/* (the job's thread) the zip written at path; 1 if it was (one that could
not be finished removed) */
static int report_job_zip(const char *path)
{
	struct report_zip zip;
	size_t size;
	char *data;
	int saved;

	memset(&zip, 0, sizeof(zip));
	zip.time = report_job.time;
	zip.date = report_job.date;
	zip.file = fopen(path, "wb");
	if (!zip.file)
		return 0;
	report_job.opened = 1;
	if (report_job.issue)
		report_zip_add_text(&zip, "report.md", report_job.issue, strlen(report_job.issue));
	if (report_job.log)
		report_zip_add_text(&zip, "log.txt", report_job.log, strlen(report_job.log));
	/* (the host's log file, where it has one) */
	data = report_log_file_end(report_job.host_log, REPORT_FILE_MOST, &size);
	if (!data && report_job.host_log_other[0])
		data = report_log_file_end(report_job.host_log_other, REPORT_FILE_MOST, &size);
	if (data)
	{
		report_zip_add_text(&zip, "halo.log", data, size);
		free(data);
	}
	/* (or, with no file, the lines the host kept: halo.log has them
	where there is one) */
	else if (report_job.host_lines && report_job.host_lines_size)
		report_zip_add_text(&zip, "host.log", report_job.host_lines, report_job.host_lines_size);
	data = report_log_file_end(report_job.debug_log, REPORT_FILE_MOST, &size);
	if (data)
	{
		report_zip_add_text(&zip, "debug.txt", data, size);
		free(data);
	}
	if (report_job.crash_text)
		report_zip_add_text(&zip, "crash.txt", report_job.crash_text, strlen(report_job.crash_text));
	if (report_job.previous_log[0])
	{
		data = report_log_file_end(report_job.previous_log, REPORT_FILE_MOST, &size);
		if (data)
		{
			report_zip_add_text(&zip, "halo.previous.log", data, size);
			free(data);
		}
	}
	if (report_job.settings)
		report_zip_add_text(&zip, "settings.txt", report_job.settings, strlen(report_job.settings));
	if (report_job.input)
		report_zip_add_text(&zip, "input.txt", report_job.input, strlen(report_job.input));
	/* (the frame, written by the host beside the zip as it was asked
	for, then taken in) */
	if (report_job.shot[0])
	{
		data = report_file_end(report_job.shot, 64 * 1024 * 1024, &size);
		if (data)
		{
			report_zip_add(&zip, "screenshot.png", data, size);
			free(data);
		}
	}
	saved = report_zip_end(&zip);
	saved = fclose(zip.file) == 0 && saved;
	if (!saved)
		remove(path);
	report_job.count = zip.count;
	return saved;
}

/* (the job's thread) the zip written in reports/, or, if it cannot be,
in the fallback folder */
static void *report_job_write(void *unused)
{
	long long started = report_now_ns();

	(void)unused;
	report_job.saved = report_job_zip(report_job.path);
	if (!report_job.saved && report_job.fallback[0])
	{
		report_job.saved = report_job.fell_back = report_job_zip(report_job.fallback);
	}
	if (report_job.shot[0])
		remove(report_job.shot);
	report_job.thread_ms = (report_now_ns() - started) / 1e6;
	report_job.done = 1;
	return NULL;
}

/* (the game's thread) the job's zip written: the player told, its issue
opened or held for the menus */
static void report_finish_done(void)
{
	char message[1400], folder[1024];
	const char *path;

	if (!report_job.running)
		return;
	/* (1: on its thread; 2: written on this one, none made) */
	if (report_job.running == 1)
		pthread_join(report_job.thread, NULL);
	report_job.running = 0;
	free(report_job.issue);
	free(report_job.log);
	free(report_job.settings);
	free(report_job.input);
	free(report_job.host_lines);
	free(report_job.crash_text);
	report_job.issue = report_job.log = report_job.settings = report_job.host_lines = report_job.crash_text = NULL;
	report_job.input = NULL;
	if (!report_job.saved)
	{
		free(report_job.url);
		report_job.url = NULL;
		snprintf(message, sizeof(message), report_job.opened ? "Report failed: couldn't write %s.zip in %s%s%s" :
			"Report failed: can't write in %s%s%s", report_job.opened ? report_job.name : report_job.folder,
			report_job.opened ? report_job.folder : "", report_job.fallback[0] ? " or " : "",
			report_job.fallback_folder);
		platform_log("report: could not write %s%s%s", report_job.path, report_job.fallback[0] ? " or " : "",
			report_job.fallback);
		if (!report_exiting)
			report_game_say(message);
		return;
	}
	/* (written in the fallback folder: the whole path said, and that
	folder opened with the issue) */
	path = report_job.fell_back ? report_job.fallback : report_job.path;
	snprintf(folder, sizeof(folder), "%s", report_job.fell_back ? report_job.fallback_folder : report_job.folder);
	if (report_job.fell_back)
	{
		snprintf(message, sizeof(message), "Report saved in %s (reports/ couldn't be written)", path);
		platform_log("report: could not write %s: written in %s instead", report_job.path, path);
	}
	else
		snprintf(message, sizeof(message), "Report saved: reports/%s.zip", report_job.name);
	platform_log("report: saved %s (%d files, in %.1f ms on its thread, %.1f ms on the game's)%s", path,
		report_job.count, report_job.thread_ms, report_job.game_ms, report_job.crash ? ", of the last run's crash" :
		report_job.noted ? ", with a note" : "");
	/* (the crash reported: not asked of again) */
	if (report_job.crash)
		report_crash_forget();
	if (report_job.url && !report_game_menus())
	{
		/* (in a game, nothing opened over it: the issue waits for the
		menus, report_frame_presented, or the game's end: report_at_exit;
		with REPORT_PENDING waiting, the oldest let go) */
		if (report_pending.count >= REPORT_PENDING)
		{
			free(report_pending.urls[0]);
			memmove(report_pending.urls, report_pending.urls + 1, (REPORT_PENDING - 1) * sizeof(report_pending.urls[0]));
			report_pending.count--;
			platform_log("report: %d issues waiting for the menus; the oldest let go", REPORT_PENDING);
		}
		snprintf(report_pending.folders[report_pending.count], sizeof(report_pending.folders[0]), "%s", folder);
		report_pending.urls[report_pending.count++] = report_job.url;
		report_job.url = NULL;
		snprintf(message, sizeof(message), "Report saved: its GitHub issue opens when you're back in the menus");
		platform_log("report: its issue opens once the game is left");
	}
	else if (report_job.url)
	{
		/* (the browser not opened: where the zip is said, for it to be
		sent by hand) */
		if (!report_open_issue(report_job.url, folder))
		{
			snprintf(message, sizeof(message), "Report saved in %s: the browser didn't open, send it at %s", path,
				REPORT_ISSUES);
			platform_log("report: its issue could not be opened: the report is %s", path);
		}
		else if (report_job.fell_back)
		{
			snprintf(message, sizeof(message), "Report saved in %s: drag it into the issue", path);
		}
		else
		{
#if !defined(HALO_ANDROID) && !defined(HALO_IOS)
			snprintf(message, sizeof(message), "Report saved: drag reports/%s.zip into the issue", report_job.name);
#else
			snprintf(message, sizeof(message), "Report saved: attach reports/%s.zip to the issue", report_job.name);
#endif
		}
	}
	free(report_job.url);
	report_job.url = NULL;
	if (!report_exiting)
		report_game_say(message);
}

/* (each frame) a zip its thread has written, finished */
static void report_finish_poll(void)
{
	if (report_job.running && report_job.done)
		report_finish_done();
}

/* the zip begun on its thread (report_job), its issue opened or held for
the menus once it is written (report_finish_done): the note typed, NULL for
none. wait: written before this returns (the game quitting); a report still
being written is finished first */
static void report_finish(const char *note, int wait)
{
	const char *name = report_capture.name;
	struct tm local;
	time_t now = time(NULL);
	size_t size, issue_size;
	char *issue;
	long long started = report_now_ns();

	report_finish_done();
	memset(&report_job, 0, sizeof(report_job));
	snprintf(report_job.name, sizeof(report_job.name), "%s", name);
	/* (whole: what the player is told, and the folder's file: address) */
	report_folder(report_job.folder, sizeof(report_job.folder));
	snprintf(report_job.path, sizeof(report_job.path), "%s/%s.zip", report_job.folder, name);
	report_fallback_folder(report_job.fallback_folder, sizeof(report_job.fallback_folder));
	if (report_job.fallback_folder[0])
	{
		snprintf(report_job.fallback, sizeof(report_job.fallback), "%s/OpenH1-report-%s.zip",
			report_job.fallback_folder, name);
	}
	if (report_capture.shot[0])
		snprintf(report_job.shot, sizeof(report_job.shot), "%s/%s", report_job.folder, report_capture.shot);
	/* (the host's log file, where it has one) */
	config_folder(report_job.host_log, sizeof(report_job.host_log));
	snprintf(report_job.host_log + strlen(report_job.host_log), sizeof(report_job.host_log) -
		strlen(report_job.host_log), "halo.log");
#ifdef HALO_MACOS
	if (getenv("HOME"))
	{
		snprintf(report_job.host_log_other, sizeof(report_job.host_log_other), "%s/Library/Logs/OpenH1/halo.log",
			getenv("HOME"));
	}
#endif
	snprintf(report_job.debug_log, sizeof(report_job.debug_log), "%s/debug.txt", platform_data_root());
	localtime_r(&now, &local);
	report_job.time = (unsigned short)(local.tm_hour << 11 | local.tm_min << 5 | local.tm_sec / 2);
	report_job.date = (unsigned short)((local.tm_year - 80) << 9 | (local.tm_mon + 1) << 5 | local.tm_mday);
	report_job.noted = note && note[0];
	/* (report.md: what happened, the player's note or where it goes, over
	the summary) */
	issue_size = (report_capture.text ? strlen(report_capture.text) : 0) + (note ? strlen(note) : 0) + 512;
	issue = malloc(issue_size);
	if (issue)
	{
		if (report_capture.crash)
			snprintf(issue, issue_size, "**What happened:** GOAT H1 closed unexpectedly.\n\n(Say what you were doing "
				"when it closed, if you remember. Then drag %s.zip, from the reports folder that opened, into this "
				"box.)\n\n%s", name, report_capture.text ? report_capture.text : "");
		else if (note && note[0])
			snprintf(issue, issue_size, "**What happened:** %s\n\n(Drag %s.zip, from the reports folder that "
				"opened, into this box.)\n\n%s", note, name, report_capture.text ? report_capture.text : "");
		else
			snprintf(issue, issue_size, "**What happened:** \n\n(Say what you saw and what you expected. Then drag "
				"%s.zip, from the reports folder that opened, into this box.)\n\n%s", name,
				report_capture.text ? report_capture.text : "");
	}
	report_job.issue = issue;
	free(report_capture.text);
	report_capture.text = NULL;
	report_capture.waiting = 0;
	/* (the last run's crash: what the host wrote of it, with that run's
	log, or what its session's file says; and the macOS app's host log of
	that run) */
	report_job.crash = report_capture.crash;
	if (report_capture.crash)
	{
		if (report_crash.record)
			report_job.crash_text = strdup(report_crash.record);
		else if (report_crash.session)
		{
			report_job.crash_text = malloc(strlen(report_crash.session) + 200);
			if (report_job.crash_text)
				sprintf(report_job.crash_text, "%s\nNo crash report was written: the game was ended from outside, or "
					"stopped before it could write one.\n", report_crash.session);
		}
#ifdef HALO_MACOS
		if (getenv("HOME"))
		{
			snprintf(report_job.previous_log, sizeof(report_job.previous_log),
				"%s/Library/Logs/OpenH1/halo.previous.log", getenv("HOME"));
		}
#endif
	}
#ifdef HALO_GUEST
	/* (the lines the host kept, for host.log where there is no halo.log) */
	report_job.host_lines = malloc(REPORT_HOST_LOG_MOST + 1);
	if (report_job.host_lines)
		report_job.host_lines_size = host_log_recent(report_job.host_lines, REPORT_HOST_LOG_MOST + 1);
#endif
	/* (what the game's thread alone reads: the log's ring, the settings) */
	report_job.log = report_log_text(&size);
	if (report_job.log)
		report_job.log[size] = 0;
	report_job.settings = report_settings_text(&size);
	if (report_job.settings)
		report_job.settings[size] = 0;
	report_job.input = input_trace_text(INPUT_TRACE_SECONDS, NULL);
	if (report_capture.send && issue)
	{
		char title[160], build[120], *scrubbed;
		size_t scrubbed_size;

		/* (a release's number, "build-1739", or a local build's) */
		updater_build_text(build, sizeof(build));
		if (!strncmp(build, "build-", 6))
			build[strcspn(build, " ")] = 0;
		else
			snprintf(build, sizeof(build), "local build");
		snprintf(title, sizeof(title), "Problem report (%s, %s)", report_platform(), build);
		/* (the body scrubbed as the zip's report.md is: the note may have
		been pasted) */
		report_job.url = malloc(REPORT_URL_MOST + 1);
		scrubbed = report_scrub(issue, strlen(issue), report_home(), &scrubbed_size);
		if (report_job.url && (!scrubbed || !report_issue_url(title, scrubbed, report_job.url, REPORT_URL_MOST + 1)))
		{
			free(report_job.url);
			report_job.url = NULL;
		}
		free(scrubbed);
	}
	report_job.game_ms = (report_now_ns() - started) / 1e6;
	report_job.running = 1;
	if (wait || pthread_create(&report_job.thread, NULL, report_job_write, NULL) != 0)
	{
		/* (no thread: written here) */
		report_job_write(NULL);
		report_job.running = 2;
	}
	if (wait || report_job.running == 2)
		report_finish_done();
}

/* the game quit: a report being written finished, one waiting for its note
finished without one, and the issues held for the menus opened */
static void report_at_exit(void)
{
	report_exiting = 1;
	if (report_capture.waiting)
		report_finish(NULL, 1);
	report_finish_done();
	report_open_pending();
}

int report_write(unsigned int target, int send, char *message, size_t message_size)
{
	static int at_exit;
	char folder[1024];
	time_t now = time(NULL);
	struct tm local;

	message[0] = 0;
	if (!at_exit)
		at_exit = atexit(report_at_exit) == 0;
	/* (a report still waiting for its note is finished first, without one) */
	if (report_capture.waiting)
	{
		report_game_note_cancel();
		report_finish(NULL, 0);
	}
	localtime_r(&now, &local);
	strftime(report_capture.name, sizeof(report_capture.name), "%Y-%m-%d_%H.%M.%S", &local);
	report_folder(folder, sizeof(folder));
	mkdir(folder, 0755);
	/* (the last run's crash's, asked for at the crash question: its summary
	heading the system's, and no screenshot, which would be of the menu) */
	report_capture.crash = report_crash.wanted && report_crash.found;
	report_crash.wanted = 0;
	if (report_capture.crash)
	{
		char *system = report_text(report_capture.name, 1), *summary = malloc(16 * 1024);
		size_t scrubbed_size;
		char *scrubbed = NULL;

		if (summary)
		{
			report_crash_summary(report_crash.session, report_crash.record, summary, 16 * 1024);
			scrubbed = report_scrub(summary, strlen(summary), report_home(), &scrubbed_size);
		}
		report_capture.text = scrubbed && system ? malloc(scrubbed_size + strlen(system) + 2) : NULL;
		if (report_capture.text)
			sprintf(report_capture.text, "%.*s\n%s", (int)scrubbed_size, scrubbed, system);
		free(scrubbed);
		free(summary);
		free(system);
		target = 0;
	}
	else
		report_capture.text = report_text(report_capture.name, 0);
	report_capture.shot[0] = 0;
	if (target && !xgpu_backend_screenshot(target, folder, report_capture.shot, sizeof(report_capture.shot)))
		report_capture.shot[0] = 0;
	report_capture.send = send;
	/* (in a game, asked for with the keyboard's key, a line for the
	player's note first: report_note_done finishes it. A menu's row, or a
	controller's player, has no keyboard to answer it with: finished now,
	its issue held all the same) */
	if (!report_capture.crash && report_asked_by_key && report_game_playing() && report_game_note_begin())
	{
		report_capture.waiting = 1;
		platform_log("report: %s asked for, waiting for its note", report_capture.name);
		return 1;
	}
	report_finish(NULL, 0);
	return 1;
}

void report_note_done(const char *note)
{
	if (report_capture.waiting)
		report_finish(note, 0);
}
