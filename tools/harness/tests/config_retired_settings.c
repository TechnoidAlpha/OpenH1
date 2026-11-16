/*
CONFIG_RETIRED_SETTINGS.C (test)

A setting an older version had that this one has not
(port/linux/src/port_config.c's config_retired_settings:
display.text_renderer): taken out of config.toml as it is read, set or at
its commented default, with the comment above it and the blank line before
that (config_migrate_retired), the other settings as they were; said in the
log once; and never reported as an unknown setting (config_setting_retired).
test_config_retired_settings.py takes the code and its list from the
sources.
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

#include "under_test.inc"

/* the file as this version writes it around the setting */
#define BEFORE "# head\n\n[display]\n\n# Vsync.\n# vsync = true\n"
#define AFTER "\n# Shadows.\nshadow_resolution = 512\n\n[audio]\nvolume = 3\n"
#define BLOCK(line) "\n# How the high-res text's letters are drawn: \"raster\"\n# or \"msdf\".\n" line

static void parses(const char *text)
{
	toml_result_t result = toml_parse(text, (int)strlen(text));

	CHECK(result.ok, "the migrated file does not parse: %s\n%s", result.errmsg, text);
	CHECK(toml_seek(result.toptab, "display.text_renderer").type == TOML_UNKNOWN,
		"the setting is still in the file:\n%s", text);
	CHECK(toml_seek(result.toptab, "display.shadow_resolution").type == TOML_INT64 &&
		toml_seek(result.toptab, "audio.volume").type == TOML_INT64, "the other settings were lost:\n%s", text);
	toml_free(result);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	/* a value the player set: the block gone, the file as if it had never
	had it; the log told */
	CASE("set")
	{
		static const char *const values[] = { "text_renderer = \"msdf\"\n", "text_renderer = \"raster\"\n",
			"  text_renderer=\"msdf\"  # mine\n" };
		size_t index;

		for (index = 0; index < sizeof(values) / sizeof(values[0]); index++)
		{
			char text[1024];
			char *out;

			logged[0] = 0;
			snprintf(text, sizeof(text), BEFORE BLOCK("%s") AFTER, values[index]);
			out = config_migrate_retired(text);
			CHECK(out, "the file was left as it was:\n%s", text);
			CHECK(!strcmp(out, BEFORE AFTER), "the file is not as if it had never had it:\n%s", out);
			parses(out);
			CHECK(strstr(logged, "display.text_renderer"), "the log was not told");
			free(out);
		}
		return 0;
	}
	/* the commented default this version's file had: gone too, without a
	word in the log; and both, a value under a commented default */
	CASE("commented")
	{
		char *out;

		logged[0] = 0;
		out = config_migrate_retired(BEFORE BLOCK("# text_renderer = \"raster\"\n") AFTER);
		CHECK(out && !strcmp(out, BEFORE AFTER), "the commented default was not taken out:\n%s", out ? out : "");
		CHECK(!logged[0], "the log was told of a setting the player never set: %s", logged);
		free(out);
		out = config_migrate_retired(BEFORE BLOCK("# text_renderer = \"raster\"\ntext_renderer = \"msdf\"\n") AFTER);
		CHECK(out && !strstr(out, "text_renderer"), "a line of it was left:\n%s", out ? out : "");
		parses(out);
		free(out);
		return 0;
	}
	/* a file without it, or with it in another section: left as it is */
	CASE("kept")
	{
		static const char *const files[] =
		{
			BEFORE AFTER,
			"[audio]\ntext_renderer = \"msdf\"\n",
			"",
		};
		size_t index;

		for (index = 0; index < sizeof(files) / sizeof(files[0]); index++)
			CHECK(!config_migrate_retired(files[index]), "a file without it was written again:\n%s", files[index]);
		return 0;
	}
	/* (config_report_unknown_keys) a retired setting is not unknown; a
	misspelt one is */
	CASE("known")
	{
		CHECK(config_setting_retired("display.text_renderer"), "display.text_renderer is reported as unknown");
		CHECK(!config_setting_retired("display.text_rendere"), "a misspelt setting is taken for a retired one");
		CHECK(!config_setting_retired("audio.text_renderer"), "another section's is taken for the retired one");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
