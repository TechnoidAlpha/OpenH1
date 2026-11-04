/*
TOUCH_CONTROLS.M

The on-screen touch controls of the iOS app: a UIKit view over SDL's, with a
move stick, the controller's buttons, swipe (and optionally gyroscope)
aiming, a toolbar (hide, options) and a layout editor. It is the Android
app's overlay (port/android/app/src/main/java/com/halo/decomp/TouchControls.java,
TouchLayout.java, TouchContext.java, GyroscopeAim.java) in Objective-C: the
same layout on the same 960 x 540 grid, the same sizes, art (the HUD's
buttons and Kenney's line icons, port/assets/touch/README.md, which
tools/ios_app.py copies into the app from the Android app's resources),
captions, context states and rules for the fingers. Its state goes to the
game through port/android/host/host_touch.c, as port 0's controller, as
Android's does; the game reads it through the host imports there
(port/linux/src/touch_input.c).

It shows only in a game, never at the main menu, over a menu or during a
cinematic, and with input.touch_controls = "auto" only while no
controller, keyboard or mouse is connected. While it is not shown, every
finger passes through it to SDL's view: in the menus SDL makes them the
mouse (SDL_HINT_TOUCH_MOUSE_EVENTS, which this view turns on there and off
in a game, where a tap would otherwise fire), and outside them
touch_input.c takes the taps that skip a cinematic or type in the text
chat.

What differs from Android: the layout is laid out inside the screen's safe
area (clear of the sensor housing and the rounded corners); its options are
a panel of UIKit's; the layout file is exported to and imported from the
app's Documents (touch-layout.txt, which the Files app shows); the phone's
haptics stand in for Android's vibrator.

The game runs on the main thread (host_sdl.c), whose run loop also takes
the fingers, draws this view and commits its layers; so the view draws
nothing per finger. Each control is a small layer of its own whose picture
is drawn once for the look it has (pressed or not, the context's glow,
the captions, the profile's names) and kept; a finger moving the stick
moves the knob's layer, the idle fade is the layers' opacity, and the view
itself has no backing store. The gyroscope's samples are taken on a queue
of their own.
*/

#import <CoreMotion/CoreMotion.h>
#import <CoreText/CoreText.h>
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "host_touch.h"

/* ---------- the layout (TouchLayout.java) */

/* the kinds of control: a button of KINDS, by index */
#define KIND_LEFT 16
#define KIND_FIRE_LEFT 17
#define BASE_COUNT 18
#define MAX_CONTROLS 64
#define MIN_SIZE 0.5f
#define MAX_SIZE 2.0f
#define MIN_OPACITY 0.2f
#define MAX_OPACITY 1.0f
#define MIN_SENSITIVITY 0.25f
#define MAX_SENSITIVITY 4.0f
#define MAX_MINIMUM_RADIUS 34.0f
#define GRID_WIDTH 960.0f
#define GRID_HEIGHT 540.0f
/* on a display taller than the grid (an iPad) the top row (the grid's lines
   down to TOP_ROW) keeps its distance from the top and the rest (from
   LOWER_PART down) its distance from the bottom, so the controls under the
   thumbs sit as low as on a phone; the lines between take the extra height
   (TouchLayout.java's displayY) */
#define TOP_ROW 72.0f
#define LOWER_PART 90.0f
#define LAYOUT_VERSION 3
#define MAX_FILE_LENGTH 65536

/* each kind's default place on the grid (Android's sizes, its places
moved clear of the game's HUD on the iPhone's and the iPad's screens:
tools/test_ios_touch_layout.py). The HUD keeps the corners: the ammunition
and the messages at the upper left, the shields at the upper right, the
motion tracker at the lower left. So the move stick is off the tracker, a
thumb's reach in; pause and back are by the toolbar at the top's middle;
the D-pad, light and grenade type are a cluster at the left, between the
messages and the tracker; crouch is by zoom at the bottom. */
static const float DEFAULTS[BASE_COUNT][2] =
{
	{875, 455}, {760, 465}, {742, 375}, {760, 228}, {862, 310}, {888, 150},
	{500, 470}, {650, 465}, {215, 200}, {215, 270}, {410, 36}, {345, 36},
	{100, 219}, {100, 317}, {51, 268}, {149, 268}, {330, 410}, {255, 239}
};
static const float RADII[BASE_COUNT] =
{
	40, 30, 30, 30, 55, 40, 26, 26, 24, 24, 28, 24, 22, 22, 22, 22, 64, 45
};

struct control
{
	int type;
	float x, y;
	BOOL shown;
	float size;
};

struct layout
{
	struct control controls[MAX_CONTROLS];
	int count;
	float width, height;
	float minimum_radius;
	BOOL rumble, gyroscope, floating_stick;
	float opacity;
};

static float clampf(float value, float low, float high)
{
	return value < low ? low : value > high ? high : value;
}

/* a line of the grid on a display this high, in logical units */
static float display_y(float line, float height)
{
	float extra = height - GRID_HEIGHT;

	if (!(extra > 0.0f))
		return line * height / GRID_HEIGHT;
	if (line <= TOP_ROW)
		return line;
	if (line >= LOWER_PART)
		return line + extra;
	return TOP_ROW + (line - TOP_ROW) * (LOWER_PART - TOP_ROW + extra) / (LOWER_PART - TOP_ROW);
}

/* the grid's line at this height on a display this high: display_y's inverse */
static float grid_y(float y, float height)
{
	float extra = height - GRID_HEIGHT;

	if (!(extra > 0.0f))
		return y * GRID_HEIGHT / height;
	if (y <= TOP_ROW)
		return y;
	if (y >= LOWER_PART + extra)
		return y - extra;
	return TOP_ROW + (y - TOP_ROW) * (LOWER_PART - TOP_ROW) / (LOWER_PART - TOP_ROW + extra);
}

static float layout_radius(const struct layout *layout, int control)
{
	float radius = RADII[layout->controls[control].type] * layout->controls[control].size;

	return radius > layout->minimum_radius ? radius : layout->minimum_radius;
}

/* moves a control, keeping all of it on the display */
static void layout_move(struct layout *layout, int control, float x, float y)
{
	float radius = layout_radius(layout, control);

	if (!isfinite(x) || !isfinite(y))
		return;
	layout->controls[control].x = fmaxf(radius, fminf(layout->width - radius, x));
	layout->controls[control].y = fmaxf(radius, fminf(layout->height - radius, y));
}

static void layout_keep_inside(struct layout *layout)
{
	int index;

	for (index = 0; index < layout->count; index++)
		layout_move(layout, index, layout->controls[index].x, layout->controls[index].y);
}

static void layout_reset(struct layout *layout)
{
	int index;

	layout->count = BASE_COUNT;
	for (index = 0; index < BASE_COUNT; index++)
	{
		struct control *control = &layout->controls[index];

		control->type = index;
		control->x = DEFAULTS[index][0] * layout->width / GRID_WIDTH;
		control->y = display_y(DEFAULTS[index][1], layout->height);
		control->shown = index != KIND_FIRE_LEFT;
		control->size = 1.0f;
	}
	layout_keep_inside(layout);
}

static void layout_initialize(struct layout *layout)
{
	memset(layout, 0, sizeof(*layout));
	layout->width = GRID_WIDTH;
	layout->height = GRID_HEIGHT;
	layout->rumble = YES;
	layout->opacity = 1.0f;
	layout_reset(layout);
}

/* places the layout on a display of this size, in logical units */
static void layout_bounds(struct layout *layout, float width, float height)
{
	int index;

	if (!(width > 0.0f) || !(height > 0.0f))
		return;
	for (index = 0; index < layout->count; index++)
	{
		layout->controls[index].x *= width / layout->width;
		layout->controls[index].y = display_y(grid_y(layout->controls[index].y, layout->height), height);
	}
	layout->width = width;
	layout->height = height;
	layout_keep_inside(layout);
}

static void layout_set_minimum_radius(struct layout *layout, float radius)
{
	layout->minimum_radius = isfinite(radius) && radius > 0.0f ? fminf(radius, MAX_MINIMUM_RADIUS) : 0.0f;
	layout_keep_inside(layout);
}

/* adds a copy of a control beside it; -1 if there may be no more (or it is the stick) */
static int layout_duplicate(struct layout *layout, int source)
{
	struct control *added;
	float offset;
	int index;

	if (layout->count >= MAX_CONTROLS || layout->controls[source].type == KIND_LEFT)
		return -1;
	offset = layout_radius(layout, source) * 2.0f + 12.0f;
	index = layout->count++;
	added = &layout->controls[index];
	*added = layout->controls[source];
	added->shown = YES;
	added->x = layout->controls[source].x + offset;
	if (added->x > layout->width - layout_radius(layout, index))
		added->x = layout->controls[source].x - offset;
	layout_move(layout, index, added->x, added->y);
	return index;
}

/* shows a hidden control of this kind, or else adds a copy; -1 if neither can be */
static int layout_add(struct layout *layout, int type)
{
	int index;

	if (type < 0 || type >= BASE_COUNT)
		return -1;
	for (index = 0; index < layout->count; index++)
	{
		if (layout->controls[index].type == type && !layout->controls[index].shown)
		{
			layout->controls[index].shown = YES;
			return index;
		}
	}
	return layout_duplicate(layout, type);
}

/* the layout as a layout file's text (Java properties, as Android's), with the look sensitivity */
static NSString *layout_export(const struct layout *layout, float sensitivity)
{
	NSMutableString *text = [NSMutableString stringWithString:@"#Halo iOS touch layout\n"];
	int index;

	[text appendFormat:@"format=halo-touch-layout\nversion=%d\nrumble=%s\ngyroscope=%s\nopacity=%g\n"
		"floating-stick=%s\ncount=%d\nsensitivity=%g\n", LAYOUT_VERSION, layout->rumble ? "true" : "false",
		layout->gyroscope ? "true" : "false", layout->opacity, layout->floating_stick ? "true" : "false",
		layout->count, sensitivity];
	for (index = 0; index < layout->count; index++)
	{
		const struct control *control = &layout->controls[index];

		[text appendFormat:@"control.%d.type=%d\ncontrol.%d.x=%g\ncontrol.%d.y=%g\ncontrol.%d.visible=%s\n"
			"control.%d.size=%g\n", index, control->type, index, control->x * GRID_WIDTH / layout->width, index,
			grid_y(control->y, layout->height), index, control->shown ? "true" : "false", index,
			control->size];
	}
	return text;
}

static BOOL property_float(NSDictionary<NSString *, NSString *> *values, NSString *key, float *value)
{
	NSString *text = values[key];
	NSScanner *scanner;
	float read;

	if (!text)
		return NO;
	scanner = [NSScanner scannerWithString:text];
	if (![scanner scanFloat:&read] || !scanner.isAtEnd || !isfinite(read))
		return NO;
	*value = read;
	return YES;
}

static BOOL property_int(NSDictionary<NSString *, NSString *> *values, NSString *key, int *value)
{
	NSString *text = values[key];
	NSScanner *scanner;
	int read;

	if (!text)
		return NO;
	scanner = [NSScanner scannerWithString:text];
	if (![scanner scanInt:&read] || !scanner.isAtEnd)
		return NO;
	*value = read;
	return YES;
}

static BOOL property_boolean(NSDictionary<NSString *, NSString *> *values, NSString *key, BOOL *value)
{
	NSString *text = values[key];

	if ([text isEqualToString:@"true"])
		*value = YES;
	else if ([text isEqualToString:@"false"])
		*value = NO;
	else
		return NO;
	return YES;
}

/* reads a layout file's text (versions 1 to 3); NO for anything not valid */
static BOOL layout_import(NSString *text, struct layout *layout, float *sensitivity)
{
	NSMutableDictionary<NSString *, NSString *> *values = [NSMutableDictionary dictionary];
	struct layout read;
	int version = 0, count = 0, index;
	float read_sensitivity = 0.0f;

	if (!text || text.length > MAX_FILE_LENGTH)
		return NO;
	for (NSString *raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
	{
		NSString *line = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
		NSRange equals;

		if (!line.length || [line hasPrefix:@"#"] || [line hasPrefix:@"!"])
			continue;
		equals = [line rangeOfString:@"="];
		if (equals.location == NSNotFound)
			continue;
		values[[[line substringToIndex:equals.location] stringByTrimmingCharactersInSet:
			NSCharacterSet.whitespaceCharacterSet]] = [[line substringFromIndex:equals.location + 1]
			stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
	}
	if (![values[@"format"] isEqualToString:@"halo-touch-layout"] || !property_int(values, @"version", &version) ||
		version < 1 || version > LAYOUT_VERSION || !property_int(values, @"count", &count) ||
		!property_float(values, @"sensitivity", &read_sensitivity) || count < BASE_COUNT || count > MAX_CONTROLS ||
		read_sensitivity < MIN_SENSITIVITY || read_sensitivity > MAX_SENSITIVITY)
	{
		return NO;
	}
	layout_initialize(&read);
	if (version >= 2 && (!property_boolean(values, @"rumble", &read.rumble) ||
		!property_boolean(values, @"gyroscope", &read.gyroscope)))
	{
		return NO;
	}
	if (version >= 3 && (!property_float(values, @"opacity", &read.opacity) || read.opacity < MIN_OPACITY ||
		read.opacity > MAX_OPACITY || !property_boolean(values, @"floating-stick", &read.floating_stick)))
	{
		return NO;
	}
	read.count = count;
	for (index = 0; index < count; index++)
	{
		struct control *control = &read.controls[index];
		NSString *key = [NSString stringWithFormat:@"control.%d.", index];

		control->size = 1.0f;
		if (!property_int(values, [key stringByAppendingString:@"type"], &control->type) ||
			!property_float(values, [key stringByAppendingString:@"x"], &control->x) ||
			!property_float(values, [key stringByAppendingString:@"y"], &control->y) ||
			!property_boolean(values, [key stringByAppendingString:@"visible"], &control->shown) ||
			(version >= 2 && !property_float(values, [key stringByAppendingString:@"size"], &control->size)))
		{
			return NO;
		}
		if (control->type < 0 || control->type >= BASE_COUNT ||
			(index >= BASE_COUNT ? control->type == KIND_LEFT : control->type != index) ||
			control->x < 0.0f || control->x > GRID_WIDTH || control->y < 0.0f || control->y > GRID_HEIGHT ||
			control->size < MIN_SIZE || control->size > MAX_SIZE)
		{
			return NO;
		}
	}
	/* (places on the grid; layout_bounds scales them to the display) */
	*layout = read;
	*sensitivity = read_sensitivity;
	return YES;
}

/* ---------- the kinds of control, and what the game says of them (TouchControls.java, TouchContext.java) */

struct kind
{
	/* the SDL button bit or trigger axis it sends (-1: none), and the game's
	controller button it is (touch_game.c's numbers), which names it */
	int bit, trigger, gamepad_button;
	const char *button_name;
	BOOL face_button;
	/* a D-pad button's arrow, degrees clockwise from the right (-1: none) */
	int arrow;
};

static const struct kind KINDS[BASE_COUNT] =
{
	{ 0, -1, 0, "A", YES, -1 }, { 1, -1, 1, "B", YES, -1 }, { 2, -1, 2, "X", YES, -1 }, { 3, -1, 3, "Y", YES, -1 },
	{ -1, 5, 7, "RT", NO, -1 }, { -1, 4, 6, "LT", NO, -1 }, { 7, -1, 14, "LS", NO, -1 }, { 8, -1, 15, "RS", NO, -1 },
	{ 9, -1, 5, "White", NO, -1 }, { 10, -1, 4, "Black", NO, -1 }, { 6, -1, 12, "Start", NO, -1 },
	{ 4, -1, 13, "Back", NO, -1 }, { 11, -1, 8, "Up", NO, 270 }, { 12, -1, 9, "Down", NO, 90 },
	{ 13, -1, 10, "Left", NO, 180 }, { 14, -1, 11, "Right", NO, 0 }, { -1, -1, -1, "Move", NO, -1 },
	{ -1, 5, 7, "RT", NO, -1 },
};

static NSString *const GAME_CONTROLS[] =
{
	@"Jump", @"Gren. type", @"Reload", @"Weapon", @"Melee", @"Light", @"Grenade", @"Fire", @"Pause", @"Back",
	@"Crouch", @"Zoom",
};
#define GAME_CONTROL_COUNT 12
static NSString *const CAPTIONS[GAME_CONTROL_COUNT] =
{
	@"JUMP", @"GREN TYPE", @"RELOAD", @"SWAP", @"MELEE", @"LIGHT", @"GRENADE", @"FIRE", @"PAUSE", @"BACK",
	@"CROUCH", @"ZOOM",
};
static const int DEFAULT_BINDINGS[16] = { 0, 4, 2, 3, 1, 5, 6, 7, -1, -1, -1, -1, 8, 9, 10, 11 };

/* the art each kind is drawn with (0..5 a face of the style's, 6..9 the
triggers' and sticks', -1 a line icon), and whether it is seldom used */
static const int KIND_ART[BASE_COUNT] = { 0, 1, 2, 3, 7, 6, 8, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, 7 };
static const BOOL MINOR_KINDS[BASE_COUNT] =
{
	NO, NO, NO, NO, NO, NO, YES, YES, YES, YES, NO, YES, YES, YES, YES, YES, NO, NO,
};
static NSString *const FACES[3][6] =
{
	{ @"touch_button_classic_a", @"touch_button_classic_b", @"touch_button_classic_x", @"touch_button_classic_y",
		@"touch_button_classic_black", @"touch_button_classic_white" },
	{ @"touch_button_gems_a", @"touch_button_gems_b", @"touch_button_gems_x", @"touch_button_gems_y",
		@"touch_button_menu_black", @"touch_button_menu_white" },
	{ @"touch_button_menu_a", @"touch_button_menu_b", @"touch_button_menu_x", @"touch_button_menu_y",
		@"touch_button_menu_black", @"touch_button_menu_white" },
};
static NSString *const CONTROL_ART[4] =
{
	@"touch_trigger_left", @"touch_trigger_right", @"touch_thumbstick_left", @"touch_thumbstick_right",
};

/* the game's scene bits (port/linux/src/touch_input.c) */
enum
{
	SCENE_KNOWN = 1,
	SCENE_MENUS = 2,
	SCENE_ON = 4,
	SCENE_OFF = 8,
	SCENE_CONTROLLER = 16,
	SCENE_STYLE_SHIFT = 5,
	SCENE_CAPTIONS = 128,
	SCENE_CONTEXT_SHIFT = 8,
	/* input.touch_crouch "toggle" (none: "hold") */
	SCENE_CROUCH_TOGGLE = 1 << 19,
};

/* player 1's state (TouchContext.java) */
enum
{
	CONTEXT_ACTION = 1,
	CONTEXT_RELOAD = 2,
	CONTEXT_NO_GRENADES = 4,
	CONTEXT_VEHICLE = 8,
	CONTEXT_ALIVE = 16,
};
#define CONTROL_JUMP 0
#define CONTROL_ACTION 2
#define CONTROL_GRENADE 6
#define CONTROL_CROUCH 10
#define CONTEXT_DIMMED 0.35f
#define PULSE_HZ 1.6f
#define PULSE_SWELL 0.06f

static float context_pulse(double seconds)
{
	double phase = fmod(seconds, 1000.0) * PULSE_HZ * 2.0 * M_PI;

	return (float)(0.5 - 0.5 * cos(phase));
}

static BOOL context_hidden(int control, int context)
{
	return (context & CONTEXT_VEHICLE) && (control == CONTROL_JUMP || control == CONTROL_CROUCH);
}

/* the kind of control crouched (tapped on, -1 standing) once a button of a
kind goes down, doing a game control: with input.touch_crouch "toggle"
Crouch's tap crouches or stands up; Jump stands up */
static int context_crouch_pressed(int crouched, int kind, int control, BOOL toggle)
{
	if (toggle && control == CONTROL_CROUCH)
		return crouched == kind ? -1 : kind;
	return control == CONTROL_JUMP ? -1 : crouched;
}

/* the kind crouched as it stays, its button doing a game control: it stands
up once Crouch is held instead ("hold"), the button no longer crouches (the
profile changed) or hides (a vehicle's seat), or the player dies */
static int context_crouch_kept(int crouched, int control, int context, BOOL toggle)
{
	return crouched >= 0 && toggle && control == CONTROL_CROUCH && !context_hidden(control, context) &&
		(context & CONTEXT_ALIVE) ? crouched : -1;
}

static float context_alpha(int control, int context)
{
	return control == CONTROL_GRENADE && (context & CONTEXT_NO_GRENADES) ? CONTEXT_DIMMED : 1.0f;
}

/* (pulse: context_pulse's, at the time drawn) */
static float context_glow(int control, int context, float pulse)
{
	if (control != CONTROL_ACTION)
		return 0.0f;
	if (context & CONTEXT_RELOAD)
		return 0.2f + 0.8f * pulse;
	return context & CONTEXT_ACTION ? 1.0f : 0.0f;
}

static float context_swell(int control, int context, float pulse)
{
	return control == CONTROL_ACTION && (context & CONTEXT_RELOAD) ? 1.0f + PULSE_SWELL * pulse : 1.0f;
}

/* ---------- the view (TouchControls.java) */

/* what a finger holds besides a control */
#define HOLD_LOOK (-5)
#define HOLD_NOTHING INT_MIN
#define TOOLBAR_TOGGLE (-3)
#define TOOLBAR_OPTIONS (-4)
#define TOOLBAR_EXPORT (-6)
#define TOOLBAR_IMPORT (-7)
#define TOOLBAR_RADIUS 28.0f
#define OPTIONS_RADIUS 34.0f
#define MINIMUM_RADIUS_POINTS 24.0f
#define SLOP 10.0f
#define STICK_DEAD_ZONE 0.12f
#define STICK_REACH 1.28f
#define STICK_KNOB_RADIUS 24.0f
#define FLOATING_ZONE_WIDTH 0.45f
/* (and logical units up from the bottom: 65% of a 16:9 display's height, as low on a taller one) */
#define FLOATING_ZONE_HEIGHT 351.0f
#define LOOK_RADIANS_PER_UNIT 0.0022f
#define BUTTON_ALPHA 0.8f
#define MINOR_BUTTON_ALPHA 0.55f
#define ICON_ALPHA 0.85f
#define MINOR_ICON_ALPHA 0.6f
#define STICK_RING_ALPHA 0.35f
#define STICK_KNOB_ALPHA 0.55f
#define ART_SIZE 1.0f
#define ICON_SIZE 0.62f
#define MINOR_ICON_SIZE 0.56f
#define PRESSED_SIZE 0.9f
#define CAPTION_SIZE 9.0f
#define CAPTION_ALPHA 0.85f
#define IDLE_SECONDS 3.0
#define IDLE_OPACITY 0.7f
#define GLOW_MARGIN 0.25f
#define FADE_OUT_SECONDS 0.6f
#define FADE_IN_SECONDS 0.12f
#define LABEL_WIDTH 1.7f
#define SHOW_DELAY_SECONDS 0.2
#define VIBRATION_RENEW_SECONDS 0.07
/* gyroscope aiming: noise at rest, and the longest gap between samples */
#define GYRO_NOISE 0.01f
#define GYRO_LONGEST_GAP 0.1

static UIColor *hud_color(float alpha)
{
	return [UIColor colorWithRed:0x9c / 255.0 green:0xd3 / 255.0 blue:0xff / 255.0 alpha:alpha];
}

@class HaloTouchOptions;

@interface HaloTouchView : UIView
- (void)resetFingers;
- (void)layoutChanged;
@property (nonatomic) struct layout layoutState;
@property (nonatomic) float sensitivity;
@property (nonatomic) BOOL editing;
@property (nonatomic) BOOL controlsHidden;
@property (nonatomic) BOOL dialogOpen;
@end

@interface HaloTouchOptions : UIViewController
@property (nonatomic, weak) HaloTouchView *controls;
@end

/* (HALO_TOUCH_TEST) a finger of the test's, at a point it is given */
@interface HaloTestTouch : UITouch
@property (nonatomic) CGPoint point;
@end

@implementation HaloTestTouch
- (CGPoint)locationInView:(UIView *)view
{
	(void)view;
	return self.point;
}
@end

@implementation HaloTouchView
{
	struct layout layout;
	/* each finger (by its UITouch): what it holds, where it went down on a
	button (points), a toolbar item's {x, y, item} */
	NSMapTable<UITouch *, NSNumber *> *owners;
	NSMapTable<UITouch *, NSValue *> *button_touches;
	NSMapTable<UITouch *, NSArray<NSNumber *> *> *toolbar_fingers;
	__weak UITouch *look_touch;
	CGPoint look_at;
	int axes[6];
	BOOL stick_floating;
	float stick_x, stick_y;
	__weak UITouch *drag_touch;
	int drag_control;
	float drag_offset_x, drag_offset_y;
	/* the kind of control that crouches (the one bound to Crouch), tapped
	on with input.touch_crouch "toggle": it stays down while the stick
	moves, until it is tapped again, Jump is pressed, a vehicle's seat is
	taken or the player dies; -1 standing (TouchControls.java's crouchedKind) */
	int crouched_kind;

	/* points per logical unit, the layout's origin (the safe area's) and the
	display in logical units */
	float scale;
	CGPoint origin;
	float logical_width, logical_height;

	int scene;
	BOOL showing;
	double shown_since;
	double last_touch, last_fade;
	float idle;
	int bindings[16];
	int bindings_serial;
	int touch_mouse;

	NSDictionary<NSString *, UIImage *> *pictures;
	UIFont *caption_font;
	CADisplayLink *poll;
	CMMotionManager *motion;
	/* the gyroscope's queue, and its state there: what the main thread
	shares with it is atomic (whether it aims, the sensitivity, the
	screen's rotation, a restart of the integration) */
	NSOperationQueue *gyro_queue;
	double gyro_previous_time;
	int gyro_previous_orientation;
	float gyro_previous_x, gyro_previous_y;
	atomic_bool gyro_aiming;
	_Atomic(float) gyro_sensitivity;
	atomic_int gyro_rotation;
	atomic_bool gyro_restart;
	/* the layers: the controls' (with the look each picture was drawn
	for, and the pictures drawn, by their look), the stick's ring and
	knob, the toolbar's, all in one whose opacity is the idle fade */
	CALayer *container, *ring_layer, *knob_layer, *toolbar_layer;
	CALayer *control_layers[MAX_CONTROLS];
	NSMutableDictionary<NSData *, id> *control_pictures[MAX_CONTROLS];
	NSData *control_looks[MAX_CONTROLS];
	NSMutableDictionary<NSData *, id> *stick_pictures;
	NSData *ring_look, *knob_look, *toolbar_look;
	NSMutableDictionary<NSData *, id> *toolbar_pictures;
	float draw_pulse;
	UIImpactFeedbackGenerator *tap_feedback;
	UIImpactFeedbackGenerator *rumble_feedback;
	double last_vibration;
	/* (HALO_TOUCH_TEST) the test's fingers: a thumb circling the move stick
	and one swiping to aim, through fingers: at the display's rate */
	CADisplayLink *test_link;
	HaloTestTouch *test_stick, *test_look;
	double test_start;
}

@synthesize sensitivity, editing, controlsHidden, dialogOpen;

static NSString *const KEY_CONFIGURATION = @"touch-layout-v2.configuration";
static NSString *const KEY_HIDDEN = @"touch-layout-v2.hidden";

- (instancetype)initWithFrame:(CGRect)frame
{
	NSMutableDictionary<NSString *, UIImage *> *loaded = [NSMutableDictionary dictionary];
	NSArray<NSString *> *names = @[ @"touch_icon_pause", @"touch_icon_back", @"touch_icon_arrow",
		@"touch_icon_options", @"touch_icon_hide", @"touch_icon_show" ];
	NSURL *font;
	NSString *saved;
	int style, index;

	self = [super initWithFrame:frame];
	if (!self)
		return nil;
	self.opaque = NO;
	self.backgroundColor = UIColor.clearColor;
	self.multipleTouchEnabled = YES;
	self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	self.accessibilityLabel = @"Halo touch controller";
	owners = [NSMapTable weakToStrongObjectsMapTable];
	button_touches = [NSMapTable weakToStrongObjectsMapTable];
	toolbar_fingers = [NSMapTable weakToStrongObjectsMapTable];
	for (style = 0; style < 3; style++)
		for (index = 0; index < 6; index++)
			names = [names arrayByAddingObject:FACES[style][index]];
	for (index = 0; index < 4; index++)
		names = [names arrayByAddingObject:CONTROL_ART[index]];
	for (NSString *name in names)
	{
		NSString *path = [[NSBundle mainBundle] pathForResource:name ofType:@"png" inDirectory:@"touch"];
		UIImage *image = path ? [UIImage imageWithContentsOfFile:path] : nil;

		if (image)
			loaded[name] = image;
	}
	pictures = loaded;
	/* the captions' font: Overpass, the HUD's text's */
	font = [[NSBundle mainBundle] URLForResource:@"touch_caption" withExtension:@"ttf" subdirectory:@"touch"];
	if (font)
	{
		NSArray *descriptors = CFBridgingRelease(CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)font));
		UIFontDescriptor *descriptor = descriptors.firstObject;

		CTFontManagerRegisterFontsForURL((__bridge CFURLRef)font, kCTFontManagerScopeProcess, NULL);
		if (descriptor)
			caption_font = [UIFont fontWithDescriptor:descriptor size:CAPTION_SIZE];
	}
	if (!caption_font)
		caption_font = [UIFont systemFontOfSize:CAPTION_SIZE weight:UIFontWeightHeavy];
	memcpy(bindings, DEFAULT_BINDINGS, sizeof(bindings));
	layout_initialize(&layout);
	sensitivity = 1.0f;
	saved = [[NSUserDefaults standardUserDefaults] stringForKey:KEY_CONFIGURATION];
	if (saved && !layout_import(saved, &layout, &sensitivity))
		NSLog(@"The saved touch layout could not be read. Using the default one.");
	layout.width = GRID_WIDTH;
	layout.height = GRID_HEIGHT;
	controlsHidden = [[NSUserDefaults standardUserDefaults] boolForKey:KEY_HIDDEN];
	idle = 1.0f;
	scale = 1.0f;
	logical_width = GRID_WIDTH;
	logical_height = GRID_HEIGHT;
	drag_control = -1;
	crouched_kind = -1;
	shown_since = -1.0;
	touch_mouse = -1;
	last_touch = CACurrentMediaTime();
	gyro_previous_orientation = -1;
	tap_feedback = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
	rumble_feedback = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleHeavy];
	motion = [CMMotionManager new];
	gyro_queue = [NSOperationQueue new];
	gyro_queue.maxConcurrentOperationCount = 1;
	gyro_queue.qualityOfService = NSQualityOfServiceUserInteractive;
	atomic_init(&gyro_aiming, false);
	atomic_init(&gyro_sensitivity, sensitivity);
	atomic_init(&gyro_rotation, 0);
	atomic_init(&gyro_restart, true);
	container = [CALayer layer];
	ring_layer = [self pictureLayer];
	knob_layer = [self pictureLayer];
	for (index = 0; index < MAX_CONTROLS; index++)
	{
		control_layers[index] = [self pictureLayer];
		control_pictures[index] = [NSMutableDictionary dictionary];
	}
	toolbar_layer = [self pictureLayer];
	stick_pictures = [NSMutableDictionary dictionary];
	toolbar_pictures = [NSMutableDictionary dictionary];
	container.hidden = YES;
	[self.layer addSublayer:container];
	poll = [CADisplayLink displayLinkWithTarget:self selector:@selector(pollGame:)];
	poll.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
	[poll addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
	if (getenv("HALO_TOUCH_TEST") && strstr(getenv("HALO_TOUCH_TEST"), "stick"))
	{
		if (strstr(getenv("HALO_TOUCH_TEST"), "gyro"))
			layout.gyroscope = YES;
		NSLog(@"touch test: a stick and an aiming finger at 120 Hz, gyroscope %s", layout.gyroscope ? "on" : "off");
		test_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(testFingers:)];
		test_link.preferredFrameRateRange = CAFrameRateRangeMake(120, 120, 120);
		[test_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
	}
	return self;
}

/* (HALO_TOUCH_TEST) moves the test's fingers, putting them down once the controls show */
- (void)testFingers:(CADisplayLink *)link
{
	double now = CACurrentMediaTime(), turn;
	float radius = layout_radius(&layout, KIND_LEFT) * 0.8f;

	(void)link;
	if (!showing || controlsHidden || editing || dialogOpen)
	{
		test_stick = test_look = nil;
		return;
	}
	if (!test_stick || ![owners objectForKey:test_stick])
	{
		test_stick = [HaloTestTouch new];
		test_look = [HaloTestTouch new];
		test_start = now;
		test_stick.point = CGPointMake(origin.x + layout.controls[KIND_LEFT].x * scale,
			origin.y + layout.controls[KIND_LEFT].y * scale);
		test_look.point = CGPointMake(origin.x + logical_width * 0.62f * scale, origin.y + logical_height * 0.6f * scale);
		[self updateSensors];
		[self fingers:[NSSet setWithObjects:test_stick, test_look, nil] phase:UITouchPhaseBegan];
		return;
	}
	turn = (now - test_start) * 2.0 * M_PI;
	test_stick.point = CGPointMake(origin.x + (layout.controls[KIND_LEFT].x + radius * cos(turn * 0.5)) * scale,
		origin.y + (layout.controls[KIND_LEFT].y + radius * sin(turn * 0.5)) * scale);
	test_look.point = CGPointMake(origin.x + (logical_width * 0.62f + 40.0f * sin(turn * 0.4)) * scale,
		origin.y + (logical_height * 0.6f + 10.0f * sin(turn * 0.3)) * scale);
	[self fingers:[NSSet setWithObjects:test_stick, test_look, nil] phase:UITouchPhaseMoved];
}

/* a layer of the container's, holding a picture */
- (CALayer *)pictureLayer
{
	CALayer *layer = [CALayer layer];

	layer.hidden = YES;
	layer.contentsGravity = kCAGravityResize;
	[container addSublayer:layer];
	return layer;
}

- (struct layout)layoutState
{
	return layout;
}

- (void)setLayoutState:(struct layout)state
{
	layout = state;
}

/* ---------- the display */

- (void)layoutSubviews
{
	[super layoutSubviews];
	[self layoutControls];
}

- (void)safeAreaInsetsDidChange
{
	[super safeAreaInsetsDidChange];
	[self layoutControls];
}

/* scales the grid to the safe area (its height, keeping the circles round) */
- (void)layoutControls
{
	CGRect area = UIEdgeInsetsInsetRect(self.bounds, UIEdgeInsetsMake(0, self.safeAreaInsets.left, 0,
		self.safeAreaInsets.right));

	if (area.size.width <= 0 || area.size.height <= 0)
		return;
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	container.frame = self.bounds;
	[CATransaction commit];
	[self resetFingers];
	scale = fmaxf(0.01f, fminf(area.size.width / GRID_WIDTH, area.size.height / GRID_HEIGHT));
	origin = area.origin;
	logical_width = area.size.width / scale;
	logical_height = area.size.height / scale;
	layout_bounds(&layout, logical_width, logical_height);
	layout_set_minimum_radius(&layout, MINIMUM_RADIUS_POINTS / scale);
	[self updateLayers];
}

- (void)layoutChanged
{
	layout_bounds(&layout, logical_width, logical_height);
	layout_set_minimum_radius(&layout, MINIMUM_RADIUS_POINTS / scale);
	[self updateLayers];
}

- (CGPoint)logical:(CGPoint)point
{
	return CGPointMake((point.x - origin.x) / scale, (point.y - origin.y) / scale);
}

- (float)toolbarY
{
	return fmaxf(38.0f, self.safeAreaInsets.top / scale + 38.0f);
}

/* (beside the hide toggle, at the top's middle: the corners are the HUD's) */
- (float)optionsX
{
	return logical_width / 2.0f + 70.0f;
}

- (float)toolbarRadius:(float)radius
{
	return fmaxf(radius, fminf(MINIMUM_RADIUS_POINTS / scale, MAX_MINIMUM_RADIUS));
}

/* ---------- when the controls show */

/* a controller, keyboard or mouse the system knows of: "auto" hides the
controls for it */
static BOOL other_input_connected(void)
{
	return GCController.controllers.count > 0 || GCKeyboard.coalescedKeyboard != nil || GCMouse.mice.count > 0;
}

- (BOOL)belongsShown
{
	if (!(scene & SCENE_KNOWN) || (scene & (SCENE_MENUS | SCENE_OFF)))
		return NO;
	return (scene & SCENE_ON) || (!other_input_connected() && !(scene & SCENE_CONTROLLER));
}

- (void)updateShowing
{
	BOOL show = NO;

	if (![self belongsShown])
	{
		shown_since = -1.0;
	}
	else
	{
		double now = CACurrentMediaTime();

		if (shown_since < 0.0)
			shown_since = now;
		show = now - shown_since >= SHOW_DELAY_SECONDS;
	}
	if (show != showing)
	{
		showing = show;
		[self resetFingers];
		[self updateSensors];
		[self updateLayers];
	}
	/* the menus (and anything but a game) take the fingers as SDL's mouse;
	a game does not, where a tap would fire */
	{
		int wanted = !(scene & SCENE_KNOWN) || (scene & SCENE_MENUS) ? 1 : 0;

		if (wanted != touch_mouse)
		{
			touch_mouse = wanted;
			SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, wanted ? "1" : "0");
		}
	}
}

/* (passes the fingers through to SDL's view while the controls are not up) */
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event
{
	if (!showing && !editing && !dialogOpen)
		return nil;
	return [super hitTest:point withEvent:event];
}

- (void)pollGame:(CADisplayLink *)link
{
	int was = scene;
	int32_t read[16];
	int serial;

	(void)link;
	scene = host_touch_scene_read();
	if (((was ^ scene) & (3 << SCENE_STYLE_SHIFT | SCENE_CAPTIONS)) ||
		(was >> SCENE_CONTEXT_SHIFT) != (scene >> SCENE_CONTEXT_SHIFT) ||
		(showing && !controlsHidden && ((scene >> SCENE_CONTEXT_SHIFT) & CONTEXT_RELOAD)))
	{
		[self updateLayers];
	}
	/* (a crouch tapped on stands up at death or in a seat, with no finger down) */
	if (((was ^ scene) & SCENE_CROUCH_TOGGLE) || [self contextOf:was] != [self context])
	{
		[self publish];
		[self updateLayers];
	}
	[self fadeWhenIdle];
	[self updateShowing];
	serial = host_touch_bindings_read(read);
	if (serial && serial != bindings_serial)
	{
		int index;

		bindings_serial = serial;
		for (index = 0; index < 16; index++)
			bindings[index] = read[index];
		[self updateLayers];
	}
	[self updateRumble];
	if (motion.gyroActive)
	{
		atomic_store(&gyro_rotation, [self screenRotation]);
		atomic_store(&gyro_sensitivity, sensitivity);
	}
}

/* ---------- the controller state the game reads */

- (void)resetFingers
{
	drag_touch = nil;
	drag_control = -1;
	[owners removeAllObjects];
	[button_touches removeAllObjects];
	[toolbar_fingers removeAllObjects];
	look_touch = nil;
	stick_floating = NO;
	crouched_kind = -1;
	atomic_store(&gyro_restart, true);
	host_touch_look_reset();
	memset(axes, 0, sizeof(axes));
	[self publish];
	[self updateLayers];
}

- (void)publish
{
	int32_t state[7] = { 0 };
	int bits = 0;

	if (!editing && !dialogOpen)
	{
		axes[4] = axes[5] = 0;
		for (UITouch *touch in owners)
		{
			int control = [owners objectForKey:touch].intValue;
			const struct kind *kind;

			if (control < 0 || control >= layout.count ||
				([self crouchToggles] && [self crouches:layout.controls[control].type]))
			{
				continue;
			}
			kind = &KINDS[layout.controls[control].type];
			if (kind->bit >= 0)
				bits |= 1 << kind->bit;
			if (kind->trigger >= 0)
				axes[kind->trigger] = 32767;
		}
		/* (crouch: tapped on, held by no finger) */
		if (crouched_kind >= 0)
		{
			crouched_kind = context_crouch_kept(crouched_kind, [self gameControl:crouched_kind], [self context],
				[self crouchToggles]);
		}
		if (crouched_kind >= 0)
		{
			const struct kind *kind = &KINDS[crouched_kind];

			if (kind->bit >= 0)
				bits |= 1 << kind->bit;
			if (kind->trigger >= 0)
				axes[kind->trigger] = 32767;
		}
		memcpy(state, axes, sizeof(axes));
		state[6] = bits;
	}
	host_touch_set_state(state);
}

/* input.touch_crouch: whether Crouch is tapped on and off, not held */
- (BOOL)crouchToggles
{
	return (scene & SCENE_CROUCH_TOGGLE) != 0;
}

/* whether a kind of control crouches (is bound to Crouch) */
- (BOOL)crouches:(int)type
{
	return [self gameControl:type] == CONTROL_CROUCH;
}

/* held by a finger, or a crouch tapped on */
- (BOOL)pressed:(int)control
{
	return [self held:control] || (crouched_kind >= 0 && layout.controls[control].type == crouched_kind);
}

- (BOOL)held:(int)control
{
	for (UITouch *touch in owners)
	{
		if ([owners objectForKey:touch].intValue == control)
			return YES;
	}
	return NO;
}

- (uint64_t)heldControls
{
	uint64_t held = 0;

	for (UITouch *touch in owners)
	{
		int control = [owners objectForKey:touch].intValue;

		if (control >= 0 && control < 64)
			held |= 1ULL << control;
	}
	return held;
}

- (float)stickCenterX
{
	return stick_floating ? stick_x : layout.controls[KIND_LEFT].x;
}

- (float)stickCenterY
{
	return stick_floating ? stick_y : layout.controls[KIND_LEFT].y;
}

- (void)moveStick:(int)control x:(float)x y:(float)y
{
	float radius = layout_radius(&layout, control);
	float dx = (x - [self stickCenterX]) / radius, dy = (y - [self stickCenterY]) / radius;
	float length = sqrtf(dx * dx + dy * dy);

	if (length < STICK_DEAD_ZONE)
	{
		dx = dy = 0.0f;
	}
	else if (length > 1.0f)
	{
		dx /= length;
		dy /= length;
	}
	axes[0] = (int)lroundf(dx * 32767.0f);
	axes[1] = (int)lroundf(dy * 32767.0f);
}

/* ---------- the buttons' names, from the player's profile */

/* the game control a kind's button does in the player's profile, or -1 */
- (int)gameControl:(int)type
{
	int control;

	if (KINDS[type].gamepad_button < 0)
		return -1;
	control = bindings[KINDS[type].gamepad_button];
	return control >= 0 && control < GAME_CONTROL_COUNT ? control : -1;
}

- (NSString *)label:(int)type
{
	int control = [self gameControl:type];
	NSString *name = @(KINDS[type].button_name);

	if (control < 0)
		return name;
	return KINDS[type].face_button ? [NSString stringWithFormat:@"%@ / %@", name, GAME_CONTROLS[control]] :
		GAME_CONTROLS[control];
}

- (NSString *)controlName:(int)type
{
	return type == KIND_LEFT ? @"Move stick" : [self label:type];
}

- (int)contextOf:(int)bits
{
	return (bits >> SCENE_CONTEXT_SHIFT) & 31;
}

- (int)context
{
	return [self contextOf:scene];
}

- (BOOL)hiddenNow:(int)type
{
	return context_hidden([self gameControl:type], [self context]);
}

/* ---------- fingers */

static BOOL inside(float x, float y, float cx, float cy, float radius)
{
	return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
}

/* what is under a point (logical units): a toolbar item, a control, or HOLD_NOTHING */
- (int)hit:(CGPoint)point
{
	float x = (float)point.x, y = (float)point.y, toolbar = [self toolbarY];
	int index;

	if (inside(x, y, [self optionsX], toolbar, [self toolbarRadius:OPTIONS_RADIUS]))
		return TOOLBAR_OPTIONS;
	if (editing && inside(x, y, [self optionsX] + 156.0f, toolbar, [self toolbarRadius:OPTIONS_RADIUS]))
		return TOOLBAR_EXPORT;
	if (editing && inside(x, y, [self optionsX] + 78.0f, toolbar, [self toolbarRadius:OPTIONS_RADIUS]))
		return TOOLBAR_IMPORT;
	if (!editing && inside(x, y, logical_width / 2.0f, toolbar, [self toolbarRadius:TOOLBAR_RADIUS]))
		return TOOLBAR_TOGGLE;
	if (controlsHidden && !editing)
		return HOLD_NOTHING;
	for (index = 0; index < layout.count; index++)
	{
		const struct control *control = &layout.controls[index];

		if (control->shown && control->type != KIND_LEFT && (editing || ![self hiddenNow:control->type]) &&
			inside(x, y, control->x, control->y, layout_radius(&layout, index)))
		{
			return index;
		}
	}
	if (layout.controls[KIND_LEFT].shown && ![self held:KIND_LEFT] &&
		inside(x, y, layout.controls[KIND_LEFT].x, layout.controls[KIND_LEFT].y,
			layout_radius(&layout, KIND_LEFT) * (editing ? 1.0f : STICK_REACH)))
	{
		return KIND_LEFT;
	}
	return HOLD_NOTHING;
}

- (BOOL)inFloatingZone:(CGPoint)point
{
	return point.x < logical_width * FLOATING_ZONE_WIDTH && point.y > logical_height - FLOATING_ZONE_HEIGHT;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	[self fingers:touches phase:UITouchPhaseBegan];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	[self fingers:touches phase:UITouchPhaseMoved];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	[self fingers:touches phase:UITouchPhaseEnded];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	[self resetFingers];
}

- (void)fingers:(NSSet<UITouch *> *)touches phase:(UITouchPhase)phase
{
	BOOL was_editing = editing, was_hidden = controlsHidden;
	uint64_t was_held = [self heldControls];
	int was_x = axes[0], was_y = axes[1];
	float was_center_x = [self stickCenterX], was_center_y = [self stickCenterY];

	last_touch = CACurrentMediaTime();
	if (idle < 1.0f)
		[self fadeWhenIdle];
	if (!showing && !editing)
	{
		if (owners.count || look_touch)
			[self resetFingers];
		return;
	}
	for (UITouch *touch in touches)
	{
		CGPoint point = [touch locationInView:self];

		if (editing)
			[self editTouch:touch at:point phase:phase];
		else if (phase == UITouchPhaseBegan)
			[self fingerDown:touch at:point];
		else if (phase == UITouchPhaseMoved)
			[self fingerMoved:touch at:point];
		else
			[self fingerUp:touch at:point];
	}
	[self publish];
	if (editing || was_editing || controlsHidden != was_hidden || [self heldControls] != was_held ||
		axes[0] != was_x || axes[1] != was_y || [self stickCenterX] != was_center_x ||
		[self stickCenterY] != was_center_y)
	{
		[self updateLayers];
	}
}

- (void)fingerDown:(UITouch *)touch at:(CGPoint)pixels
{
	CGPoint point = [self logical:pixels];
	int target = [self hit:point];

	if (target == HOLD_NOTHING && layout.floating_stick && !controlsHidden && layout.controls[KIND_LEFT].shown &&
		![self held:KIND_LEFT] && [self inFloatingZone:point])
	{
		stick_floating = YES;
		stick_x = (float)point.x;
		stick_y = (float)point.y;
		target = KIND_LEFT;
	}
	if (target == TOOLBAR_OPTIONS || target == TOOLBAR_TOGGLE)
	{
		[toolbar_fingers setObject:@[ @(pixels.x), @(pixels.y), @(target) ] forKey:touch];
	}
	else if (target != HOLD_NOTHING)
	{
		[owners setObject:@(target) forKey:touch];
		if (layout.controls[target].type == KIND_LEFT)
		{
			[self moveStick:target x:(float)point.x y:(float)point.y];
		}
		else
		{
			int type = layout.controls[target].type;

			[button_touches setObject:[NSValue valueWithCGPoint:pixels] forKey:touch];
			crouched_kind = context_crouch_pressed(crouched_kind, type, [self gameControl:type], [self crouchToggles]);
			[self tap];
		}
	}
	else if (!controlsHidden && !look_touch)
	{
		[self startLook:touch at:pixels];
	}
}

- (void)startLook:(UITouch *)touch at:(CGPoint)pixels
{
	look_touch = touch;
	look_at = pixels;
	if (![owners objectForKey:touch])
		[owners setObject:@(HOLD_LOOK) forKey:touch];
}

- (BOOL)moved:(CGPoint)start to:(CGPoint)pixels
{
	return hypot(pixels.x - start.x, pixels.y - start.y) > SLOP * scale;
}

- (void)fingerMoved:(UITouch *)touch at:(CGPoint)pixels
{
	NSNumber *owner = [owners objectForKey:touch];
	NSArray<NSNumber *> *toolbar = [toolbar_fingers objectForKey:touch];
	NSValue *pressed_at = [button_touches objectForKey:touch];

	if (owner && owner.intValue >= 0 && layout.controls[owner.intValue].type == KIND_LEFT)
	{
		CGPoint point = [self logical:pixels];

		[self moveStick:owner.intValue x:(float)point.x y:(float)point.y];
	}
	if (toolbar && [self moved:CGPointMake(toolbar[0].doubleValue, toolbar[1].doubleValue) to:pixels])
	{
		/* a swipe that began on the toolbar aims; the item does not act */
		[toolbar_fingers removeObjectForKey:touch];
		if (!controlsHidden && !look_touch)
			[self startLook:touch at:CGPointMake(toolbar[0].doubleValue, toolbar[1].doubleValue)];
	}
	if (pressed_at && look_touch && look_touch != touch)
	{
		/* while another finger aims, a held button's finger may only take over from where it is */
		pressed_at = [NSValue valueWithCGPoint:pixels];
		[button_touches setObject:pressed_at forKey:touch];
	}
	if (!look_touch && pressed_at && [self moved:pressed_at.CGPointValue to:pixels])
	{
		/* a held button (either Fire too) also aims */
		look_touch = touch;
		look_at = pressed_at.CGPointValue;
	}
	if (touch == look_touch)
	{
		host_touch_add_look((float)(pixels.x - look_at.x) / scale * sensitivity,
			(float)(pixels.y - look_at.y) / scale * sensitivity);
		look_at = pixels;
	}
}

- (void)fingerUp:(UITouch *)touch at:(CGPoint)pixels
{
	NSArray<NSNumber *> *toolbar = [toolbar_fingers objectForKey:touch];
	NSNumber *owner;

	if (toolbar)
	{
		[toolbar_fingers removeObjectForKey:touch];
		if ([self hit:[self logical:pixels]] == toolbar[2].intValue)
			[self toolbarAction:toolbar[2].intValue];
		return;
	}
	owner = [owners objectForKey:touch];
	if (owner && owner.intValue >= 0 && layout.controls[owner.intValue].type == KIND_LEFT)
	{
		axes[0] = axes[1] = 0;
		stick_floating = NO;
	}
	if (touch == look_touch)
		look_touch = nil;
	[owners removeObjectForKey:touch];
	[button_touches removeObjectForKey:touch];
}

- (void)toolbarAction:(int)item
{
	if (item == TOOLBAR_OPTIONS)
		[self showOptions];
	else if (item == TOOLBAR_TOGGLE)
		[self setHidden:!controlsHidden save:YES];
}

- (void)setHidden:(BOOL)hidden save:(BOOL)save
{
	[self resetFingers];
	controlsHidden = hidden;
	if (save)
		[[NSUserDefaults standardUserDefaults] setBool:hidden forKey:KEY_HIDDEN];
	[self updateSensors];
	[self updateLayers];
}

/* a finger in the layout editor: drags controls; Save, Export and Import act at once */
- (void)editTouch:(UITouch *)touch at:(CGPoint)pixels phase:(UITouchPhase)phase
{
	CGPoint point = [self logical:pixels];

	if (phase == UITouchPhaseBegan)
	{
		int target;

		if (drag_touch)
			return;
		target = [self hit:point];
		if (target == TOOLBAR_OPTIONS)
		{
			[self saveLayout];
			[self resetFingers];
			editing = NO;
		}
		else if (target == TOOLBAR_EXPORT)
		{
			[self exportLayout];
		}
		else if (target == TOOLBAR_IMPORT)
		{
			[self importLayout];
		}
		else if (target >= 0)
		{
			drag_touch = touch;
			drag_control = target;
			drag_offset_x = (float)point.x - layout.controls[target].x;
			drag_offset_y = (float)point.y - layout.controls[target].y;
		}
	}
	else if (phase == UITouchPhaseMoved && touch == drag_touch)
	{
		layout_move(&layout, drag_control, (float)point.x - drag_offset_x, (float)point.y - drag_offset_y);
	}
	else if (touch == drag_touch)
	{
		drag_touch = nil;
		drag_control = -1;
	}
}

/* ---------- the layout, saved and in files */

- (void)saveLayout
{
	[[NSUserDefaults standardUserDefaults] setObject:layout_export(&layout, sensitivity) forKey:KEY_CONFIGURATION];
}

- (NSString *)layoutFile
{
	NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
		inDomains:NSUserDomainMask].firstObject;

	return [documents URLByAppendingPathComponent:@"touch-layout.txt"].path;
}

- (void)message:(NSString *)title text:(NSString *)text
{
	UIAlertController *alert = [UIAlertController alertControllerWithTitle:title message:text
		preferredStyle:UIAlertControllerStyleAlert];

	[alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
	[self.window.rootViewController presentViewController:alert animated:YES completion:nil];
}

- (void)exportLayout
{
	NSError *error = nil;

	[self resetFingers];
	if ([layout_export(&layout, sensitivity) writeToFile:[self layoutFile] atomically:YES
		encoding:NSUTF8StringEncoding error:&error])
	{
		[self message:@"Layout exported" text:@"It is touch-layout.txt in OpenH1's folder (the Files app, On My "
			"iPhone). Import reads it from there."];
	}
	else
	{
		[self message:@"The layout could not be exported" text:error.localizedDescription];
	}
}

- (void)importLayout
{
	NSString *text = [NSString stringWithContentsOfFile:[self layoutFile] encoding:NSUTF8StringEncoding error:NULL];
	struct layout imported;
	float imported_sensitivity;

	[self resetFingers];
	if (!text)
	{
		[self message:@"No layout to import" text:@"Put a layout file, touch-layout.txt, in OpenH1's folder (the "
			"Files app, On My iPhone), and import again."];
		return;
	}
	if (!layout_import(text, &imported, &imported_sensitivity))
	{
		[self message:@"The layout could not be imported" text:@"touch-layout.txt is not a valid layout file."];
		return;
	}
	imported.width = GRID_WIDTH;
	imported.height = GRID_HEIGHT;
	layout = imported;
	sensitivity = imported_sensitivity;
	[self layoutChanged];
	[self saveLayout];
	[self setHidden:NO save:YES];
}

/* ---------- the options */

- (void)showOptions
{
	HaloTouchOptions *options = [HaloTouchOptions new];
	UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:options];

	options.controls = self;
	dialogOpen = YES;
	[self resetFingers];
	navigation.modalPresentationStyle = UIModalPresentationFormSheet;
	[self.window.rootViewController presentViewController:navigation animated:YES completion:nil];
}

- (void)optionsClosed
{
	dialogOpen = NO;
	[self saveLayout];
	[self resetFingers];
	[self updateSensors];
	[self updateLayers];
}

/* ---------- the gyroscope and the haptics */

- (void)updateSensors
{
	BOOL needed = showing && !controlsHidden && layout.gyroscope && motion.gyroAvailable && !editing && !dialogOpen;

	atomic_store(&gyro_aiming, needed);
	atomic_store(&gyro_sensitivity, sensitivity);
	atomic_store(&gyro_rotation, [self screenRotation]);
	if (needed && !motion.gyroActive)
	{
		__weak HaloTouchView *weak = self;

		atomic_store(&gyro_restart, true);
		motion.gyroUpdateInterval = 1.0 / 120.0;
		[motion startGyroUpdatesToQueue:gyro_queue withHandler:^(CMGyroData *data, NSError *error) {
			(void)error;
			[weak gyroSample:data];
		}];
	}
	else if (!needed && motion.gyroActive)
	{
		[motion stopGyroUpdates];
		atomic_store(&gyro_restart, true);
	}
}

/* the screen's turn from portrait, in quarters (on the main thread) */
- (int)screenRotation
{
	UIInterfaceOrientation orientation = self.window.windowScene.effectiveGeometry.interfaceOrientation;

	return orientation == UIInterfaceOrientationLandscapeRight ? 1 :
		orientation == UIInterfaceOrientationPortraitUpsideDown ? 2 :
		orientation == UIInterfaceOrientationLandscapeLeft ? 3 : 0;
}

/* the turn since the last sample, in the screen's axes (GyroscopeAim.java):
the trapezoid rule over two samples, as a swipe of so many logical units
(on the gyroscope's queue: it reads only the atomics the main thread keeps) */
- (void)gyroSample:(CMGyroData *)data
{
	float x = (float)data.rotationRate.x, y = (float)data.rotationRate.y, screen_x = x, screen_y = y;
	int rotation = atomic_load(&gyro_rotation);
	float aim_sensitivity = atomic_load(&gyro_sensitivity);
	double seconds = data.timestamp - gyro_previous_time;
	BOOL valid;

	if (atomic_exchange(&gyro_restart, false))
		gyro_previous_orientation = -1;
	if (!atomic_load(&gyro_aiming) || !isfinite(x) || !isfinite(y))
	{
		gyro_previous_orientation = -1;
		return;
	}
	if (rotation == 1)
	{
		screen_x = y;
		screen_y = -x;
	}
	else if (rotation == 2)
	{
		screen_x = -x;
		screen_y = -y;
	}
	else if (rotation == 3)
	{
		screen_x = -y;
		screen_y = x;
	}
	if (fabsf(screen_x) < GYRO_NOISE)
		screen_x = 0.0f;
	if (fabsf(screen_y) < GYRO_NOISE)
		screen_y = 0.0f;
	valid = gyro_previous_orientation == rotation && seconds > 0.0 && seconds <= GYRO_LONGEST_GAP;
	if (valid)
	{
		float yaw = -(screen_y + gyro_previous_y) * 0.5f * (float)seconds;
		float pitch = -(screen_x + gyro_previous_x) * 0.5f * (float)seconds;

		if (yaw != 0.0f || pitch != 0.0f)
			host_touch_add_gyro(-yaw / LOOK_RADIANS_PER_UNIT * aim_sensitivity,
				-pitch / LOOK_RADIANS_PER_UNIT * aim_sensitivity);
	}
	gyro_previous_time = data.timestamp;
	gyro_previous_orientation = rotation;
	gyro_previous_x = screen_x;
	gyro_previous_y = screen_y;
}

/* a light tap at a button's press, with the vibration on */
- (void)tap
{
	if (layout.rumble)
		[tap_feedback impactOccurredWithIntensity:0.5];
}

/* the phone's haptics for port 0's rumble, while the controls show */
- (void)updateRumble
{
	BOOL active = showing && !controlsHidden && layout.rumble && !editing && !dialogOpen;
	int amplitude = active ? host_touch_rumble_strength() : 0;
	double now = CACurrentMediaTime();

	if (!amplitude || now - last_vibration < VIBRATION_RENEW_SECONDS)
		return;
	[rumble_feedback impactOccurredWithIntensity:amplitude / 255.0];
	last_vibration = now;
}

/* ---------- drawing */

/* the idle fade: toward IDLE_OPACITY once no finger has touched the screen
for IDLE_SECONDS, back to whole at a touch */
- (void)fadeWhenIdle
{
	double now = CACurrentMediaTime(), elapsed = fmin(now - last_fade, 0.1);
	BOOL touched = owners.count || look_touch || toolbar_fingers.count || now - last_touch < IDLE_SECONDS;
	float target = touched ? 1.0f : IDLE_OPACITY;
	float step = (1.0f - IDLE_OPACITY) * (float)elapsed / (touched ? FADE_IN_SECONDS : FADE_OUT_SECONDS);
	float faded = idle < target ? fminf(target, idle + step) : fmaxf(target, idle - step);

	last_fade = now;
	if (faded != idle)
	{
		idle = faded;
		if (showing)
			[self updateLayers];
	}
}

- (UIImage *)art:(int)type
{
	int art = KIND_ART[type], style = (scene >> SCENE_STYLE_SHIFT) & 3;

	if (art < 0)
		return nil;
	if (art >= 6)
		return pictures[CONTROL_ART[art - 6]];
	return pictures[FACES[style < 3 ? style : 0][art]];
}

/* a picture, its longer side 2 * radius, centred on a point; brighter while
held; with a glow (the HUD's colour) of strength glow */
- (void)fitted:(UIImage *)image x:(float)x y:(float)y radius:(float)radius alpha:(float)alpha active:(BOOL)active
	glow:(float)glow
{
	CGContextRef context = UIGraphicsGetCurrentContext();
	float fit = radius / (float)fmax(image.size.width, image.size.height);
	CGRect rect = CGRectMake(x - image.size.width * fit, y - image.size.height * fit, image.size.width * fit * 2.0f,
		image.size.height * fit * 2.0f);

	if (glow > 0.0f)
	{
		CGContextSaveGState(context);
		CGContextSetShadowWithColor(context, CGSizeZero, radius * GLOW_MARGIN * 2.0f, hud_color(glow).CGColor);
		[image drawInRect:rect blendMode:kCGBlendModeNormal alpha:clampf(alpha, 0.0f, 1.0f)];
		CGContextRestoreGState(context);
	}
	[image drawInRect:rect blendMode:kCGBlendModeNormal alpha:clampf(alpha, 0.0f, 1.0f)];
	if (active)
		[image drawInRect:rect blendMode:kCGBlendModePlusLighter alpha:0.15f * clampf(alpha, 0.0f, 1.0f)];
}

- (void)text:(NSString *)text x:(float)x y:(float)y font:(UIFont *)font alpha:(float)alpha spacing:(float)spacing
{
	NSShadow *shadow = [NSShadow new];
	NSDictionary *attributes;
	CGSize size;

	shadow.shadowColor = [UIColor colorWithWhite:0.0 alpha:0.8 * alpha];
	shadow.shadowOffset = CGSizeMake(0, 1);
	shadow.shadowBlurRadius = 2.5;
	attributes = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: hud_color(alpha),
		NSShadowAttributeName: shadow, NSKernAttributeName: @(spacing * font.pointSize) };
	size = [text sizeWithAttributes:attributes];
	[text drawAtPoint:CGPointMake(x - size.width / 2.0, y - size.height / 2.0) withAttributes:attributes];
}

/* a word fitted inside a circle (LABEL_WIDTH radii) */
- (void)label:(NSString *)text x:(float)x y:(float)y radius:(float)radius size:(float)size alpha:(float)alpha
{
	UIFont *font = [UIFont systemFontOfSize:size weight:UIFontWeightSemibold];
	float width = (float)[text sizeWithAttributes:@{ NSFontAttributeName: font }].width;

	if (width > radius * LABEL_WIDTH)
		font = [font fontWithSize:size * radius * LABEL_WIDTH / width];
	[self text:text x:x y:y font:font alpha:fmaxf(alpha, 0.5f) spacing:0.0f];
}

- (void)caption:(int)type x:(float)x bottom:(float)bottom opacity:(float)opacity
{
	int game = [self gameControl:type];
	NSString *caption;

	if (!(scene & SCENE_CAPTIONS) || game < 0)
		return;
	caption = (([self context] & CONTEXT_VEHICLE) && !editing && game == CONTROL_ACTION) ? @"EXIT" : CAPTIONS[game];
	[self text:caption x:x y:bottom + CAPTION_SIZE * 0.5f + 1.0f font:caption_font
		alpha:CAPTION_ALPHA * fmaxf(opacity, 0.5f) spacing:0.06f];
}

- (void)drawControl:(int)index active:(BOOL)active opacity:(float)opacity
{
	const struct control *control = &layout.controls[index];
	int type = control->type;
	float x = control->x, y = control->y, radius = layout_radius(&layout, index);
	BOOL minor = MINOR_KINDS[type];
	float sink = active ? PRESSED_SIZE : 1.0f;
	UIImage *art = [self art:type];
	const struct kind *kind = &KINDS[type];
	UIImage *icon;
	float icon_opacity;

	if (art)
	{
		int game = [self gameControl:type], context = editing ? 0 : [self context];
		float glow = context_glow(game, context, draw_pulse);
		float size = radius * ART_SIZE * sink * context_swell(game, context, draw_pulse);
		float alpha = (active ? 1.0f : minor ? MINOR_BUTTON_ALPHA : BUTTON_ALPHA) * opacity * context_alpha(game, context);

		[self fitted:art x:x y:y radius:size alpha:alpha active:active || glow > 0.6f glow:glow * fmaxf(opacity, 0.5f)];
		[self caption:type x:x bottom:y + radius * ART_SIZE * (float)(art.size.height / fmax(art.size.width,
			art.size.height)) opacity:opacity * context_alpha(game, context)];
		return;
	}
	icon = kind->arrow >= 0 ? pictures[@"touch_icon_arrow"] : kind->bit == 6 ? pictures[@"touch_icon_pause"] :
		kind->bit == 4 ? pictures[@"touch_icon_back"] : nil;
	icon_opacity = (active ? 1.0f : minor ? MINOR_ICON_ALPHA : ICON_ALPHA) * fmaxf(opacity, 0.5f);
	if (!icon)
	{
		[self label:[self label:type] x:x y:y radius:radius size:fminf(11.0f * fmaxf(control->size, radius / 36.0f),
			16.0f) alpha:icon_opacity];
		return;
	}
	{
		CGContextRef context = UIGraphicsGetCurrentContext();

		CGContextSaveGState(context);
		if (kind->arrow > 0)
		{
			CGContextTranslateCTM(context, x, y);
			CGContextRotateCTM(context, kind->arrow * (float)M_PI / 180.0f);
			CGContextTranslateCTM(context, -x, -y);
		}
		[self fitted:icon x:x y:y radius:radius * (minor ? MINOR_ICON_SIZE : ICON_SIZE) * sink alpha:icon_opacity
			active:active glow:0.0f];
		CGContextRestoreGState(context);
	}
}

- (void)toolbarItem:(float)x radius:(float)radius icon:(UIImage *)icon label:(NSString *)label
{
	float opacity = fmaxf(layout.opacity * idle, 0.5f);

	if (icon)
		[self fitted:icon x:x y:[self toolbarY] radius:[self toolbarRadius:radius] * MINOR_ICON_SIZE
			alpha:ICON_ALPHA * opacity active:NO glow:0.0f];
	else
		[self label:label x:x y:[self toolbarY] radius:[self toolbarRadius:radius] size:13.0f alpha:opacity];
}

- (void)drawToolbar
{
	if (editing)
	{
		[self toolbarItem:[self optionsX] radius:OPTIONS_RADIUS icon:nil label:@"Save"];
		[self toolbarItem:[self optionsX] + 156.0f radius:OPTIONS_RADIUS icon:nil label:@"Export"];
		[self toolbarItem:[self optionsX] + 78.0f radius:OPTIONS_RADIUS icon:nil label:@"Import"];
		[self label:@"Drag the controls, then push Save" x:logical_width / 2.0f y:[self toolbarY] + 58.0f radius:1000.0f
			size:14.0f alpha:1.0f];
	}
	else
	{
		[self toolbarItem:logical_width / 2.0f radius:TOOLBAR_RADIUS
			icon:pictures[controlsHidden ? @"touch_icon_show" : @"touch_icon_hide"] label:nil];
		[self toolbarItem:[self optionsX] radius:OPTIONS_RADIUS icon:pictures[@"touch_icon_options"] label:nil];
	}
}

- (void)drawRingAt:(float)x y:(float)y radius:(float)radius opacity:(float)opacity
{
	CGContextRef context = UIGraphicsGetCurrentContext();

	CGContextSaveGState(context);
	CGContextSetShadowWithColor(context, CGSizeMake(0, 1), 3, [UIColor colorWithWhite:0 alpha:0.5 * opacity].CGColor);
	CGContextSetStrokeColorWithColor(context, hud_color(STICK_RING_ALPHA * opacity).CGColor);
	CGContextSetLineWidth(context, 2.0);
	CGContextStrokeEllipseInRect(context, CGRectMake(x - radius + 1, y - radius + 1, 2 * radius - 2, 2 * radius - 2));
	CGContextRestoreGState(context);
}

- (void)drawKnobAt:(float)x y:(float)y radius:(float)knob active:(BOOL)active opacity:(float)opacity
{
	CGContextRef context = UIGraphicsGetCurrentContext();

	CGContextSaveGState(context);
	CGContextSetShadowWithColor(context, CGSizeMake(0, 1), 3, [UIColor colorWithWhite:0 alpha:0.5 * opacity].CGColor);
	CGContextSetFillColorWithColor(context, hud_color((active ? 0.8f : STICK_KNOB_ALPHA) * opacity).CGColor);
	CGContextFillEllipseInRect(context, CGRectMake(x - knob, y - knob, 2 * knob, 2 * knob));
	CGContextRestoreGState(context);
}

/* ---------- the layers */

/* what a control's picture is drawn for: a picture is drawn again only
when this changes (its place on the display moves its layer, but for the
fraction of a pixel it is drawn at) */
struct control_look
{
	float scale, pixels, radius, size, offset_x, offset_y, opacity, pulse;
	int type, style, captions, game, context, active, editing;
};

static void layer_hidden(CALayer *layer, BOOL hidden)
{
	if (layer.hidden != hidden)
		layer.hidden = hidden;
}

static void layer_frame(CALayer *layer, CGRect frame)
{
	if (!CGRectEqualToRect(layer.frame, frame))
		layer.frame = frame;
}

/* the display's pixels per point */
- (CGFloat)pixelScale
{
	CGFloat pixels = self.traitCollection.displayScale;

	return pixels > 0.0 ? pixels : UIScreen.mainScreen.scale;
}

/* a picture of this many points, drawn by draw */
- (UIImage *)picture:(CGSize)size draw:(void (^)(CGContextRef context))draw
{
	UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat preferredFormat];
	UIGraphicsImageRenderer *renderer;

	format.opaque = NO;
	format.scale = [self pixelScale];
	format.preferredRange = UIGraphicsImageRendererFormatRangeStandard;
	renderer = [[UIGraphicsImageRenderer alloc] initWithSize:size format:format];
	return [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
		draw(context.CGContext);
	}];
}

/* a frame on whole pixels around a box (logical units), and the fraction
of a pixel the box begins in it */
- (CGRect)pixelFrame:(CGRect)box offset:(CGPoint *)offset
{
	CGFloat pixels = [self pixelScale];
	CGFloat left = origin.x + box.origin.x * scale, top = origin.y + box.origin.y * scale;
	CGRect frame;

	frame.origin = CGPointMake(floor(left * pixels) / pixels, floor(top * pixels) / pixels);
	offset->x = left - frame.origin.x;
	offset->y = top - frame.origin.y;
	frame.size = CGSizeMake(ceil((offset->x + box.size.width * scale) * pixels) / pixels,
		ceil((offset->y + box.size.height * scale) * pixels) / pixels);
	return frame;
}

- (void)updateControl:(int)index shown:(BOOL)shown
{
	CALayer *layer = control_layers[index];
	const struct control *control = &layout.controls[index];
	struct control_look look;
	float radius, margin, half, top, bottom;
	int game, context;
	CGRect box, frame;
	CGPoint offset;

	if (!shown)
	{
		layer_hidden(layer, YES);
		return;
	}
	game = [self gameControl:control->type];
	context = editing ? 0 : [self context];
	radius = layout_radius(&layout, index);
	/* (room for the art, the reload's swell and glow, and the caption under it) */
	margin = radius * (game == CONTROL_ACTION ? 2.4f : 1.25f);
	half = fmaxf(margin, 34.0f);
	top = margin + 2.0f;
	bottom = margin + 2.0f * CAPTION_SIZE + 6.0f;
	box = CGRectMake(control->x - half, control->y - top, 2.0f * half, top + bottom);
	frame = [self pixelFrame:box offset:&offset];
	memset(&look, 0, sizeof(look));
	look.scale = scale;
	look.pixels = (float)[self pixelScale];
	look.radius = radius;
	look.size = control->size;
	look.offset_x = (float)offset.x;
	look.offset_y = (float)offset.y;
	look.opacity = layout.opacity;
	/* (the reload's pulse, in steps a cycle's pictures are kept for) */
	look.pulse = game == CONTROL_ACTION && (context & CONTEXT_RELOAD) ?
		roundf(context_pulse(CACurrentMediaTime()) * 24.0f) / 24.0f : 0.0f;
	look.type = control->type;
	look.style = (scene >> SCENE_STYLE_SHIFT) & 3;
	look.captions = (scene & SCENE_CAPTIONS) != 0;
	look.game = game;
	look.context = context;
	look.active = [self pressed:index] || drag_control == index;
	look.editing = editing;
	if (!control_looks[index] || memcmp(control_looks[index].bytes, &look, sizeof(look)))
	{
		NSData *key = [NSData dataWithBytes:&look length:sizeof(look)];
		UIImage *picture = control_pictures[index][key];

		if (!picture)
		{
			draw_pulse = look.pulse;
			picture = [self picture:frame.size draw:^(CGContextRef graphics) {
				CGContextTranslateCTM(graphics, offset.x, offset.y);
				CGContextScaleCTM(graphics, self->scale, self->scale);
				CGContextTranslateCTM(graphics, -box.origin.x, -box.origin.y);
				[self drawControl:index active:look.active opacity:self->layout.opacity];
			}];
			if (control_pictures[index].count >= 64)
				[control_pictures[index] removeAllObjects];
			control_pictures[index][key] = picture;
		}
		layer.contents = (__bridge id)picture.CGImage;
		control_looks[index] = key;
	}
	layer_frame(layer, frame);
	layer_hidden(layer, NO);
}

/* a picture of the stick's centred on a point (logical units), drawn
again only when its look changes; a finger moves its layer */
- (void)stickLayer:(CALayer *)layer look:(NSData *)key current:(NSData *__strong *)current half:(float)half
	x:(float)x y:(float)y draw:(void (^)(void))draw
{
	if (![key isEqual:*current])
	{
		UIImage *picture = stick_pictures[key];

		if (!picture)
		{
			picture = [self picture:CGSizeMake(2.0f * half * scale, 2.0f * half * scale) draw:^(CGContextRef graphics) {
				CGContextScaleCTM(graphics, self->scale, self->scale);
				CGContextTranslateCTM(graphics, half, half);
				draw();
			}];
			if (stick_pictures.count >= 16)
				[stick_pictures removeAllObjects];
			stick_pictures[key] = picture;
		}
		layer.contents = (__bridge id)picture.CGImage;
		layer.bounds = CGRectMake(0, 0, 2.0f * half * scale, 2.0f * half * scale);
		*current = key;
	}
	{
		CGPoint position = CGPointMake(origin.x + x * scale, origin.y + y * scale);

		if (!CGPointEqualToPoint(layer.position, position))
			layer.position = position;
	}
	layer_hidden(layer, NO);
}

- (void)updateStick:(BOOL)shown
{
	float radius, size, x, y, knob, opacity = layout.opacity;
	float pixels = (float)[self pixelScale];
	BOOL active;

	if (!shown)
	{
		layer_hidden(ring_layer, YES);
		layer_hidden(knob_layer, YES);
		return;
	}
	radius = layout_radius(&layout, KIND_LEFT);
	size = layout.controls[KIND_LEFT].size;
	x = fmaxf(radius, fminf(logical_width - radius, [self stickCenterX]));
	y = fmaxf(radius, fminf(logical_height - radius, [self stickCenterY]));
	knob = fmaxf(STICK_KNOB_RADIUS * size, radius * 0.375f);
	active = [self held:KIND_LEFT] || drag_control == KIND_LEFT;
	{
		float ring[5] = { 0.0f, scale, pixels, radius, opacity };
		float disc[6] = { 1.0f, scale, pixels, knob, opacity, active ? 1.0f : 0.0f };
		/* (the shadows' room, in points) */
		float margin = 6.0f / scale;

		[self stickLayer:ring_layer look:[NSData dataWithBytes:ring length:sizeof(ring)] current:&ring_look
			half:radius + margin x:x y:y draw:^{
				[self drawRingAt:0.0f y:0.0f radius:radius opacity:opacity];
			}];
		[self stickLayer:knob_layer look:[NSData dataWithBytes:disc length:sizeof(disc)] current:&knob_look
			half:knob + margin x:x + axes[0] / 32767.0f * radius y:y + axes[1] / 32767.0f * radius draw:^{
				[self drawKnobAt:0.0f y:0.0f radius:knob active:active opacity:opacity];
			}];
	}
}

- (void)updateToolbar
{
	float toolbar = [self toolbarY];
	CGRect box = CGRectMake(logical_width / 2.0f - 200.0f, 0.0f, [self optionsX] + 216.0f - (logical_width / 2.0f - 200.0f),
		toolbar + 80.0f);
	CGPoint offset;
	CGRect frame = [self pixelFrame:box offset:&offset];
	float look[9] = { scale, (float)[self pixelScale], logical_width, toolbar, editing ? 1.0f : 0.0f,
		controlsHidden ? 1.0f : 0.0f, layout.opacity, (float)offset.x, (float)offset.y };
	NSData *key = [NSData dataWithBytes:look length:sizeof(look)];

	if (![key isEqual:toolbar_look])
	{
		UIImage *picture = toolbar_pictures[key];

		if (!picture)
		{
			picture = [self picture:frame.size draw:^(CGContextRef graphics) {
				float faded = self->idle;

				CGContextTranslateCTM(graphics, offset.x, offset.y);
				CGContextScaleCTM(graphics, self->scale, self->scale);
				CGContextTranslateCTM(graphics, -box.origin.x, -box.origin.y);
				/* (the idle fade is the container's opacity) */
				self->idle = 1.0f;
				[self drawToolbar];
				self->idle = faded;
			}];
			if (toolbar_pictures.count >= 8)
				[toolbar_pictures removeAllObjects];
			toolbar_pictures[key] = picture;
		}
		toolbar_layer.contents = (__bridge id)picture.CGImage;
		toolbar_look = key;
	}
	layer_frame(toolbar_layer, frame);
	layer_hidden(toolbar_layer, NO);
}

/* the layers as the controls are now: cheap unless a control's look
changed (then only its picture is drawn), and no drawing while a finger
moves the stick or aims */
- (void)updateLayers
{
	BOOL visible = showing || editing, controls_up = visible && (!controlsHidden || editing);
	int index;

	if (!container)
		return;
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	layer_hidden(container, !visible);
	if (visible)
	{
		float faded = editing ? 1.0f : idle;

		if (container.opacity != faded)
			container.opacity = faded;
		[self updateStick:controls_up && layout.controls[KIND_LEFT].shown];
		for (index = 0; index < MAX_CONTROLS; index++)
		{
			[self updateControl:index shown:controls_up && index < layout.count && layout.controls[index].shown &&
				layout.controls[index].type != KIND_LEFT && (editing || ![self hiddenNow:layout.controls[index].type])];
		}
		[self updateToolbar];
	}
	[CATransaction commit];
}

@end

/* ---------- the options panel */

@implementation HaloTouchOptions
{
	UIStackView *stack;
	UILabel *opacity_label, *sensitivity_label;
}

- (UISwitch *)switchRow:(NSString *)title on:(BOOL)on enabled:(BOOL)enabled action:(SEL)action
{
	UIStackView *row = [[UIStackView alloc] init];
	UILabel *label = [UILabel new];
	UISwitch *control = [UISwitch new];

	label.text = title;
	label.numberOfLines = 0;
	control.on = on;
	control.enabled = enabled;
	[control addTarget:self action:action forControlEvents:UIControlEventValueChanged];
	row.axis = UILayoutConstraintAxisHorizontal;
	row.spacing = 12;
	[row addArrangedSubview:label];
	[row addArrangedSubview:control];
	[stack addArrangedSubview:row];
	return control;
}

- (UISlider *)sliderFrom:(float)low to:(float)high value:(float)value label:(UILabel *)label action:(SEL)action
{
	UISlider *slider = [UISlider new];

	slider.minimumValue = low;
	slider.maximumValue = high;
	slider.value = value;
	[slider addTarget:self action:action forControlEvents:UIControlEventValueChanged];
	[stack addArrangedSubview:label];
	[stack addArrangedSubview:slider];
	return slider;
}

- (void)button:(NSString *)title action:(SEL)action
{
	UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];

	[button setTitle:title forState:UIControlStateNormal];
	button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeading;
	[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
	[stack addArrangedSubview:button];
}

- (void)viewDidLoad
{
	struct layout layout = self.controls.layoutState;
	UIScrollView *scroll = [UIScrollView new];

	[super viewDidLoad];
	self.title = @"Touch controls";
	self.view.backgroundColor = UIColor.systemBackgroundColor;
	self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
		target:self action:@selector(done)];
	stack = [UIStackView new];
	stack.axis = UILayoutConstraintAxisVertical;
	stack.spacing = 14;
	stack.translatesAutoresizingMaskIntoConstraints = NO;
	scroll.translatesAutoresizingMaskIntoConstraints = NO;
	[self.view addSubview:scroll];
	[scroll addSubview:stack];
	[NSLayoutConstraint activateConstraints:@[
		[scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
		[scroll.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
		[scroll.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor],
		[scroll.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor],
		[stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
		[stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
		[stack.leadingAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.leadingAnchor constant:20],
		[stack.trailingAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.trailingAnchor constant:-20],
	]];
	[self switchRow:@"Vibration" on:layout.rumble enabled:YES action:@selector(rumbleChanged:)];
	[self switchRow:[CMMotionManager new].gyroAvailable ? @"Gyroscope aiming (experimental)" :
		@"Gyroscope aiming (not on this device)" on:layout.gyroscope enabled:[CMMotionManager new].gyroAvailable
		action:@selector(gyroscopeChanged:)];
	[self switchRow:@"Floating move stick: it goes where your thumb lands in the lower left" on:layout.floating_stick
		enabled:YES action:@selector(floatingChanged:)];
	opacity_label = [UILabel new];
	[self sliderFrom:MIN_OPACITY to:MAX_OPACITY value:layout.opacity label:opacity_label
		action:@selector(opacityChanged:)];
	sensitivity_label = [UILabel new];
	[self sliderFrom:MIN_SENSITIVITY to:MAX_SENSITIVITY value:self.controls.sensitivity label:sensitivity_label
		action:@selector(sensitivityChanged:)];
	[self showValues];
	[self button:@"Edit buttons layout" action:@selector(editLayout)];
	[self button:@"Hide, add or size buttons" action:@selector(manageButtons)];
	[self button:@"Reset the buttons" action:@selector(confirmReset)];
}

- (void)showValues
{
	struct layout layout = self.controls.layoutState;

	opacity_label.text = [NSString stringWithFormat:@"Opacity: %d%%", (int)lroundf(layout.opacity * 100.0f)];
	sensitivity_label.text = [NSString stringWithFormat:@"Look sensitivity: %.2fx", self.controls.sensitivity];
}

- (void)change:(void (^)(struct layout *layout))change
{
	struct layout layout = self.controls.layoutState;

	change(&layout);
	self.controls.layoutState = layout;
	[self.controls layoutChanged];
}

- (void)rumbleChanged:(UISwitch *)control
{
	[self change:^(struct layout *layout) { layout->rumble = control.on; }];
}

- (void)gyroscopeChanged:(UISwitch *)control
{
	[self change:^(struct layout *layout) { layout->gyroscope = control.on; }];
}

- (void)floatingChanged:(UISwitch *)control
{
	[self change:^(struct layout *layout) { layout->floating_stick = control.on; }];
}

- (void)opacityChanged:(UISlider *)slider
{
	float value = roundf(slider.value * 20.0f) / 20.0f;

	[self change:^(struct layout *layout) { layout->opacity = clampf(value, MIN_OPACITY, MAX_OPACITY); }];
	[self showValues];
}

- (void)sensitivityChanged:(UISlider *)slider
{
	self.controls.sensitivity = clampf(roundf(slider.value / 0.025f) * 0.025f, MIN_SENSITIVITY, MAX_SENSITIVITY);
	[self showValues];
}

- (void)done
{
	HaloTouchView *controls = self.controls;

	[self dismissViewControllerAnimated:YES completion:^{ [controls optionsClosed]; }];
}

- (void)editLayout
{
	HaloTouchView *controls = self.controls;

	[self dismissViewControllerAnimated:YES completion:^{
		[controls optionsClosed];
		controls.editing = YES;
		[controls setHidden:NO save:YES];
	}];
}

- (void)confirmReset
{
	UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Reset the buttons?"
		message:@"This puts every button back in its place and size, shows them all and removes the copies."
		preferredStyle:UIAlertControllerStyleAlert];

	[alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
	[alert addAction:[UIAlertAction actionWithTitle:@"Reset" style:UIAlertActionStyleDestructive
		handler:^(UIAlertAction *action) {
			(void)action;
			[self change:^(struct layout *layout) { layout_reset(layout); }];
			[self.controls setHidden:NO save:YES];
		}]];
	[self presentViewController:alert animated:YES completion:nil];
}

- (void)manageButtons
{
	UIViewController *list = [UIViewController new];
	UIScrollView *scroll = [UIScrollView new];
	UIStackView *rows = [UIStackView new];
	struct layout layout = self.controls.layoutState;
	int index;

	list.title = @"Buttons";
	list.view.backgroundColor = UIColor.systemBackgroundColor;
	rows.axis = UILayoutConstraintAxisVertical;
	rows.spacing = 8;
	rows.translatesAutoresizingMaskIntoConstraints = NO;
	scroll.translatesAutoresizingMaskIntoConstraints = NO;
	[list.view addSubview:scroll];
	[scroll addSubview:rows];
	[NSLayoutConstraint activateConstraints:@[
		[scroll.topAnchor constraintEqualToAnchor:list.view.safeAreaLayoutGuide.topAnchor],
		[scroll.bottomAnchor constraintEqualToAnchor:list.view.bottomAnchor],
		[scroll.leadingAnchor constraintEqualToAnchor:list.view.safeAreaLayoutGuide.leadingAnchor],
		[scroll.trailingAnchor constraintEqualToAnchor:list.view.safeAreaLayoutGuide.trailingAnchor],
		[rows.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:12],
		[rows.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-12],
		[rows.leadingAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.leadingAnchor constant:16],
		[rows.trailingAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.trailingAnchor constant:-16],
	]];
	for (index = 0; index < layout.count; index++)
	{
		UIStackView *row = [UIStackView new];
		UILabel *name = [UILabel new];
		NSString *title = [self.controls controlName:layout.controls[index].type];

		if (index >= BASE_COUNT)
			title = [title stringByAppendingString:@" (copy)"];
		name.text = [NSString stringWithFormat:@"%@: %d%%%@", title, (int)lroundf(layout.controls[index].size * 100.0f),
			layout.controls[index].shown ? @"" : @" (hidden)"];
		name.adjustsFontSizeToFitWidth = YES;
		row.axis = UILayoutConstraintAxisHorizontal;
		row.spacing = 10;
		[row addArrangedSubview:name];
		for (NSString *action in @[ @"-", @"+", layout.controls[index].shown ? @"Hide" : @"Show", @"Copy" ])
		{
			UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];

			if ([action isEqualToString:@"Copy"] && layout.controls[index].type == KIND_LEFT)
				continue;
			[button setTitle:action forState:UIControlStateNormal];
			button.tag = index;
			[button addTarget:self action:NSSelectorFromString([@{ @"-": @"smaller:", @"+": @"larger:",
				@"Hide": @"toggleShown:", @"Show": @"toggleShown:", @"Copy": @"duplicate:" }[action] copy])
				forControlEvents:UIControlEventTouchUpInside];
			[row addArrangedSubview:button];
		}
		[rows addArrangedSubview:row];
	}
	{
		UIButton *add = [UIButton buttonWithType:UIButtonTypeSystem];

		[add setTitle:@"Add a button" forState:UIControlStateNormal];
		[add addTarget:self action:@selector(chooseButtonToAdd) forControlEvents:UIControlEventTouchUpInside];
		[rows addArrangedSubview:add];
	}
	if (self.navigationController.topViewController != self)
		[self.navigationController popToViewController:self animated:NO];
	[self.navigationController pushViewController:list animated:NO];
}

- (void)resize:(NSInteger)index by:(float)step
{
	[self change:^(struct layout *layout) {
		float size = clampf(roundf(layout->controls[index].size * 10.0f + step) / 10.0f, MIN_SIZE, MAX_SIZE);

		layout->controls[index].size = size;
		layout_move(layout, (int)index, layout->controls[index].x, layout->controls[index].y);
	}];
	[self manageButtons];
}

- (void)smaller:(UIButton *)button
{
	[self resize:button.tag by:-1.0f];
}

- (void)larger:(UIButton *)button
{
	[self resize:button.tag by:1.0f];
}

- (void)toggleShown:(UIButton *)button
{
	[self change:^(struct layout *layout) { layout->controls[button.tag].shown = !layout->controls[button.tag].shown; }];
	[self manageButtons];
}

- (void)duplicate:(UIButton *)button
{
	__block int added = 0;

	[self change:^(struct layout *layout) { added = layout_duplicate(layout, (int)button.tag); }];
	if (added < 0)
		[self.controls message:@"No more buttons" text:@"At most 64 controls. Reset removes the copies."];
	[self manageButtons];
}

- (void)chooseButtonToAdd
{
	UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Add a button" message:nil
		preferredStyle:UIAlertControllerStyleAlert];
	int type;

	for (type = 0; type < BASE_COUNT; type++)
	{
		int chosen = type;

		[sheet addAction:[UIAlertAction actionWithTitle:[self.controls controlName:type] style:UIAlertActionStyleDefault
			handler:^(UIAlertAction *action) {
				(void)action;
				[self change:^(struct layout *layout) { layout_add(layout, chosen); }];
				[self manageButtons];
			}]];
	}
	[sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
	[self presentViewController:sheet animated:YES completion:nil];
}

@end

/* ---------- the overlay over SDL's view (host_metal.m, as the game's window opens) */

static HaloTouchView *overlay;

void host_touch_overlay_install(void *window)
{
	UIWindow *native = (__bridge UIWindow *)window;
	UIView *parent = native.rootViewController.view ? native.rootViewController.view : native;

	if (overlay || !parent)
		return;
	overlay = [[HaloTouchView alloc] initWithFrame:parent.bounds];
	[parent addSubview:overlay];
}
