/*
REPORT.C (test)

Save Report (port/linux/src/report.c): report.md's frame times (the average,
the 1% and 0.1% lows, the frames of 50 ms or more, the longest frames, a
stall only), the GPU's time and what held the frames back (GPU-bound,
CPU-bound, vsync or the frame cap), its Where section (report_where_text:
the game, the player, the network game, the console's lines to look from
there), the zip (its CRC-32, its local and central records), which
test_report.py opens with Python's zipfile, and the last run's crash: its
heading of report.md (report_crash_summary) and the host's record of it,
the log's ring oldest first (port/android/host/host_common.c); and the
reports folder's whole path and file: address (report_whole_path).
test_report.py takes the code.
*/

#include "harness.h"

#include <stdint.h>
#include <unistd.h>

#define REPORT_HITCH_MS 50
#define REPORT_STALL_MS 100
#define REPORT_LONGEST 8
#define REPORT_ZIP_FILES 8
#define REPORT_GPU_UNKNOWN 0xFFFFFFFFu
#define REPORT_PACED_PERCENT 20
#define REPORT_BUSY_PERCENT 95
#define REPORT_GPU_FEWEST 30

#include "under_test.inc"

static uint32_t micros[2048], at_ms[2048];
static struct report_frame_work work[2048];

/* 600 frames of 10 ms, each with the GPU's time, the renderer's thread's,
the game's waits and of them the pacing's */
static void gpu_frames(uint32_t gpu, uint32_t render, uint32_t waited, uint32_t paced)
{
	unsigned long index;

	for (index = 0; index < 600; index++)
	{
		micros[index] = 10000;
		work[index].gpu = gpu;
		work[index].render = render;
		work[index].waited = waited;
		work[index].paced = paced;
	}
}

/* how often word is in text */
static int report_count(char const *text, char const *word)
{
	int count = 0;

	while ((text = strstr(text, word)) != NULL)
	{
		count++;
		text += strlen(word);
	}
	return count;
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";
	static char text[4096];
	/* (CHECK's condition is printed as a format: no percent signs in it) */
	static char const low_10[] = "1% low: 10.0 fps\n", lowest_10[] = "0.1% low: 10.0 fps\n";
	static char const low_60[] = "1% low: 60.0 fps";

	if (!strcmp(test, "crc"))
	{
		CHECK(report_crc32(0, (const unsigned char *)"123456789", 9) == 0xCBF43926u, "the check value");
		CHECK(report_crc32(report_crc32(0, (const unsigned char *)"1234", 4), (const unsigned char *)"56789", 5) ==
			0xCBF43926u, "in parts");
		return 0;
	}
	if (!strcmp(test, "frames"))
	{
		unsigned long index;

		/* (1000 frames of 10 ms, every hundredth of 100 ms: 10 of them) */
		for (index = 0; index < 1000; index++)
		{
			micros[index] = index % 100 == 99 ? 100000 : 10000;
			at_ms[index] = (uint32_t)(index * 10);
		}
		report_frame_summary(micros, at_ms, 1000, 12000, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "Frames: 1000 in the last 10.9 seconds\n"), "the frames and their time");
		CHECK(strstr(text, "Average: 91.7 fps (10.90 ms a frame)\n"), "the average");
		CHECK(strstr(text, "Median frame: 10.00 ms\n"), "the median");
		CHECK(strstr(text, low_10), "the 1 percent low (the slowest 10 frames)");
		CHECK(strstr(text, lowest_10), "the 0.1 percent low (the slowest frame)");
		CHECK(strstr(text, "Frames of 50 ms or more: 10\n"), "the frames of 50 ms or more");
		CHECK(strstr(text, "Frames of 100 ms or more: 10 (the stall: lines"), "the frames of 100 ms or more");
		/* (the latest of the longest first, and how long before the report) */
		CHECK(strstr(text, "Longest frames:\n  100.0 ms, 2.0 s ago\n  100.0 ms, 3.0 s ago\n"), "the longest");
		CHECK(!strstr(text, "  10.0 ms,"), "a frame of the median's time listed as one of the longest");
		return 0;
	}
	if (!strcmp(test, "steady"))
	{
		unsigned long index;

		for (index = 0; index < 600; index++)
		{
			micros[index] = 16667;
			at_ms[index] = (uint32_t)(index * 17);
		}
		report_frame_summary(micros, at_ms, 600, 10200, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "Average: 60.0 fps"), "the average");
		CHECK(strstr(text, low_60), "the 1 percent low");
		CHECK(strstr(text, "Longest frames: none twice the median or longer\n"), "no stalls");
		/* (a frame of 50 ms exactly is one) */
		micros[0] = micros[1] = 50000;
		report_frame_summary(micros, at_ms, 600, 10200, text, sizeof(text));
		CHECK(strstr(text, "Frames of 50 ms or more: 2\n"), "frames of 50 ms exactly");
		CHECK(strstr(text, "Frames of 100 ms or more: 0\n"), "no frame of 100 ms");
		report_frame_summary(micros, at_ms, 0, 0, text, sizeof(text));
		CHECK(!strcmp(text, "No frames yet.\n"), "no frames");
		return 0;
	}
	if (!strcmp(test, "gpu"))
	{
		unsigned long index;

		/* (the GPU busy nearly all the frame, the game waiting for it) */
		gpu_frames(9600, 3000, 6000, 0);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "GPU time: 9.60 ms a frame, 9.60 ms at the 99th percentile\n"), "the GPU's time");
		CHECK(strstr(text, "Busy: the GPU 96%, the game 40%, the renderer's thread 30% of the time\n"), "the shares");
		CHECK(strstr(text, "Waiting for vsync or the frame cap: 0% of the time\n"), "the pacing's share");
		CHECK(strstr(text, "Verdict: GPU-bound\n"), "GPU-bound");
		/* (the game busy, the GPU and the renderer not) */
		gpu_frames(3000, 2000, 600, 0);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "the game 94%"), "the game's share");
		CHECK(strstr(text, "Verdict: CPU-bound (the game)\n"), "CPU-bound, the game");
		/* (the renderer's thread busy, the game waiting for it) */
		gpu_frames(5000, 9000, 8000, 0);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "Verdict: CPU-bound (the renderer's thread)\n"), "CPU-bound, the renderer's thread");
		/* (neither: the game waits most of the frame for the display) */
		gpu_frames(3000, 2000, 8500, 7000);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "Waiting for vsync or the frame cap: 70% of the time\n"), "the pacing's share, paced");
		CHECK(strstr(text, "Verdict: limited by vsync or the frame cap\n"), "vsync or the cap");
		/* (at the frame cap, the GPU busy for most but not all of the time) */
		gpu_frames(8600, 4400, 8800, 4800);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(strstr(text, "Verdict: limited by vsync or the frame cap\n"), "the cap, the GPU busy");
		/* (waits for the cap, but the GPU all but always busy: the GPU) */
		gpu_frames(9700, 3000, 6000, 3000);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(strstr(text, "Verdict: GPU-bound\n"), "the cap, the GPU saturated");
		/* (few waits for the cap, none busy most of the time: the busiest) */
		gpu_frames(6500, 5000, 8000, 1000);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(strstr(text, "Verdict: GPU-bound\n"), "the busiest, the GPU");
		gpu_frames(5000, 6500, 8000, 1000);
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(strstr(text, "Verdict: CPU-bound (the renderer's thread)\n"), "the busiest, the renderer's thread");
		/* (the 99th percentile: 10 of the 600 frames of 20 ms) */
		gpu_frames(3000, 2000, 8500, 7000);
		for (index = 0; index < 600; index += 60)
			work[index].gpu = 20000;
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(strstr(text, "ms a frame, 20.00 ms at the 99th percentile\n"), "the 99th percentile");
		/* (frames not measured left out: half of them) */
		gpu_frames(9600, 3000, 6000, 0);
		for (index = 0; index < 600; index += 2)
			work[index].gpu = REPORT_GPU_UNKNOWN;
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(strstr(text, "GPU time: 9.60 ms a frame, 9.60 ms at the 99th percentile\n"), "unmeasured frames left out");
		CHECK(strstr(text, "Verdict: GPU-bound\n"), "GPU-bound with unmeasured frames");
		/* (none measured: no verdict) */
		for (index = 0; index < 600; index++)
			work[index].gpu = REPORT_GPU_UNKNOWN;
		report_gpu_summary(micros, work, 600, text, sizeof(text));
		CHECK(!strcmp(text, "GPU time: not measured\n"), "no GPU time");
		return 0;
	}
	if (!strcmp(test, "issue"))
	{
		static char url[300], body[400];
		static char const fence[] = "%60%60%60", closed[] = "text%0A%60%60%60%0A",
			whole[] = "&body=%60%60%60%0Aall%0A%60%60%60%0A";
		unsigned long index;

		CHECK(report_issue_url("Problem report (macOS, build-1)", "# Hi\n- a&b=c\n", url, sizeof(url)), "it fits");
		printf("%s\n", url);
		CHECK(!strcmp(url, "https://github.com/OpenH1/goath1/issues/new?title=Problem%20report%20%28macOS%2C%20build-1"
			"%29&body=%23%20Hi%0A-%20a%26b%3Dc%0A"), "the address");
		/* (a long body cut at a line's end) */
		for (index = 0; index < 20; index++)
			strcat(body, "line of text\n");
		CHECK(report_issue_url("t", body, url, 150), "a long one cut to fit");
		printf("%s\n", url);
		CHECK(strlen(url) < 150, "within its room");
		CHECK(strlen(url) > 3 && !strcmp(url + strlen(url) - 3, "%0A"), "cut at a line's end");
		CHECK(!report_issue_url("t", "no line ends here at all, and too long for the room", url, 70), "no line to cut at");
		/* (a cut inside a code block: the block closed after it) */
		body[0] = 0;
		strcat(body, "## Frames\n\n```\n");
		for (index = 0; index < 20; index++)
			strcat(body, "line of text\n");
		strcat(body, "```\n");
		CHECK(report_issue_url("t", body, url, 200), "a long code block cut to fit");
		printf("%s\n", url);
		CHECK(strlen(url) < 200, "within its room, the fence too");
		CHECK(report_count(url, fence) == 2, "the code block left open by the cut");
		CHECK(strlen(url) > strlen(closed) && !strcmp(url + strlen(url) - strlen(closed), closed),
			"closed after its last line");
		CHECK(report_issue_url("t", "```\nall\n```\n", url, sizeof(url)) && strstr(url, whole) &&
			report_count(url, fence) == 2, "a whole body's block closed twice");
		return 0;
	}
	if (!strcmp(test, "scrub"))
	{
		static char const text[] =
			"data root: /Users/keyes/Halo/maps\n"
			"saves: /Volumes/Games/keyes/saves and /tmp/run-Users-keyes-dev/z\n"
			"C:\\Users\\keyes\\Downloads\\OpenH1\\halo.exe; Keyes joined; the Keyesmith map\n"
			"hosting: openh1://join/fba388a4deadbeef (the rest is left out)\n"
			"joined 203.0.113.7:2302 and 10.0.0.5, local 127.0.0.1, any 0.0.0.0\n"
			"vulkan: Vulkan 1.3.280, driver 24.1.3; version 300.1.2.3.4\n"
			"11.06.26 22:55:20  chat (team): Johnson: meet at the warthog\n"
			"11.06.26 22:55:21  chat: Cortana: hello\n"
			"11.06.26 22:55:22  chat notice: Johnson joined the game\n"
			"chat: a: hi\n"
			"chat: b: x\n"
			"host notice: Keyes (Discord: keyes#1, 12345) banned by the host\n"
			"Discord link: linked as keyes_discord at https://master.example\n"
			"Discord link: opening https://master.example/link in the browser (the page shows the code K7Q2-ZX)\n"
			"Internet play: cannot look up network.public_address home.keyes.example\n"
			"Internet play: network.public_address [2001:db8::7]:2302 is IPv6, which the tunnel does not use\n"
			"v6 2001:db8:85a3:0:0:8a2e:370:7334 and fe80::1c2b:3a4d%en0, mapped ::ffff:198.51.100.9, loopback ::1\n"
			"times 12:30:45 and 12:30:45.250, std::vector, a deadbeef::cafe, build 1.2.3, aa:bb\n"
			"chats are fine\n"
			"11.06.26 22:55:23  chat: the on-screen keyboard opened\n"
			"no newline at the end";
		size_t size;
		char *out = report_scrub(text, sizeof(text) - 1, "/Users/keyes", &size);

		CHECK(out != NULL, "scrubbed");
		CHECK(strlen(out) == size, "the text ended as a string");
		printf("%s\n", out);
		CHECK(strstr(out, "data root: ~/Halo/maps\n"), "the home folder as ~");
		CHECK(!strstr(out, "keyes"), "the account's name left");
		CHECK(strstr(out, "saves: /Volumes/Games/(user)/saves and /tmp/run-Users-(user)-dev/z\n"),
			"the account's name in a path outside the home folder");
		CHECK(strstr(out, "C:\\Users\\(user)\\Downloads") && strstr(out, "Keyes joined; the Keyesmith map"),
			"a Windows path's name, or a word that is not a path's part");
		CHECK(strstr(out, "hosting: openh1://(removed) (the rest"), "the link taken out");
		CHECK(strstr(out, "joined x.x.x.x:2302 and x.x.x.x, local 127.0.0.1, any 0.0.0.0\n"), "the addresses");
		CHECK(strstr(out, "Vulkan 1.3.280, driver 24.1.3; version 300.1.2.3.4\n"), "versions left as they are");
		CHECK(strstr(out, "22:55:20  chat: (a chat line, removed from the report)\n"), "a team chat line");
		CHECK(!strstr(out, "warthog") && !strstr(out, "Cortana"), "a chat line left");
		CHECK(strstr(out, "22:55:22  chat notice: (removed)\n") && !strstr(out, "Johnson joined"), "a notice's name");
		CHECK(!strstr(out, "hi\n") && !strstr(out, "x\n") && strstr(out, "\nchat: (a chat line, removed from the "
			"report)\nchat: (a chat line, removed from the report)\n"), "a short chat line");
		CHECK(strstr(out, "host notice: (removed)\n") && !strstr(out, "12345"), "the host's notice, a Discord id");
		CHECK(strstr(out, "Discord link: linked as (removed)\n"), "a Discord name");
		CHECK(strstr(out, "(the page shows the code (removed))\n") && !strstr(out, "K7Q2"), "a link's code");
		CHECK(strstr(out, "look up network.public_address (removed)\n"), "network.public_address's host name");
		CHECK(strstr(out, "network.public_address (removed) is IPv6"), "network.public_address's IPv6 address");
		CHECK(strstr(out, "v6 x::x and x::x%en0, mapped ::ffff:x.x.x.x, loopback ::1\n"), "the IPv6 addresses");
		CHECK(strstr(out, "times 12:30:45 and 12:30:45.250, std::vector, a deadbeef::cafe, build 1.2.3, aa:bb\n"),
			"times, versions and words taken for IPv6 addresses");
		CHECK(strstr(out, "chats are fine\n"), "a word taken for a chat line");
		CHECK(strstr(out, "chat: the on-screen keyboard opened\n"), "the chat's own line, of no player, taken out");
		CHECK(size >= 21 && !memcmp(out + size - 21, "no newline at the end", 21), "the last line");
		CHECK(report_setting_private("server.password") && report_setting_private("server.web_owner_password") &&
			report_setting_private("network.public_address") && report_setting_private("debug.update_key"),
			"a private setting not left out");
		CHECK(report_setting_private("server.admins") && report_setting_private("server.name") &&
			report_setting_private("server.motd") && report_setting_private("debug.friends") &&
			report_setting_private("debug.update_url") && report_setting_private("paths.data") &&
			report_setting_private("server.publisher_key_file") && report_setting_private("debug.friends_folder") &&
			report_setting_private("audio.output_device") && report_setting_private("network.front") &&
			report_setting_private("debug.screenshot_directory") && report_setting_private("debug.play_log"),
			"a name, a path or an address not left out");
		CHECK(!report_setting_private("display.vsync") && !report_setting_private("network.online") &&
			!report_setting_private("server.max_players") && !report_setting_private("debug.network_test"),
			"a setting left out that is not private");
		free(out);
		/* (a log's end cut mid-line: from its next line) */
		CHECK(report_whole_lines("ohnson: hi\nnext\n", 16, 1) == 11, "a cut log's partial first line");
		CHECK(report_whole_lines("whole\nnext\n", 11, 0) == 0, "a whole log's first line");
		CHECK(report_whole_lines("no line end", 11, 1) == 11, "a cut log of no line's end");
		return 0;
	}
	if (!strcmp(test, "crash"))
	{
		static char const session[] =
			"build number: 99\nbuild: build-1234 (abcdef0)\nplatform: macOS\nstarted: 2026-11-06 21:10:30\n"
			"reference address: 4a1b2c\n";
		static char record[8192];
		static char summary[16384];
		int index;

		strcpy(record, "signal 11 at address 0x10: pc 0000000000401000 lr 0 sp 0 fp 0\n"
			"  in the guest image: addr2line -e halo_guest.elf 0x401000 0x0\n"
			"  x0 0000000000000000\n");
		for (index = 0; index < 30; index++)
			sprintf(record + strlen(record), "  frame %2d: return %016x\n", index, index);
		strcat(record, REPORT_CRASH_LOG_HEADING);
		for (index = 0; index < 20; index++)
			sprintf(record + strlen(record), "log line %d\n", index);
		strcat(record, "a quote: ```\n```\nlast `line`\n");
		report_crash_summary(session, record, summary, sizeof(summary));
		printf("%s", summary);
		CHECK(strstr(summary, "- Build: build-1234 (abcdef0)\n"), "the build");
		CHECK(strstr(summary, "- Started: 2026-11-06 21:10:30\n"), "when it started");
		CHECK(strstr(summary, "- Crash: signal 11 at address 0x10: pc 0000000000401000 lr 0 sp 0 fp 0\n"),
			"the crash's first line");
		CHECK(strstr(summary, "- Reference address: 4a1b2c "), "the reference address");
		CHECK(strstr(summary, "```\n  in the guest image: addr2line"), "the crash's lines in a block");
		CHECK(strstr(summary, "  frame 21: return") && !strstr(summary, "  frame 22: return"),
			"the crash's lines, at most REPORT_CRASH_LINES");
		CHECK(strstr(summary, "(and more: crash.txt)\n"), "the lines left out said");
		CHECK(!strstr(summary, "log line 12\n") && strstr(summary, "log line 13\nlog line 14\n"),
			"the log's last lines only");
		CHECK(strstr(summary, "a quote: '''\n'''\nlast 'line'\n```\n"), "a quoted line's backticks");
		/* (no record: the session's alone) */
		report_crash_summary(session, NULL, summary, sizeof(summary));
		CHECK(strstr(summary, "- Crash: none written"), "no crash written");
		CHECK(!strstr(summary, "```"), "no blocks without a record");
		/* (a room too small: cut, and ended) */
		report_crash_summary(session, record, summary, 100);
		CHECK(strlen(summary) == 99, "cut to its room");
		return 0;
	}
	if (!strcmp(test, "ring"))
	{
		static char ring[16], out[64];
		static uint64_t written;
		char const *expected = "\n--- the log's last lines\nGHIJKLMNOPQRSTUV";
		int pipes[2];
		ssize_t size;
		unsigned long index;

		/* (22 bytes written to a ring of 16: G to V kept, Q to V at its
		start) */
		for (index = 0; index < 22; index++)
			ring[index % sizeof(ring)] = (char)('A' + index);
		written = 22;
		CHECK(pipe(pipes) == 0, "a pipe");
		crash_record_file = pipes[1];
		crash_record_log = ring;
		crash_record_log_size = sizeof(ring);
		crash_record_log_written = &written;
		host_crash_record_line("signal 11");
		host_crash_record_end();
		CHECK(crash_record_file == -1, "the record closed");
		size = read(pipes[0], out, sizeof(out) - 1);
		CHECK(size > 0, "the record read");
		out[size] = 0;
		printf("%s\n", out);
		CHECK(!strncmp(out, "signal 11\n", 10), "the line");
		CHECK(!strcmp(out + 10, expected), "the log, oldest first");
		return 0;
	}
	if (!strcmp(test, "where"))
	{
		static struct report_where where;

		/* (the menus: nothing) */
		memset(&where, 0, sizeof(where));
		strcpy(where.map, "levels\\a10\\a10");
		where.camera = 1;
		report_where_text(&where, text, sizeof(text));
		CHECK(text[0] == 0, "a section for the menus");
		/* (a campaign on Heroic, in a vehicle, its camera wider than the script's takes) */
		where.playing = 1;
		strcpy(where.game, "levels\\a10\\a10 (campaign)");
		where.campaign = 1;
		where.difficulty = 2;
		where.bsp = 1;
		where.ticks = 4500;
		where.ticks_per_second = 30;
		where.player = where.alive = 1;
		where.position[0] = 1.5f;
		where.position[1] = -2.25f;
		where.position[2] = 3.0f;
		where.yaw = 90.0f;
		where.pitch = -10.0f;
		strcpy(where.vehicle, "vehicles\\warthog\\mp_warthog");
		where.camera_position[0] = 1.0f;
		where.camera_position[1] = 2.0f;
		where.camera_position[2] = 3.5f;
		where.camera_yaw = 45.0f;
		where.camera_pitch = -5.0f;
		where.camera_roll = 0.0f;
		where.camera_field_of_view = 110.0f;
		report_where_text(&where, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "\n## Where\n\n- Game: levels\\a10\\a10 (campaign), Heroic\n"), "the game and its difficulty");
		CHECK(strstr(text, "- Time: 4500 ticks (150.0 s), BSP 1\n"), "the time and the BSP");
		CHECK(strstr(text, "- Player: at 1.500 -2.250 3.000, facing 90.0 yaw -10.0 pitch, in vehicles\\warthog\\mp_warthog\n"),
			"the player in a vehicle");
		CHECK(!strstr(text, "- Network:"), "a network game where there is none");
		CHECK(strstr(text, "```\ngame_difficulty_set hard\nmap_name levels\\a10\\a10\nswitch_bsp 1\n"
			"debug_camera_set \"1.000 2.000 3.500 45.0 -5.0 0.0 89.0\"\n```\n"), "the console's lines");
		/* (dead, a multiplayer game hosted: the slowest client's ping, the builds) */
		where.alive = 0;
		where.campaign = 0;
		strcpy(where.game, "levels\\test\\bloodgulch\\bloodgulch (Slayer)");
		where.camera_field_of_view = 70.0f;
		where.network = 1;
		where.rate = 60;
		where.ping = 48;
		where.players = 4;
		where.build_kinds = 3;
		where.builds[0] = 812;
		where.build_counts[0] = 2;
		where.builds[1] = 0;
		where.build_counts[1] = 1;
		where.builds[2] = REPORT_WHERE_OPENCE;
		where.build_counts[2] = 1;
		where.builds_unknown = 1;
		report_where_text(&where, text, sizeof(text));
		printf("%s", text);
		CHECK(strstr(text, "- Game: levels\\test\\bloodgulch\\bloodgulch (Slayer)\n"), "a multiplayer game");
		CHECK(strstr(text, "- Player: dead\n") && !strstr(text, "- Player: at"), "a dead player's place");
		CHECK(strstr(text, "- Network: hosting, GOAT H1 60 Tick, 4 players, ping 48 ms (the slowest client's)\n"),
			"the network game");
		CHECK(strstr(text, "- Other machines' builds: build-812 x2, a local build, OpenCE, 1 not told\n"), "the builds");
		CHECK(!strstr(text, "game_difficulty_set"), "a difficulty for a multiplayer game");
		CHECK(strstr(text, "debug_camera_set \"1.000 2.000 3.500 45.0 -5.0 0.0 70.0\"\n"), "the field of view kept");
		/* (joined a Legacy game, its host silent, the ping not known) */
		where.network = 2;
		where.rate = 0;
		where.ping = -1;
		where.players = 1;
		where.connection_problem = 1;
		where.build_kinds = where.builds_unknown = 0;
		report_where_text(&where, text, sizeof(text));
		CHECK(strstr(text, "- Network: joined, Legacy, 1 player, the host silent\n"), "a client's network game");
		CHECK(!strstr(text, "builds:"), "builds where none were told");
		where.ping = 30;
		report_where_text(&where, text, sizeof(text));
		CHECK(strstr(text, "- Network: joined, Legacy, 1 player, ping 30 ms, the host silent\n"), "a client's ping");
		/* (a small room: cut, ended) */
		report_where_text(&where, text, 40);
		CHECK(strlen(text) == 39, "cut to its room");
		return 0;
	}
	if (!strcmp(test, "zip") && getenv("REPORT_ZIP"))
	{
		struct report_zip zip;
		static unsigned char picture[100000];
		unsigned long index;

		for (index = 0; index < sizeof(picture); index++)
			picture[index] = (unsigned char)(index * 7 + index / 13);
		memset(&zip, 0, sizeof(zip));
		zip.time = (unsigned short)(21 << 11 | 10 << 5 | 30 / 2);
		zip.date = (unsigned short)((2026 - 1980) << 9 | 11 << 5 | 6);
		zip.file = fopen(getenv("REPORT_ZIP"), "wb");
		CHECK(zip.file != NULL, "the zip opened");
		report_zip_add(&zip, "report.md", "OpenH1 report\n", 14);
		report_zip_add(&zip, "empty.txt", "", 0);
		report_zip_add(&zip, "screenshot.png", picture, sizeof(picture));
		CHECK(report_zip_end(&zip), "the zip written");
		CHECK(fclose(zip.file) == 0, "the zip closed");
		return 0;
	}
	if (!strcmp(test, "maps"))
	{
		CHECK(report_map_counted("bloodgulch.map"), "a map");
		CHECK(report_map_counted("Hugeass_1A2B3C4D.MAP"), "its extension in capitals");
		CHECK(!report_map_counted("bitmaps.map") && !report_map_counted("Sounds.map") &&
			!report_map_counted("loc.map"), "the resource maps");
		CHECK(report_map_counted("bitmaps2.map") && report_map_counted("loc.map2.map"), "names that only begin as one does");
		CHECK(!report_map_counted("hugeass.map.partial") && !report_map_counted("readme.txt") &&
			!report_map_counted(".map") && !report_map_counted("._a10.map"), "what is not a map");
		return 0;
	}
	if (!strcmp(test, "folder"))
	{
		static char path[300], url[400];

		/* (the data root ".", where a game started in its own folder, as
		Steam and the menu entry start it, finds its maps: the reports
		folder's address was file:///./reports, which is /reports) */
		report_whole_path("./reports", "/home/someone/Applications/OpenH1", path, sizeof(path));
		printf("%s\n", path);
		CHECK(!strcmp(path, "/home/someone/Applications/OpenH1/reports"), "the data root . after the current folder");
		report_file_url(path, url, sizeof(url));
		printf("%s\n", url);
		CHECK(!strcmp(url, "file:///home/someone/Applications/OpenH1/reports"), "its address the folder's");
		report_whole_path(".", "/srv/halo/", path, sizeof(path));
		CHECK(!strcmp(path, "/srv/halo/"), "the current folder itself");
		report_whole_path("assets/reports", "/src/openh1", path, sizeof(path));
		CHECK(!strcmp(path, "/src/openh1/assets/reports"), "a checkout's assets");
		report_whole_path("/data/halo/reports", "/elsewhere", path, sizeof(path));
		CHECK(!strcmp(path, "/data/halo/reports"), "a whole path kept");
		report_whole_path("C:\\Games\\OpenH1/reports", "D:\\", path, sizeof(path));
		CHECK(!strcmp(path, "C:\\Games\\OpenH1/reports"), "a Windows drive's path kept");
		report_whole_path("./reports", "C:\\Games\\OpenH1", path, sizeof(path));
		CHECK(!strcmp(path, "C:\\Games\\OpenH1/reports"), "after a Windows folder");
		report_file_url(path, url, sizeof(url));
		printf("%s\n", url);
		CHECK(!strcmp(url, "file:///C:/Games/OpenH1/reports"), "a Windows folder's address");
		report_whole_path("./reports", NULL, path, sizeof(path));
		CHECK(!strcmp(path, "./reports"), "no current folder known: as it is");
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
