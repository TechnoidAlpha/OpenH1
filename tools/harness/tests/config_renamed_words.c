/*
CONFIG_RENAMED_WORDS.C (test)

An older version's word for a setting (port/linux/src/port_config.c's
config_renamed_words: display.ambient_occlusion's "subtle" and "strong",
the screen's occlusion, gone) read as the word in its place ("world"), in
config.toml (written again with it, the setting's comment as this version
writes it: config_migrate_words) and from the environment
(config_check_words), in any case; this version's words kept as they are.
test_config_renamed_words.py takes the code and its tables from the
sources; the settings are the test's.
*/

#include "harness.h"

#include <assert.h>
#include <ctype.h>

/* (tomlc17 is C11's; its warnings are its own) */
#ifndef static_assert
#define static_assert _Static_assert
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "toml.inc"
#pragma clang diagnostic pop

#include "types.inc"

static const struct config_setting config_settings[] =
{
	{ "display.vsync", _config_boolean, "true", "HALO_VSYNC", _environment_value, _platform_all, "Vsync." },
	{ "display.ambient_occlusion", _config_string, "\"off\"", "HALO_AMBIENT_OCCLUSION", _environment_value,
		_platform_all, "Ambient occlusion:\n\"off\" or \"world\"." },
};
#define NUMBER_OF_CONFIG_SETTINGS (sizeof(config_settings) / sizeof(config_settings[0]))
#define CONFIG_PLATFORM _platform_all

static struct config_value config_values[NUMBER_OF_CONFIG_SETTINGS];

/* what the log was told */
static char logged[4096];

static void platform_log(const char *format, ...)
{
	va_list arguments;
	size_t length = strlen(logged);

	va_start(arguments, format);
	vsnprintf(logged + length, sizeof(logged) - length, format, arguments);
	va_end(arguments);
	strncat(logged, "\n", sizeof(logged) - strlen(logged) - 1);
}

static const char *config_default_of(const struct config_setting *setting)
{
	return setting->default_value;
}

#include "under_test.inc"

/* the file migrated: its text (NULL: left as it is) */
static char *migrated(const char *text)
{
	toml_result_t result = toml_parse(text, (int)strlen(text));
	char *out;

	CHECK(result.ok, "the test's file does not parse: %s", result.errmsg);
	out = config_migrate_words(text, result.toptab);
	toml_free(result);
	return out;
}

/* the setting's value in the text, as TOML reads it ("" none) */
static const char *value_in(const char *text)
{
	static char value[64];
	toml_result_t result = toml_parse(text, (int)strlen(text));
	toml_datum_t datum;

	CHECK(result.ok, "the migrated file does not parse: %s\n%s", result.errmsg, text);
	datum = toml_seek(result.toptab, "display.ambient_occlusion");
	snprintf(value, sizeof(value), "%s", datum.type == TOML_STRING ? datum.u.s : "");
	toml_free(result);
	return value;
}

/* the value as the settings read it, from the environment's text */
static const char *read_as(const char *text)
{
	free(config_values[1].string);
	config_values[1].string = strdup(text);
	config_check_words();
	return config_values[1].string;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	static const char *const older[] = { "strong", "subtle", "Strong", "SUBTLE" };
	char text[512];
	size_t index;

	/* config.toml with an older word: written with "world", the comment
	this version's, the other settings as they were */
	CASE("file")
	{
		for (index = 0; index < sizeof(older) / sizeof(older[0]); index++)
		{
			char *out;

			snprintf(text, sizeof(text), "[display]\nvsync = false\n# An old comment.\nambient_occlusion = \"%s\"\n"
				"\n[audio]\nvolume = 3\n", older[index]);
			out = migrated(text);
			CHECK(out, "\"%s\": the file was left as it was", older[index]);
			CHECK(!strcmp(value_in(out), "world"), "\"%s\" was written as \"%s\":\n%s", older[index], value_in(out),
				out);
			CHECK(strstr(out, "# \"off\" or \"world\".\n") && !strstr(out, "An old comment"),
				"\"%s\": the comment is not this version's:\n%s", older[index], out);
			CHECK(strstr(out, "vsync = false\n") && strstr(out, "[audio]\nvolume = 3\n"),
				"\"%s\": the other settings were changed:\n%s", older[index], out);
			CHECK(strstr(logged, "display.ambient_occlusion"), "\"%s\": the log was not told", older[index]);
			free(out);
		}
		return 0;
	}
	/* this version's words, a commented default and no setting: left as
	they are */
	CASE("kept")
	{
		static const char *const files[] =
		{
			"[display]\nambient_occlusion = \"off\"\n",
			"[display]\nambient_occlusion = \"world\"\n",
			"[display]\nambient_occlusion = \"ray_traced\"\n",
			"[display]\n# ambient_occlusion = \"strong\"\n",
			"[display]\nvsync = true\n",
			"[other]\nambient_occlusion = \"strong\"\n",
		};

		for (index = 0; index < sizeof(files) / sizeof(files[0]); index++)
			CHECK(!migrated(files[index]), "a file without an older word was written again:\n%s", files[index]);
		return 0;
	}
	/* the environment's (and a file's that could not be written): read as
	"world"; this version's words, and an unknown one, as they are */
	CASE("read")
	{
		for (index = 0; index < sizeof(older) / sizeof(older[0]); index++)
			CHECK(!strcmp(read_as(older[index]), "world"), "\"%s\" was read as \"%s\"", older[index],
				config_values[1].string);
		CHECK(!strcmp(read_as("off"), "off"), "\"off\" was read as \"%s\"", config_values[1].string);
		CHECK(!strcmp(read_as("World"), "world"), "\"World\" was read as \"%s\"", config_values[1].string);
		CHECK(!strcmp(read_as("ray_traced"), "ray_traced"), "\"ray_traced\" was read as \"%s\"", config_values[1].string);
		logged[0] = 0;
		CHECK(!strcmp(read_as("bright"), "bright") && strstr(logged, "is not \"off\", \"world\" or \"ray_traced\""),
			"an unknown word: \"%s\", logged %s", config_values[1].string, logged);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
