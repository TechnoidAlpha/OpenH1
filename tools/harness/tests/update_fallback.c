/*
UPDATE_FALLBACK.C (test)

The self-updater's downloads (port/linux/src/updater.c, the whole file:
under_test.inc) against a fake GitHub: each tried again while GitHub is in
trouble (a 5xx, no answer), and a release's file its link cannot give had
from GitHub's API (/releases/assets/<id>, the ids read from its answer
about the release), the bytes checked as any are (release_signature.c and
p2p_crypto.c, the real ones: a signature made here, by the test's key,
debug.update_key). An outage asks the player nothing and shows no dialog; a
refused download is said. test_update_fallback.py takes the code and runs
each case in a process of its own.

Compiled for this computer, not the game's 32-bit ABI (as
update_gamescope.c is, whose SDL it shares: update_sdl.h), with no network.
*/

#include "harness.h"

#include <stdarg.h>

/* ---------- the crypto, as whole files (test_release_signature.py's) */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang optimize off
#include "monocypher.inc"
#define hash_reduce ed25519_hash_reduce
#include "monocypher_ed25519.inc"
#undef hash_reduce
#pragma clang optimize on
#pragma clang diagnostic pop
#undef FOR
#undef COPY
#undef ZERO
#undef WIPE_CTX
#undef WIPE_BUFFER
#undef MIN

#include "p2p_crypto.inc"

void posix_random_bytes(void *buffer, posix_ulong size)
{
	memset(buffer, 0, size);
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + index * 2, "%02x", bytes[index]);
}

#include "release_signature.inc"

#include "update.h"

/* ---------- SDL (waits recorded, not waited) */

#define UPDATE_SDL_OWN_DELAY
#include "update_sdl.h"

static unsigned waited_total, waits;

static void SDL_Delay(Uint32 milliseconds)
{
	/* (the download window's frames: a short real wait) */
	if (milliseconds == 16)
	{
		usleep(1000);
		return;
	}
	__atomic_add_fetch(&waited_total, milliseconds, __ATOMIC_SEQ_CST);
	__atomic_add_fetch(&waits, 1, __ATOMIC_SEQ_CST);
}

/* ---------- the game's */

#define INSTALL_NAME "openh1"
#define TEST_SERVER "https://github.test"
#define TEST_SEED "8a543fbaacb9aaa064eeb749f9cb3c3fc4c0591525d068c0d96f7472708f1c01"
#define OTHER_SEED "86c556326346f528048138fa6180b750571a9cbaa30338625345b38c7d159682"

static char log_text[65536];
static char test_key_hex[65];
static char folder[1024];

static void platform_log(const char *format, ...)
{
	char line[2048];
	va_list arguments;
	size_t used = strlen(log_text);

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	printf("%s\n", line);
	if (used + strlen(line) + 2 < sizeof(log_text))
	{
		strcat(log_text, line);
		strcat(log_text, "\n");
	}
}

static const char *config_string(const char *name)
{
	if (!strcmp(name, "debug.update_url"))
		return TEST_SERVER;
	if (!strcmp(name, "debug.update_key"))
		return test_key_hex;
	return "";
}

static int config_boolean(const char *name)
{
	return !strcmp(name, "update.auto");
}

static double config_real(const char *name)
{
	(void)name;
	return 0.0;
}

static long config_integer(const char *name)
{
	(void)name;
	return 0;
}

static int config_write_boolean(const char *name, int value)
{
	(void)name, (void)value;
	return 1;
}

static int install_is_system(void) { return 0; }
static const char *install_system_reason(void) { return ""; }
static int install_is_tree(void) { return 1; }
static const char *install_folder(void) { return folder; }
static int pc_menu_update_question_open(void) { return 0; }
static void report_session_handover(int leaving) { (void)leaving; }
static void platform_request_quit(void) { }

/* (the system side's files and processes: none is run) */
int update_executable_path(char *path, int size)
{
	snprintf(path, (size_t)size, "%s/bin/openh1", folder);
	return 1;
}

int update_replace_file(const char *path, const char *new_path, const char *old_path)
{
	(void)path, (void)new_path, (void)old_path;
	return 0;
}

int update_make_executable(const char *path) { (void)path; return 1; }
void update_delete_file(const char *path) { remove(path); }
int update_make_directory(const char *path) { return mkdir(path, 0755) == 0 || errno == EEXIST; }
int update_launch(const char *path) { (void)path; return 0; }
int update_restart(const char *path) { (void)path; return 0; }

#include "under_test.inc"

/* ---------- a fake GitHub */

#define BUILD 8
/* the release's files, as updater_asset_name numbers them: SHA256SUMS,
SHA256SUMS.sig, the zip; their ids in the API */
static char file_bytes[UPDATER_ASSET_COUNT][1024];
static const unsigned long long file_ids[UPDATER_ASSET_COUNT] = { 4100001, 4100002, 4100003 };
/* how each answers: its link at github.com (link_status: 302 to the file
host, else that status; link_trouble: that many 503s first), the API's copy
(api_status: 302, or 200 with the bytes, else that status), and whether a
link gets no answer at all (no_answer) */
static int link_status[UPDATER_ASSET_COUNT], link_trouble[UPDATER_ASSET_COUNT], api_status[UPDATER_ASSET_COUNT];
static int no_answer;
/* the latest release's answer lists the files' ids */
static int ids_listed = 1;
/* what was asked */
static int link_requests[UPDATER_ASSET_COUNT], api_requests[UPDATER_ASSET_COUNT], file_host_requests;
static int api_accept_octets;

static int file_index(const char *name)
{
	int index;

	for (index = 0; index < UPDATER_ASSET_COUNT; index++)
	{
		if (!strcmp(name, updater_asset_name(index)))
			return index;
	}
	return -1;
}

static void answer_bytes(struct update_response *response, int status, const char *text)
{
	response->status = status;
	response->body = strdup(text);
	response->body_size = (unsigned long)strlen(text);
}

/* the latest release's answer (GitHub's API's), into text */
static void latest_answer(char *text, size_t size)
{
	char assets[1500] = "";
	int index;

	/* (the release's and the uploader's ids, and a name and id in the
	notes, are not the files') */
	for (index = 0; ids_listed && index < UPDATER_ASSET_COUNT; index++)
	{
		snprintf(assets + strlen(assets), sizeof(assets) - strlen(assets),
			"%s{\"url\": \"" TEST_SERVER "/repos/OpenH1/goath1/releases/assets/%llu\", \"id\": %llu, "
			"\"uploader\": {\"login\": \"github-actions[bot]\", \"id\": 41898282}, \"name\": \"%s\", "
			"\"label\": \"\", \"size\": %d}",
			index ? ", " : "", file_ids[index], file_ids[index], updater_asset_name(index),
			(int)strlen(file_bytes[index]));
	}
	snprintf(text, size, "{\"url\": \"x\", \"id\": 77, \"tag_name\": \"build-%d\", \"author\": {\"id\": 5}, "
		"\"body\": \"Network version: 3\\n\\\"name\\\": \\\"SHA256SUMS\\\", \\\"id\\\": 1\", \"assets\": [%s]}",
		BUILD, assets);
}

/* GitHub's API: the copy of a release's file (/releases/assets/<id>),
its bytes when asked for application/octet-stream (redirected to the file
host, or the answer itself), its description else */
int update_https_request(const char *method, const char *url, const char *headers, const void *body,
	unsigned long body_size, unsigned long maximum, struct update_response *response, char *error, int error_size)
{
	static const char api[] = TEST_SERVER "/repos/OpenH1/goath1/releases/assets/";
	unsigned long long id;
	int index;

	(void)method, (void)body, (void)body_size;
	memset(response, 0, sizeof(*response));
	if (strncmp(url, api, sizeof(api) - 1))
	{
		answer_bytes(response, 404, "");
		return 1;
	}
	id = strtoull(url + sizeof(api) - 1, NULL, 10);
	for (index = 0; index < UPDATER_ASSET_COUNT && file_ids[index] != id; index++)
	{
	}
	if (index == UPDATER_ASSET_COUNT)
	{
		answer_bytes(response, 404, "{\"message\": \"Not Found\"}");
		return 1;
	}
	api_requests[index]++;
	if (!headers || !strstr(headers, "Accept: application/octet-stream\r\n"))
	{
		answer_bytes(response, 200, "{\"id\": 1, \"name\": \"the asset's description, not its bytes\"}");
		return 1;
	}
	api_accept_octets++;
	if (api_status[index] == 200)
	{
		/* (no more than the caller takes) */
		if (strlen(file_bytes[index]) > maximum)
		{
			snprintf(error, (size_t)error_size, "the answer is larger than %lu bytes", maximum);
			return 0;
		}
		answer_bytes(response, 200, file_bytes[index]);
	}
	else if (api_status[index] == 302)
	{
		answer_bytes(response, 302, "");
		snprintf(response->headers, sizeof(response->headers), "Content-Type: text/html\nlocation: "
			"https://files.test/%s?from=api \r\n", updater_asset_name(index));
	}
	else
	{
		answer_bytes(response, api_status[index], "");
	}
	return 1;
}

/* the file at path, the bytes text; 1 when written */
static int write_file(const char *path, const char *text, char *error, int error_size)
{
	FILE *file = fopen(path, "wb");

	if (!file)
	{
		snprintf(error, (size_t)error_size, "could not write %s", path);
		return 0;
	}
	fwrite(text, 1, strlen(text), file);
	fclose(file);
	return 1;
}

/* GitHub's addresses that update_download fetches, following redirects:
the latest release's answer (its API's), a release's links (which
redirect to the file host), and the file host (which the API's copies
redirect to) */
int update_download(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size)
{
	static const char link[] = TEST_SERVER "/OpenH1/goath1/releases/download/build-8/";
	static const char host[] = "https://files.test/";
	char name[256];
	int index;

	(void)maximum;
	if (!strcmp(url, TEST_SERVER "/repos/OpenH1/goath1/releases/latest"))
	{
		char text[4096];

		latest_answer(text, sizeof(text));
		return write_file(path, text, error, error_size);
	}
	if (!strncmp(url, link, sizeof(link) - 1))
	{
		index = file_index(url + sizeof(link) - 1);
		if (index < 0)
		{
			snprintf(error, (size_t)error_size, "the server answered 404");
			return 0;
		}
		link_requests[index]++;
		if (no_answer)
		{
			snprintf(error, (size_t)error_size, "no answer from github.test");
			return 0;
		}
		if (link_trouble[index] > 0)
		{
			link_trouble[index]--;
			snprintf(error, (size_t)error_size, "the server answered 503");
			return 0;
		}
		if (link_status[index] != 302)
		{
			snprintf(error, (size_t)error_size, "the server answered %d", link_status[index]);
			return 0;
		}
	}
	else if (!strncmp(url, host, sizeof(host) - 1))
	{
		file_host_requests++;
		snprintf(name, sizeof(name), "%s", url + sizeof(host) - 1);
		name[strcspn(name, "?")] = 0;
		index = file_index(name);
		if (index < 0)
		{
			snprintf(error, (size_t)error_size, "the server answered 404");
			return 0;
		}
	}
	else
	{
		snprintf(error, (size_t)error_size, "the server answered 404");
		return 0;
	}
	if (!write_file(path, file_bytes[index], error, error_size))
		return 0;
	if (progress)
		progress(context, strlen(file_bytes[index]), strlen(file_bytes[index]));
	return 1;
}

/* ---------- the release */

static void from_hex(const char *text, unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		unsigned int value;

		sscanf(text + index * 2, "%2x", &value);
		bytes[index] = (unsigned char)value;
	}
}

/* the release: its zip's bytes (zip), its SHA256SUMS listing the digest of
listed, signed by seed; the test's key is TEST_SEED's */
static void make_release(const char *zip, const char *listed, const char *seed_hex)
{
	unsigned char seed[32], key[32], digest[32], signature[P2P_SIGNATURE_SIZE], test_seed[32], test_key[32];
	char hex[65];

	from_hex(TEST_SEED, test_seed, 32);
	p2p_ed25519_public(test_seed, test_key, NULL);
	p2p_hex(test_key, 32, test_key_hex);
	from_hex(seed_hex, seed, 32);
	p2p_ed25519_public(seed, key, NULL);
	snprintf(file_bytes[2], sizeof(file_bytes[2]), "%s", zip);
	p2p_sha256(listed, (int)strlen(listed), digest);
	p2p_hex(digest, 32, hex);
	snprintf(file_bytes[0], sizeof(file_bytes[0]),
		"0000000000000000000000000000000000000000000000000000000000000000  OpenH1-Android.apk\n%s  %s\n", hex,
		UPDATE_ASSET);
	p2p_ed25519_sign(seed, key, file_bytes[0], (int)strlen(file_bytes[0]), signature);
	p2p_hex(signature, P2P_SIGNATURE_SIZE, file_bytes[1]);
	strcat(file_bytes[1], "\n");
}

/* the game's folder: an empty one of the test's */
static void setup(const char *game_folder)
{
	int index;

	snprintf(folder, sizeof(folder), "%s", game_folder);
	snprintf(updater_directory, sizeof(updater_directory), "%s", folder);
	update_executable_path(updater_executable, sizeof(updater_executable));
	for (index = 0; index < UPDATER_ASSET_COUNT; index++)
	{
		link_status[index] = 302;
		api_status[index] = 302;
	}
	make_release("the new build", "the new build", TEST_SEED);
}

/* the latest release looked for, as at the start (updater_check_thread),
without its thread: the state it came to */
static int look(void)
{
	updater_check_thread(NULL);
	return SDL_GetAtomicInt(&updater_state);
}

/* the release's zip downloaded (as the background's or a server's is) and
checked: updater_verify's 1 or 0, why in error */
static int download_and_verify(char *error, size_t error_size)
{
	char partial[1200], zip_path[1200];
	int result;

	updater_path(partial, sizeof(partial), updater_partial);
	update_make_directory(partial);
	updater_partial_path(zip_path, sizeof(zip_path), UPDATE_ASSET);
	result = updater_download_asset(UPDATE_ASSET, zip_path, MAXIMUM_UPDATE_SIZE, NULL, NULL, error, error_size);
	if (result != _updater_fetch_got)
		return 0;
	return updater_verify(zip_path, error, error_size);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	char error[1024] = "";

	setvbuf(stdout, NULL, _IOLBF, 0);
	CHECK(argc > 2, "no folder for the game");
	setup(argv[2]);

	/* the files' ids in GitHub's answer about a release: the assets' own,
	not the release's, the uploader's or the notes' */
	CASE("asset-ids")
	{
		static const char good[] = "{\"id\": 1, \"body\": \"\\\"name\\\": \\\"SHA256SUMS\\\", \\\"id\\\": 2\", "
			"\"assets\": [{\"id\": 30, \"uploader\": {\"id\": 31, \"name\": \"SHA256SUMS\"}, \"name\": \"OpenH1.zip\"}, "
			"{\"name\": \"SHA256SUMS\", \"label\": null, \"size\": 3, \"id\": 40}, {\"url\": \"[{\", \"id\": 50, "
			"\"name\": \"SHA256SUMS.sig\"}]}";
		static const char *const bad[] =
		{
			"", "[]", "{", "{\"assets\": [{\"id\": 40, \"name\": \"SHA256SUMS\"}", "{\"assets\": {\"id\": 40, "
			"\"name\": \"SHA256SUMS\"}}", "{\"assets\": [{\"id\": 1234567890123456789, \"name\": \"SHA256SUMS\"}]}",
			"{\"assets\": [{\"id\": \"40\", \"name\": \"SHA256SUMS\"}]}", "{\"assets\": [{\"id\": 4e1, \"name\": "
			"\"SHA256SUMS\"}]}", "{\"assets\": [{\"id\": 40, \"name\": \"SHA256SUMS\\\"\"}]}",
			"{\"assets\": [{\"id\": 40, \"name\": \"SHA256SUMS\" \"x\"}]}", "{\"assets\": [{\"id\": 40, \"name\": "
			"\"SHA256SUMS\"}, ", "{\"assets\": [{\"id\": 40, \"name\": \"SHA256SUMSX\"}]}",
		};
		size_t index;

		CHECK(updater_json_asset_id(good, strlen(good), "SHA256SUMS") == 40, "SHA256SUMS's id was not 40");
		CHECK(updater_json_asset_id(good, strlen(good), "SHA256SUMS.sig") == 50, "SHA256SUMS.sig's id was not 50");
		CHECK(updater_json_asset_id(good, strlen(good), "OpenH1.zip") == 30, "the zip's id was not 30");
		CHECK(updater_json_asset_id(good, strlen(good), "OpenH1-Windows.zip") == 0, "a file not listed had an id");
		/* (not past the bytes given) */
		CHECK(updater_json_asset_id(good, strlen(good) - 3, "SHA256SUMS.sig") == 0, "an answer cut short was read");
		for (index = 0; index < sizeof(bad) / sizeof(*bad); index++)
			CHECK(updater_json_asset_id(bad[index], strlen(bad[index]), "SHA256SUMS") == 0, "an id from '%s'", bad[index]);
		return 0;
	}

	/* a redirect's address: https:// only, or a path on the same host, no
	longer than its buffer, no spaces or control characters */
	CASE("redirects")
	{
		char location[64];
		char long_headers[200];

		CHECK(updater_location("A: b\nLOCATION:  https://files.test/x?y=1\r\n", "https://github.test/a",
			location, sizeof(location)) && !strcmp(location, "https://files.test/x?y=1"), "an https:// address: %s",
			location);
		CHECK(updater_location("Location: /b/c\n", "https://github.test:8443/a/b", location, sizeof(location)) &&
			!strcmp(location, "https://github.test:8443/b/c"), "a path on the host: %s", location);
		CHECK(!updater_location("Location: http://files.test/x\n", "https://github.test/a", location,
			sizeof(location)), "an http:// address was followed");
		CHECK(!updater_location("Location: //files.test/x\n", "https://github.test/a", location, sizeof(location)),
			"a scheme-relative address was followed");
		CHECK(!updater_location("Location: https://files.test/x y\n", "https://github.test/a", location,
			sizeof(location)), "an address with a space was followed");
		CHECK(!updater_location("Location: https://files.test/\x01\n", "https://github.test/a", location,
			sizeof(location)), "an address with a control character was followed");
		CHECK(!updater_location("Location:\n", "https://github.test/a", location, sizeof(location)),
			"an empty address was followed");
		CHECK(!updater_location("X-Location: https://files.test/x\n", "https://github.test/a", location,
			sizeof(location)), "another header was taken for Location");
		snprintf(long_headers, sizeof(long_headers), "Location: https://files.test/%080d\n", 0);
		CHECK(!updater_location(long_headers, "https://github.test/a", location, sizeof(location)),
			"an address longer than its buffer was followed");
		/* (update_download's failures: the server's trouble passes, this
		computer's does not) */
		CHECK(updater_download_failure("the server answered 504") == _updater_fetch_unreachable, "504");
		CHECK(updater_download_failure("no answer from files.test") == _updater_fetch_unreachable, "no answer");
		CHECK(updater_download_failure("the server answered 403") == _updater_fetch_failed, "403");
		CHECK(updater_download_failure("the server answered 404") == _updater_fetch_none, "404");
		CHECK(updater_download_failure("could not write /x (No space left on device)") == _updater_fetch_failed,
			"a full disk");
		CHECK(updater_download_failure("the download from files.test is larger than expected") ==
			_updater_fetch_failed, "too large");
		return 0;
	}

	/* the release's links working: no API, no waits */
	CASE("links")
	{
		CHECK(look() == _updater_available, "the release was not found: %s", log_text);
		CHECK(download_and_verify(error, sizeof(error)), "%s", error);
		CHECK(strstr(log_text, "is signed by OpenH1's release key"), "not checked as signed: %s", log_text);
		CHECK(api_requests[0] + api_requests[1] + api_requests[2] == 0, "GitHub's API was asked");
		CHECK(waits == 0, "waited %u times", waits);
		return 0;
	}

	/* a 503 from a link, then its file: tried again after a short wait */
	CASE("retry")
	{
		link_trouble[0] = 1;
		link_trouble[2] = 2;
		CHECK(look() == _updater_available, "the release was not found: %s", log_text);
		CHECK(download_and_verify(error, sizeof(error)), "%s", error);
		CHECK(link_requests[0] == 2 && link_requests[2] == 3, "SHA256SUMS asked %d times, the zip %d",
			link_requests[0], link_requests[2]);
		CHECK(waits == 3 && waited_total == 1000 + 1000 + 3000, "%u waits, %u ms", waits, waited_total);
		CHECK(api_requests[0] + api_requests[2] == 0, "GitHub's API was asked");
		return 0;
	}

	/* every link answering 504 (GitHub's outage of its release links): each
	file had from GitHub's API instead, its bytes checked as any are */
	CASE("fallback")
	{
		int index;

		for (index = 0; index < UPDATER_ASSET_COUNT; index++)
			link_status[index] = 504;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		CHECK(download_and_verify(error, sizeof(error)), "%s", error);
		CHECK(strstr(log_text, "is signed by OpenH1's release key"), "not checked as signed: %s", log_text);
		for (index = 0; index < UPDATER_ASSET_COUNT; index++)
		{
			CHECK(link_requests[index] == UPDATER_FETCH_TRIES, "%s's link asked %d times", updater_asset_name(index),
				link_requests[index]);
			CHECK(api_requests[index] == 1, "%s asked of the API %d times", updater_asset_name(index),
				api_requests[index]);
		}
		CHECK(api_accept_octets == 3, "the API was not asked for the files' bytes");
		CHECK(strstr(log_text, "SHA256SUMS came from GitHub's API"), "%s", log_text);
		return 0;
	}

	/* no answer at all from the links (a reset, a timeout), and the API's
	answer the bytes themselves (200, no redirect) */
	CASE("fallback-direct")
	{
		int index;

		no_answer = 1;
		for (index = 0; index < UPDATER_ASSET_COUNT; index++)
			api_status[index] = 200;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		CHECK(download_and_verify(error, sizeof(error)), "%s", error);
		CHECK(file_host_requests == 0, "the file host was asked");
		return 0;
	}

	/* a 404 from a link the release lists: had from the API */
	CASE("fallback-listed")
	{
		link_status[0] = 404;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		CHECK(download_and_verify(error, sizeof(error)), "%s", error);
		CHECK(link_requests[0] == 1 && api_requests[0] == 1, "SHA256SUMS: link %d, API %d", link_requests[0],
			api_requests[0]);
		return 0;
	}

	/* a file the release does not list, and whose link answers 404: not
	there (not asked of the API, which has no id for it), and a release
	without its SHA256SUMS is refused (RELEASE_SIGNATURE_REQUIRED) */
	CASE("not-listed")
	{
		ids_listed = 0;
		link_status[0] = 404;
		link_status[1] = 404;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		CHECK(!download_and_verify(error, sizeof(error)), "a release without SHA256SUMS was taken");
		CHECK(strstr(error, "was refused"), "%s", error);
		CHECK(api_requests[0] + api_requests[1] + api_requests[2] == 0, "GitHub's API was asked");
		CHECK(waits == 0, "a 404 was tried again");
		return 0;
	}

	/* the API's copy of the signature another key's: refused, as it would be
	from the links */
	CASE("fallback-bad-signature")
	{
		int index;

		make_release("the new build", "the new build", OTHER_SEED);
		for (index = 0; index < UPDATER_ASSET_COUNT; index++)
			link_status[index] = 504;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		CHECK(!download_and_verify(error, sizeof(error)), "a release signed by another key was taken");
		CHECK(strstr(error, "was refused"), "%s", error);
		CHECK(!updater_unreachable, "a refusal was taken for an outage");
		CHECK(api_requests[1] == 1, "the signature was not had from the API");
		return 0;
	}

	/* the API's copy of the zip not the one SHA256SUMS lists: refused, and
	deleted */
	CASE("fallback-bad-zip")
	{
		char zip_path[1200];

		make_release("another build", "the new build", TEST_SEED);
		link_status[2] = 504;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		CHECK(!download_and_verify(error, sizeof(error)), "a zip SHA256SUMS does not list was taken");
		CHECK(strstr(error, "was refused"), "%s", error);
		CHECK(api_requests[2] == 1, "the zip was not had from the API");
		updater_partial_path(zip_path, sizeof(zip_path), UPDATE_ASSET);
		CHECK(!SDL_GetPathInfo(zip_path, NULL), "the refused download was kept");
		return 0;
	}

	/* the links and the API both in trouble as the game starts: the player
	is not asked, nothing is shown, and the next start looks again */
	CASE("outage-at-start")
	{
		int index;

		for (index = 0; index < UPDATER_ASSET_COUNT; index++)
		{
			link_status[index] = 504;
			api_status[index] = 502;
		}
		CHECK(look() == _updater_handled, "the player was asked about a release whose signature could not be had");
		CHECK(strstr(log_text, "GitHub is not answering"), "%s", log_text);
		updater_poll(NULL);
		CHECK(dialogs_shown == 0, "%d dialog(s) shown", dialogs_shown);
		return 0;
	}

	/* the player's Yes (the desktop's dialog), and then the zip cannot be
	had for an outage: logged, no dialog, the game goes on */
	CASE("outage-after-yes")
	{
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		link_status[2] = 504;
		api_status[2] = 503;
		updater_update();
		CHECK(strstr(log_text, "update: failed: "), "%s", log_text);
		CHECK(strstr(log_text, "GitHub could not be reached"), "%s", log_text);
		CHECK(dialogs_shown == 0, "a dialog for an outage");
		return 0;
	}

	/* ... and a download that came but is refused: said, in a dialog */
	CASE("refusal-after-yes")
	{
		make_release("the new build", "the new build", OTHER_SEED);
		link_status[1] = 504;
		CHECK(look() == _updater_available, "the release was not asked about: %s", log_text);
		updater_update();
		CHECK(strstr(log_text, "was refused"), "%s", log_text);
		CHECK(dialogs_shown == 1, "%d dialog(s) for a refused download", dialogs_shown);
		return 0;
	}
	return 2;
}
