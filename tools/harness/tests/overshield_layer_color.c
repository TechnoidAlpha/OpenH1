#include "harness.h"
/* The overshield's color by its layer (port/linux/game/overshield_effect.c,
display.overshield_layer_color): the color of the layer the HUD's shield meter
shows on top (hud_unit.c's red, green, yellow, purple), green while the
second layer lasts, red once only the first is left; red throughout with the
setting off. */
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
typedef struct { real red, green, blue; } real_rgb_color;
struct unit_datum { struct { real shield_vitality; } object; };
static struct unit_datum unit;
static boolean unit_exists = TRUE;
static int layers_setting = 1;
static unsigned long changes = 1;
static struct unit_datum *unit_try_and_get(long unit_index){(void)unit_index;return unit_exists ? &unit : NULL;}
static int config_boolean(char const *name){(void)name;return layers_setting;}
static unsigned long config_changes(void){return changes;}
#include "under_test.inc"

static int is_red(real_rgb_color const *color){return color->red > 0.9f && color->green < 0.4f && color->blue < 0.5f;}
static int is_green(real_rgb_color const *color){return color->green > 0.9f && color->red < 0.4f && color->blue < 0.5f;}
static int is_yellow(real_rgb_color const *color){return color->red > 0.9f && color->green > 0.9f && color->blue < 0.4f;}

static real_rgb_color const *at(real shield_vitality)
{
 unit.object.shield_vitality = shield_vitality;
 return overshield_effect_unit_color(7);
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 CASE("powerup") {
  real_rgb_color const *red = &overshield_effect_layer_rgb[0];
  CHECK(red->red == 0.992f && red->green == 0.306f && red->blue == 0.455f, "the first layer: the powerup's color");
  CHECK(is_green(&overshield_effect_layer_rgb[1]), "the second: green");
 } else CASE("full") {
  CHECK(is_green(at(3.f)), "a full overshield (3): green");
 } else CASE("decayed") {
  CHECK(is_green(at(2.5f)), "half the green layer left: green");
  CHECK(is_green(at(2.01f)), "the green layer's last: green");
 } else CASE("red_layer") {
  CHECK(is_red(at(2.f)), "the green layer gone (2): red, as the meter shows only red");
  CHECK(is_red(at(1.5f)), "into the red layer: red");
  CHECK(is_red(at(1.01f)), "the red layer's last: red");
 } else CASE("past_three") {
  CHECK(is_yellow(at(3.5f)), "a shield past 3 (a map's own): the meter's third, yellow");
  CHECK(at(100.f) == &overshield_effect_layer_rgb[NUMBEROF(overshield_effect_layer_rgb) - 1],
   "far past: the last layer's, never past the table");
 } else CASE("off") {
  layers_setting = 0; changes++;
  CHECK(is_red(at(3.f)), "off: a full overshield red");
  CHECK(is_red(at(2.5f)), "off: red with the green layer left");
  layers_setting = 1; changes++;
  CHECK(is_green(at(3.f)), "on again: green, the setting read again on a change");
 } else CASE("no_unit") {
  unit_exists = FALSE;
  CHECK(is_red(at(3.f)), "a unit gone: red, the powerup's");
 } else {
  fprintf(stderr,"unknown case %s\n",case_name);
  return 2;
 }
 return 0;
}
