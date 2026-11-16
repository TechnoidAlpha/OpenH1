/*
VIEWMODEL_WEAPON_CONFIG.C (test)

Each weapon's own first-person offsets in config.toml (port/linux/src/
port_config.c's config_weapons_read, config_viewmodel_weapon and
config_viewmodel_weapon_write): [display.viewmodel_weapon."<tag path>"]
tables read by the real TOML parser, looked up by the path in any case and
with / for \, written in place of the weapon's own table (in any of TOML's
spellings) or at the end, taken out at all 0, and never written to leave the
file unreadable; and a table's long name still a section's line for the
other settings' writes. test_viewmodel_weapon_config.py takes the code from
the sources; the file is the test's.
*/

#include "harness.h"

#include <ctype.h>
#include <pthread.h>

/* (tomlc17 asserts with C11's static_assert) */
#define static_assert _Static_assert
#include "tomlc17.inc"

#define CONFIG_WEAPON_VALUES 6
#define CONFIG_WEAPON_TABLE "display.viewmodel_weapon"

struct config_text
{
	char *buffer;
	size_t length, capacity;
};

static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile unsigned long config_change_count;
static int config_answer_only;
static char *file_text;
static int file_writes;
static int logged;

static void platform_log(const char *format, ...)
{
	(void)format;
	logged++;
}

static void config_ensure_loaded(void)
{
}

static void config_path(char *path, size_t size)
{
	snprintf(path, size, "config.toml");
}

static char *config_read_file(const char *path, size_t *size)
{
	(void)path;
	if (!file_text)
		return NULL;
	*size = strlen(file_text);
	return strdup(file_text);
}

static int config_write_file(const char *path, const char *text)
{
	(void)path;
	free(file_text);
	file_text = strdup(text);
	file_writes++;
	return 1;
}

#include "under_test.inc"

static void file(const char *text)
{
	free(file_text);
	file_text = text ? strdup(text) : NULL;
	file_writes = 0;
}

/* the file parsed, its weapons read again (as config_load does) */
static void load(void)
{
	toml_result_t result = toml_parse(file_text, (int)strlen(file_text));

	CHECK(result.ok, "the file does not read: %s\n%s", result.errmsg, file_text);
	config_weapons_read_global(result.toptab);
	toml_free(result);
}

static int count(const char *text, const char *part)
{
	int found = 0;

	for (; (text = strstr(text, part)); text++)
		found++;
	return found;
}

#define NEAR(a, b) ((a) - (b) < 1e-9 && (b) - (a) < 1e-9)

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	double values[CONFIG_WEAPON_VALUES];

	CASE("read")
	{
		file("[display]\nfov = 90.0\n\n"
			"[display.viewmodel_weapon.\"weapons\\\\pistol\\\\pistol\"]\nx = 2\ny = -1.5\nroll = 7.25\n\n"
			"[display.viewmodel_weapon.'mods/Rifle/rifle']\nyaw = -3.0\nscale = 2.0\npitch = \"up\"\n\n"
			"[audio]\nvolume = 0.5\n");
		load();
		CHECK(config_viewmodel_weapon("weapons\\pistol\\pistol", values), "the pistol's not found");
		CHECK(NEAR(values[0], 2.0) && NEAR(values[1], -1.5) && NEAR(values[2], 0.0) && NEAR(values[5], 7.25),
			"the pistol's %f %f %f %f", values[0], values[1], values[2], values[5]);
		/* (the path in another case, with \ for /) */
		CHECK(config_viewmodel_weapon("MODS\\rifle\\Rifle", values) && NEAR(values[4], -3.0) && NEAR(values[3], 0.0),
			"the rifle's %f %f", values[4], values[3]);
		CHECK(logged == 2, "%d said in the log, not the unknown key and the word", logged);
		values[0] = 9.0;
		CHECK(!config_viewmodel_weapon("weapons\\needler\\needler", values) && NEAR(values[0], 0.0), "a weapon without");
		CHECK(!config_viewmodel_weapon("weapons\\pistol\\pistol2", values), "a longer path taken");
	}
	CASE("write_new")
	{
		static const double set[CONFIG_WEAPON_VALUES] = { 1.5, 0, -2.0, 0, 4.0, 0 };

		file("[display]\nfov = 90.0\n\n[audio]\nvolume = 0.5\n");
		load();
		CHECK(config_viewmodel_weapon_write("weapons\\sniper rifle\\sniper rifle", set) && file_writes == 1, "not written");
		CHECK(strstr(file_text, "[display.viewmodel_weapon.\"weapons\\\\sniper rifle\\\\sniper rifle\"]\nx = 1.5\ny = 0.0\n"
			"z = -2.0\npitch = 0.0\nyaw = 4.0\nroll = 0.0\n") != NULL, "the table:\n%s", file_text);
		CHECK(strstr(file_text, "[display]\nfov = 90.0\n\n[audio]\nvolume = 0.5\n") == file_text, "the rest:\n%s", file_text);
		load();
		CHECK(config_viewmodel_weapon("weapons\\sniper rifle\\sniper rifle", values) && NEAR(values[4], 4.0),
			"read back %f", values[4]);
	}
	CASE("write_replace")
	{
		static const double set[CONFIG_WEAPON_VALUES] = { 0, 3.0, 0, 0, 0, -6.5 };

		file("[display]\nfov = 90.0\n\n[display.viewmodel_weapon.'weapons\\pistol\\pistol']\n# mine\nx = 9.0\n\n"
			"[display.viewmodel_weapon.\"weapons\\\\needler\\\\needler\"]\nz = 1.0\n\n[audio]\nvolume = 0.5\n");
		load();
		CHECK(config_viewmodel_weapon_write("weapons/PISTOL/pistol", set), "not written");
		CHECK(count(file_text, "pistol]") == 0 && count(file_text, "viewmodel_weapon") == 2 && !strstr(file_text, "x = 9.0"), "the old table kept:\n%s", file_text);
		CHECK(strstr(file_text, "fov = 90.0\n\n[display.viewmodel_weapon.\"weapons/PISTOL/pistol\"]\nx = 0.0\ny = 3.0\n")
			!= NULL, "not in its place:\n%s", file_text);
		CHECK(strstr(file_text, "roll = -6.5\n\n[display.viewmodel_weapon.\"weapons\\\\needler\\\\needler\"]\nz = 1.0\n\n"
			"[audio]\nvolume = 0.5\n") != NULL, "the others:\n%s", file_text);
		load();
		CHECK(config_viewmodel_weapon("weapons\\pistol\\pistol", values) && NEAR(values[1], 3.0) && NEAR(values[0], 0.0),
			"read back %f %f", values[1], values[0]);
		CHECK(config_viewmodel_weapon("weapons\\needler\\needler", values) && NEAR(values[2], 1.0), "the needler's");
	}
	CASE("write_zero")
	{
		static const double zero[CONFIG_WEAPON_VALUES] = { 0 };

		file("[display]\nfov = 90.0\n\n[display.viewmodel_weapon.\"weapons\\\\pistol\\\\pistol\"]\nx = 9.0\n\n"
			"[audio]\nvolume = 0.5\n");
		load();
		CHECK(config_viewmodel_weapon_write("weapons\\pistol\\pistol", zero), "not written");
		CHECK(!strstr(file_text, "pistol") && strstr(file_text, "[audio]\nvolume = 0.5\n"), "still there:\n%s", file_text);
		CHECK(!config_viewmodel_weapon("weapons\\pistol\\pistol", values), "still kept");
		/* (and none written at all for a weapon without a table) */
		CHECK(config_viewmodel_weapon_write("weapons\\needler\\needler", zero) && !strstr(file_text, "needler"), "added");
	}
	CASE("refused")
	{
		/* an inline table of the weapon's: a second table would not read */
		static const double set[CONFIG_WEAPON_VALUES] = { 1.0, 0, 0, 0, 0, 0 };
		static const char inline_table[] = "[display.viewmodel_weapon]\n\"weapons\\\\pistol\\\\pistol\" = { x = 2.0 }\n";

		file(inline_table);
		load();
		CHECK(!config_viewmodel_weapon_write("weapons\\pistol\\pistol", set) && file_writes == 0 &&
			!strcmp(file_text, inline_table), "written:\n%s", file_text);
		/* a path that would end its line, and one past a tag's */
		CHECK(!config_viewmodel_weapon_write("weapons\\pis\ntol", set), "a new line taken");
		{
			char path[300];

			memset(path, 'a', sizeof(path) - 1);
			path[sizeof(path) - 1] = 0;
			CHECK(!config_viewmodel_weapon_write(path, set), "a path too long taken");
		}
	}
	CASE("long_section")
	{
		/* a weapon's table's line is a section's, however long: a setting
		after [display]'s goes in [display] */
		char section[64];
		const char *line = "[display.viewmodel_weapon.\"weapons\\\\a very long folder name\\\\an even longer weapon name here\"]";

		CHECK(config_line_section(line, line + strlen(line), section, sizeof(section)) &&
			strncmp(section, "display.viewmodel_weapon", 24) == 0, "not a section");
		{
			static const char twice[] =
				"[display]\nfov = 90.0\n\n"
				"[display.viewmodel_weapon.\"weapons\\\\a very long folder name\\\\an even longer weapon name here\"]\nx = 1.0\n\n"
				"[display.viewmodel_weapon.\"weapons\\\\a very long folder name\\\\an even longer weapon name there\"]\nx = 2.0\n\n"
				"[display]\nvsync = true\n";
			char *joined = config_join_sections(twice);

			CHECK(joined && count(joined, "x = 1.0") == 1 && count(joined, "x = 2.0") == 1 &&
				strstr(joined, "[display]\nfov = 90.0\n\nvsync = true\n"), "joined:\n%s", joined ? joined : "(none)");
			free(joined);
		}
	}
	CASE("profile_zero")
	{
		/* a player profile's weapons (config_profile's): its 0s kept, in
		place of config.toml's; config.toml's own taken out at 0 */
		static const double zero[CONFIG_WEAPON_VALUES] = { 0 };
		static const double some[CONFIG_WEAPON_VALUES] = { 1.0 };
		struct config_weapons profile = { NULL, 0 };

		CHECK(config_weapon_store(&profile, "weapons\\pistol\\pistol", zero, 1) &&
			config_weapon_find(&profile, "weapons/pistol/pistol") == 0, "a profile's 0 not kept");
		CHECK(config_weapon_store(&profile, "weapons\\pistol\\pistol", some, 1) && profile.count == 1 &&
			profile.list[0].values[0] == 1.0, "a profile's not set again in place");
		CHECK(config_weapon_store(&config_weapons, "weapons\\pistol\\pistol", some, 0) &&
			config_weapon_store(&config_weapons, "weapons\\pistol\\pistol", zero, 0) &&
			config_weapon_find(&config_weapons, "weapons\\pistol\\pistol") < 0, "config.toml's 0 kept");
		config_weapons_clear(&profile);
	}
	return 0;
}
