/* The tag validator's walk (port/linux/game/tag_validate.c) leaves out the
passes an element has nothing to do in (definition_summary_get): every
extent, value and check it reaches, and every refusal, is the same, in the
same order, as the walk of every pass over every element makes. */
#include "harness.h"
#include <stdarg.h>
#include <stddef.h>

#include "enums.inc"

struct tag_block { long count; void *address; void *definition; };
struct tag_data { long size; long flags; long file_offset; void *address; void *definition; };
struct tag_validation;
typedef boolean (*tag_schema_check_proc)(struct tag_validation *validation, void *base);
struct tag_schema_definition;
#include "structures.inc"

/* what the walk did, one line each */
static char trace[1 << 20];
static long trace_length;
static long trace_lines;
static void traced(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	trace_length += vsnprintf(trace + trace_length, sizeof(trace) - trace_length, format, arguments);
	va_end(arguments);
	trace_lines++;
}
/* the walk's own: what each pass does to a field, traced */
static void tag_validate_refuse(struct tag_validation *validation, char const *format, ...)
{
	validation->refused = TRUE;
	traced("refused %s at depth %d\n", format, validation->depth);
}
static void validate_block_extent(struct tag_validation *validation, struct tag_block *block,
	struct tag_schema_field const *field)
{
	(void)validation;
	traced("extent %s %p\n", field->name, (void *)block);
}
static void validate_data_extent(struct tag_validation *validation, struct tag_data *data,
	struct tag_schema_field const *field)
{
	(void)validation;
	traced("data %s %p\n", field->name, (void *)data);
}
static void validate_value(struct tag_validation *validation, byte *address, struct tag_schema_field const *field)
{
	traced("value %s %p structure %p\n", field->name, (void *)address, (void *)validation->structure);
}
static boolean check_traced(struct tag_validation *validation, void *base)
{
	traced("check %p depth %d\n", base, validation->depth);
	return TRUE;
}
static boolean check_refuses(struct tag_validation *validation, void *base)
{
	(void)validation;
	traced("check refuses %p\n", base);
	return FALSE;
}

/* the walk with every pass (summaries off) or with the summaries */
static boolean summaries_off;
static struct definition_summary const *definition_summary_get(struct tag_schema_definition const *definition);
static void validate_element(struct tag_validation *validation, byte *base,
	struct tag_schema_definition const *definition, char const *field_name, long element_index);
#define summary_of(definition) (summaries_off ? NULL : definition_summary_get(definition))

#include "under_test.inc"

#define FIELD(kind, structure, member, definition) \
	{ kind, sizeof(((structure *)0)->member), 1, 0, (long)offsetof(structure, member), 0, 0, 0, definition, NULL, #member }
#define CHECK_FIELD(proc) { _tag_schema_check, 0, 1, 0, 0, 0, 0, 0, NULL, proc, #proc }
#define END { _tag_schema_terminator, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL }

/* a leaf of values (a collision edge's indices), a leaf with a check, one
with data, and an element with blocks of each and data */
struct leaf { short a, b; struct tag_data bytes; };
struct pair { struct tag_block values, checked, data; struct tag_data bytes; short kind; };
struct outer { struct pair inner; struct tag_block pairs; };
static struct tag_schema_field const leaf_fields[] = { FIELD(_tag_schema_enum, struct leaf, a, NULL),
	FIELD(_tag_schema_block_index, struct leaf, b, NULL), END };
static struct tag_schema_definition const leaf_definition = { "leaf", sizeof(struct leaf), leaf_fields };
static struct tag_schema_field const checked_fields[] = { CHECK_FIELD(check_traced), END };
static struct tag_schema_definition const checked_definition = { "checked", sizeof(struct leaf), checked_fields };
static struct tag_schema_field const bytes_fields[] = { FIELD(_tag_schema_data, struct leaf, bytes, NULL), END };
static struct tag_schema_definition const bytes_definition = { "bytes", sizeof(struct leaf), bytes_fields };
static struct tag_schema_field pair_fields[] = { FIELD(_tag_schema_block, struct pair, values, &leaf_definition),
	FIELD(_tag_schema_block, struct pair, checked, &checked_definition),
	FIELD(_tag_schema_block, struct pair, data, &bytes_definition),
	FIELD(_tag_schema_data, struct pair, bytes, NULL), FIELD(_tag_schema_enum, struct pair, kind, NULL), END };
static struct tag_schema_definition const pair_definition = { "pair", sizeof(struct pair), pair_fields };
/* (a check reached only through a structure's block) */
static struct tag_schema_field const outer_fields[] = { FIELD(_tag_schema_struct, struct outer, inner, &pair_definition),
	FIELD(_tag_schema_block, struct outer, pairs, &pair_definition), END };
static struct tag_schema_definition const outer_definition = { "outer", sizeof(struct outer), outer_fields };

static struct leaf leaves[3][40];
static struct pair pairs[4];
static struct outer outer;

static void world_make(void)
{
	int index;

	memset(&outer, 0, sizeof(outer));
	outer.pairs.count = 4;
	outer.pairs.address = pairs;
	for (index = 0; index < 4; index++)
	{
		struct pair *pair = index < 3 ? &pairs[index] : &outer.inner;

		pairs[index].values.count = 40;
		pairs[index].values.address = leaves[index % 3];
		pairs[index].checked.count = 3;
		pairs[index].checked.address = leaves[(index + 1) % 3];
		pairs[index].data.count = 7;
		pairs[index].data.address = leaves[(index + 2) % 3];
		if (pair != &pairs[index])
			*pair = pairs[index];
	}
}

/* a chain of blocks one element deep, depth elements long, of values or
with a check in each element (so that the walk of the checks shows how
far it went), the last of only a value or a check */
static struct tag_schema_field chain_fields[48][3];
static struct tag_schema_definition chain_definitions[48];
static struct tag_block chain_blocks[48];
static void chain_make(int depth, boolean checked)
{
	int index;

	memset(chain_fields, 0, sizeof(chain_fields));
	for (index = 0; index < depth; index++)
	{
		struct tag_schema_field *field = &chain_fields[index][0];

		chain_definitions[index].name = "chain";
		chain_definitions[index].size = sizeof(struct tag_block);
		chain_definitions[index].fields = field;
		/* (the check before the block: the walk of the checks shows each
		element it reached before it went deeper) */
		if (checked)
			*field++ = (struct tag_schema_field)CHECK_FIELD(check_traced);
		else if (index + 1 == depth)
			*field++ = (struct tag_schema_field)FIELD(_tag_schema_enum, struct leaf, a, NULL);
		if (index + 1 < depth)
		{
			*field = (struct tag_schema_field)FIELD(_tag_schema_block, struct pair, values, &chain_definitions[index + 1]);
			field->offset = 0;
			field->size = sizeof(struct tag_block);
			field++;
		}
		field->type = _tag_schema_terminator;
		chain_blocks[index].count = 1;
		chain_blocks[index].address = &chain_blocks[index + 1];
	}
}

static void walk(struct tag_schema_definition const *definition, void *root, boolean off)
{
	struct tag_validation validation;
	short pass;

	summaries_off = off;
	memset(definition_summaries, 0, sizeof(definition_summaries));
	trace_length = 0;
	trace[0] = 0;
	trace_lines = 0;
	for (pass = _pass_values; pass <= _pass_checks; pass++)
	{
		memset(&validation, 0, sizeof(validation));
		validation.pass = pass;
		validation.structure = root;
		validate_element(&validation, root, definition, NULL, NONE);
		if (validation.refused)
			break;
	}
}

static void same_walks(struct tag_schema_definition const *definition, void *root, char const *what)
{
	static char full[sizeof(trace)];
	long full_lines;

	walk(definition, root, TRUE);
	memcpy(full, trace, sizeof(full));
	full_lines = trace_lines;
	walk(definition, root, FALSE);
	CHECK(full_lines > 0, "%s: the walk did nothing", what);
	CHECK(!strcmp(full, trace), "%s: the walk differs (%ld lines with every pass, %ld with the summaries)\n"
		"every pass:\n%.2000s\nsummaries:\n%.2000s", what, full_lines, trace_lines, full, trace);
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";

	if (!strcmp(test, "elements"))
	{
		world_make();
		same_walks(&outer_definition, &outer, "outer");
		return 0;
	}
	if (!strcmp(test, "refused"))
	{
		/* a check that refuses, deep in a structure's block: the walk stops
		at the same place */
		static struct tag_schema_field refusing[] = { CHECK_FIELD(check_refuses), END };
		struct tag_schema_definition const *saved = pair_fields[1].definition;

		world_make();
		static struct tag_schema_definition const refusing_definition = { "refusing", sizeof(struct leaf), refusing };

		pair_fields[1].definition = &refusing_definition;
		same_walks(&outer_definition, &outer, "refusing");
		pair_fields[1].definition = saved;
		return 0;
	}
	if (!strcmp(test, "deep"))
	{
		/* chains too deep for the walk, with no check in them and with one,
		and one just deep enough */
		int depth;

		for (depth = MAXIMUM_VALIDATION_DEPTH - 1; depth <= MAXIMUM_VALIDATION_DEPTH + 2; depth++)
		{
			chain_make(depth, FALSE);
			same_walks(&chain_definitions[0], &chain_blocks[0], "a chain of values");
			chain_make(depth, TRUE);
			same_walks(&chain_definitions[0], &chain_blocks[0], "a chain with a check");
		}
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
