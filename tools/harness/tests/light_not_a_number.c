#include "harness.h"
#include <math.h>

/* a light let go of fading through its transition (object_lights.c's
light_port_transition_fraction), and the rasterizer's lights refusing one
whose place, reach or colour is not a number (rasterizer_lights.c's
rasterizer_light_submit): the Scorpion's cannon's light, of no transition,
faded by 0/0 */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAXIMUM_LIGHTS_PER_WINDOW 4
typedef struct { real red, green, blue; } real_rgb_color;
struct point_light_definition { long unused; };
enum { _rasterizer_statistics_mode_none, _rasterizer_statistics_mode_geometry };
static struct { long statistics_mode; } rasterizer_debug_options;
static struct { long dynamic_light_count; } rasterizer_frame_statistics;
static int errors;
static void error(long level, char const *format, ...) { (void)level; (void)format; errors++; }
#include "under_test.inc"
static struct rasterizer_lights rasterizer_lights;
#include "under_test_submit.inc"

static struct rasterizer_light_submit_parameters light(void)
{
 struct rasterizer_light_submit_parameters parameters;

 memset(&parameters, 0, sizeof(parameters));
 parameters.position.x = 1.0f; parameters.position.y = 2.0f; parameters.position.z = 3.0f;
 parameters.forward.i = 1.0f; parameters.up.k = 1.0f;
 parameters.color.red = parameters.color.green = parameters.color.blue = 0.5f;
 parameters.radius = 2.0f;
 return parameters;
}

int main(int argc,char **argv)
{
 char const *case_name=argc==2?argv[1]:"";
 CASE("no-transition") {
  /* (made this tick, and drawn a part of a tick before or after) */
  real elapsed[] = { 0.0f, -0.5f, 0.5f, 3.0f };
  unsigned index;
  for (index = 0; index < sizeof(elapsed) / sizeof(elapsed[0]); index++)
  {
   real fraction = light_port_transition_fraction(elapsed[index], 0.0f);
   CHECK(fraction == 1.0f, "a light of no transition %g ticks on: %g, not at its end", elapsed[index], fraction);
  }
 }
 else CASE("transition") {
  CHECK(light_port_transition_fraction(0.0f, 10.0f) == 0.0f, "not at the start as it is let go");
  CHECK(light_port_transition_fraction(5.0f, 10.0f) == 0.5f, "not half way at half its transition");
  CHECK(light_port_transition_fraction(-0.5f, 10.0f) == 0.0f, "before it was let go: not at the start");
  CHECK(!isnan(light_port_transition_fraction(0.0f, NAN)), "a transition not a number: a fraction not a number");
 }
 else CASE("brighten-black") {
  /* (an artificially bright object on a lightmap's black: object_lights.c's
  build_distant_lights) */
  real_rgb_color black = { 0.0f, 0.0f, 0.0f }, dim = { 0.1f, 0.05f, 0.0f }, bright = { 0.9f, 0.6f, 0.3f };
  brighten_real_rgb_color(&black, 0.3f);
  CHECK(black.red == 0.3f && black.green == 0.3f && black.blue == 0.3f, "black brightened to %g %g %g, not grey",
   black.red, black.green, black.blue);
  brighten_real_rgb_color(&dim, 0.3f);
  CHECK(fabsf(dim.red - 0.3f) < 1e-6f && fabsf(dim.green - 0.15f) < 1e-6f && dim.blue == 0.0f, "a dim colour %g %g %g",
   dim.red, dim.green, dim.blue);
  brighten_real_rgb_color(&bright, 0.3f);
  CHECK(fabsf(bright.red - 1.0f) < 1e-6f, "a bright colour %g past white", bright.red);
 }
 else CASE("submitted") {
  struct rasterizer_light_submit_parameters parameters = light();
  CHECK(rasterizer_light_submit(&parameters) == 0 && rasterizer_lights.light_count == 1, "a light refused");
  CHECK(rasterizer_lights.lights[0].radius == 2.0f, "not the light submitted");
 }
 else CASE("not-a-number") {
  real *fields[13];
  real bad[] = { NAN, INFINITY, -INFINITY };
  unsigned field, value;
  for (field = 0; field < 13; field++)
   for (value = 0; value < 3; value++)
   {
    struct rasterizer_light_submit_parameters parameters = light();
    fields[0] = &parameters.radius;
    fields[1] = &parameters.position.x; fields[2] = &parameters.position.y; fields[3] = &parameters.position.z;
    fields[4] = &parameters.forward.i; fields[5] = &parameters.forward.j; fields[6] = &parameters.forward.k;
    fields[7] = &parameters.up.i; fields[8] = &parameters.up.j; fields[9] = &parameters.up.k;
    fields[10] = &parameters.color.red; fields[11] = &parameters.color.green; fields[12] = &parameters.color.blue;
    *fields[field] = bad[value];
    CHECK(rasterizer_light_submit(&parameters) == NONE, "field %u as %g: the light submitted", field, bad[value]);
   }
  CHECK(rasterizer_lights.light_count == 0, "%ld lights not a number kept", rasterizer_lights.light_count);
  CHECK(errors >= 1, "none said");
 }
 else return 2;
 return 0;
}
