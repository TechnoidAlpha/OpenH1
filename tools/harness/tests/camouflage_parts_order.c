/* A camouflaged object's other parts (groups2: the assault rifle's ammo
counter) after its model parts: once each, after the last of the object's
model parts in their own view (#701). The queue is a fake, sorted as given;
the drawing walks it as rasterizer_transparent_geometry_group_draw does. */
#include "harness.h"
#include <stddef.h>

typedef unsigned short word;
typedef struct { real x, y; } real_point2d;
typedef struct { real i, j; } real_vector2d;
typedef struct { real red, green, blue; } real_rgb_color;
typedef struct { real alpha; real_rgb_color rgb; } real_argb_color;
typedef struct { real_vector3d n; real d; } real_plane3d;
typedef struct { short x, y; } point2d;
#define TAG_STRING_LENGTH 31
#include "types.inc"

#define MAXIMUM_GROUPS 16

static struct transparent_geometry_group groups[MAXIMUM_GROUPS], parts[MAXIMUM_GROUPS];
static short group_count, part_count;
static boolean drawn[MAXIMUM_GROUPS];
static struct shader model_shader, generic_shader;

static struct transparent_geometry_group *rasterizer_transparent_geometry_next_group(
    struct transparent_geometry_group const *group)
{
    long next = group->sorted_index + 1;

    CHECK(group >= groups && group < groups + group_count, "a queued group's next");
    return next < group_count ? &groups[next] : NULL;
}

static boolean rasterizer_transparent_geometry_get_group_pending_status(
    struct transparent_geometry_group const *group)
{
    return !drawn[group - groups];
}

#include "under_test.inc"

enum { WORLD = 0, FIRST_PERSON = 1 };

/* a camouflaged model part of source's (or another object's group, source 0) */
static void queue(long source, int view, boolean camouflage)
{
    struct transparent_geometry_group *group = &groups[group_count];

    memset(group, 0, sizeof(*group));
    group->sorted_index = group_count++;
    group->source_object_index = source;
    group->shader = &model_shader;
    group->effect_type = camouflage ? _render_model_effect_type_active_camouflage : _render_model_effect_type_none;
    group->geometry_flags = view == FIRST_PERSON ? FLAG(_rasterizer_geometry_first_person_bit) : 0;
}

/* a camouflaged object's other part (groups2) */
static void part(long source, int view)
{
    struct transparent_geometry_group *group = &parts[part_count++];

    memset(group, 0, sizeof(*group));
    group->source_object_index = source;
    group->active_camouflage_transparent_source_object_index = source;
    group->shader = &generic_shader;
    group->effect_type = _render_model_effect_type_active_camouflage;
    group->geometry_flags = view == FIRST_PERSON ? FLAG(_rasterizer_geometry_first_person_bit) : 0;
}

/* each part's draws, and after which queued group the last was */
static short draws[MAXIMUM_GROUPS], drawn_after[MAXIMUM_GROUPS];

static void draw_queue(void)
{
    short index, part_index;

    memset(drawn, 0, sizeof(drawn));
    memset(draws, 0, sizeof(draws));
    for (index = 0; index < group_count; index++)
    {
        struct transparent_geometry_group *group = &groups[index];

        drawn[index] = TRUE;
        if (group->effect_type != _render_model_effect_type_active_camouflage ||
            !active_camouflage_last_model_group(group))
            continue;
        for (part_index = 0; part_index < part_count; part_index++)
        {
            if (active_camouflage_part_follows(&parts[part_index], group))
            {
                draws[part_index]++;
                drawn_after[part_index] = index;
            }
        }
    }
}

static void expect(short part_index, short after)
{
    CHECK(draws[part_index] == 1, "part %d drawn %d times", part_index, draws[part_index]);
    CHECK(drawn_after[part_index] == after, "part %d drawn after group %d, expected %d", part_index,
        drawn_after[part_index], after);
}

int main(int argc, char **argv)
{
    char const *case_name;

    CHECK(argc == 2, "one named case");
    case_name = argv[1];
    model_shader.base.type = _shader_type_model;
    generic_shader.base.type = _shader_type_transparent_generic;

    CASE("one-run")
    {
        /* the arms and the weapon together: the counter after both */
        queue(7, FIRST_PERSON, TRUE);
        queue(7, FIRST_PERSON, TRUE);
        part(7, FIRST_PERSON);
        draw_queue();
        expect(0, 1);
        return 0;
    }
    CASE("split-run")
    {
        /* another group sorted between the arms and the weapon: the counter
        once, after the weapon, not again after the arms */
        queue(7, FIRST_PERSON, TRUE);
        queue(0, FIRST_PERSON, FALSE);
        queue(7, FIRST_PERSON, TRUE);
        part(7, FIRST_PERSON);
        draw_queue();
        expect(0, 2);
        return 0;
    }
    CASE("legs")
    {
        /* the body in the world (display.first_person_legs), the world's
        groups, then the first-person arms and weapon: the body's own parts
        after it in the world, the counter after the weapon */
        queue(7, WORLD, TRUE);
        queue(0, WORLD, FALSE);
        queue(7, FIRST_PERSON, TRUE);
        queue(7, FIRST_PERSON, TRUE);
        part(7, FIRST_PERSON);
        part(7, WORLD);
        draw_queue();
        expect(0, 3);
        expect(1, 0);
        return 0;
    }
    CASE("two-objects")
    {
        /* another camouflaged unit's parts after its own model */
        queue(5, WORLD, TRUE);
        queue(7, FIRST_PERSON, TRUE);
        part(7, FIRST_PERSON);
        part(5, WORLD);
        draw_queue();
        expect(0, 1);
        expect(1, 0);
        return 0;
    }
    fprintf(stderr, "unknown case %s\n", case_name);
    return 2;
}
