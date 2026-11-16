/*
XGPU_HDR.H

display.hdr: the renderer's high dynamic range, one contract that every
backend implements (docs/hdr.md tells it at length). The front end
(d3d8_device.c) decides everything that is the game's: whether the frame is
drawn in HDR, which draws may go past the display's SDR white ("paper
white") and how far, and when the 3D view is tone mapped. A backend draws
what it is told: it makes the screen's target floating point, carries out
each draw's tag (below), runs the tone map and encodes the frame for its
display, filling in only what its display says (the headroom, the output's
encoding).

The frame. The back buffer holds the game's own values, blended by the
game's own maths, in half floats: 1.0 is paper white, the encoding is the
game's (sRGB's curve, extended past 1 as sRGB's extended spaces extend it),
the primaries sRGB's. Nothing is made linear but by the tone map and the
encoding. A value past 1 can only come of blending (every pixel shader's
result is clamped to [0, 1], as the NV2A's is), so a frame whose draws add
no light past white is the SDR frame, value for value.

The draws' tags (nv2a_pixel_shader_key.hdr, decided by xgpu_hdr_classify):

- none: as the game blends it, unclamped. Light the game adds (additive
  effects, plasma, lens flares, the sun, the dynamic lights) keeps what it
  adds past white: the highlights.
- a ceiling (XGPU_HDR_CEILING_*): the draw may not take a channel past
  max(what the pixel had, the ceiling), nor below 0. Paper white for
  surfaces and the HUD whose blend could go past white (a double
  multiply, the specular passes, the fog screens, the HUD's additive art),
  so that they stay where the SDR frame has them without dimming the light
  already there; a glow's ceiling (XGPU_HDR_GLOW) for the deliberate glows
  and the surfaces of light. The backend
  blends such a draw itself in its pixel shader (reading the pixel it
  draws over), with the key's blend (XGPU_HDR_BLEND_*), and its
  fixed-function blending off. The destination's colour as a blend factor
  is the pixel's value, but one less it is never below 0.
- a glow's ceiling (XGPU_HDR_GLOW: the first-person weapon's displays and
  glows; the HUD's flashes and the motion tracker's blips; the surfaces
  that are light, the light of the world the game draws in [0, 1]: a
  teleporter's field, a light fixture, a Covenant energy shield, a plasma
  bolt's light volume) brightened toward the ceiling where it is bright, by
  more of the light the draw added (its own colour: what was under it is
  not brightened): xgpu_hdr_expand.
- XGPU_HDR_ALPHA_LIMIT: destination alpha, which the game reads back as a
  blend factor, stays at most 1 (blended in the shader as above, its colour
  unlimited where no ceiling is set).
- XGPU_HDR_LIGHTS: the enhanced dynamic lights' shoulder (a structure's
  lightmap pass) goes on past white: the same curve below white, and what
  the SDR shoulder squeezed under it rolled off toward
  XGPU_HDR_LIGHTS_WHITE instead (xgpu_hdr_lights).
- XGPU_HDR_UNCLAMPED: the pixel stage carries values past 1 (no clamp
  above, where the NV2A's are: the combiners' results and inputs, the
  pixel's colour), for the screen effects' passes, which copy the tone
  mapped view through their target and back (a scope's warp and mask): they
  keep its light past white instead of cutting it at paper white.
- XGPU_HDR_SELF_ILLUMINATION: a surface whose own pass adds its
  self-illumination to its light (a model's; a structure's lightmap pass,
  which the textures' pass multiplies after) draws as the SDR frame has it
  but for its self-illumination (a door's panel, a computer's screen, a
  Forerunner glyph): the pixel less the same pixel without it (the
  constants of the combiner stages the tag names, its colours, made 0) is
  brightened toward XGPU_HDR_SELF_ILLUMINATION_VALUE as a glow is, where
  it is bright (xgpu_hdr_self_illuminate).

The tone map (xgpu_backend_tone_map) brings each window's 3D view into the
display's range before the HUD is drawn over it: XGPU_HDR_MODE_FAITHFUL
leaves every pixel no channel of which is past 1 as it is; one a little
past it keeps its hue, rolled off softly toward the display's peak, and one
far past it (a core of light the game adds up: an explosion's, a plasma
bolt's) takes the SDR frame's colour (its channels clipped, white-hot as
the SDR frame has it) brightened by the light that was clipped;
XGPU_HDR_MODE_PUNCHY does the same with the whole scene's light lifted and
its bright parts more, under a shoulder that starts lower, so that what is
under white keeps its hue, brighter, and the highlights go further toward
the peak. The encoding (the presentation) makes the frame linear, at most
the peak, times paper white, in the display's encoding. Their shaders are
one GLSL source (xgpu_hdr.c), compiled for each backend
(tools/metal_post_shaders.py, tools/vulkan_post_shaders.py); the C below
is the same maths, the reference the tests hold the shaders to.
*/

#ifndef __HALO_LINUX_XGPU_HDR_H
#define __HALO_LINUX_XGPU_HDR_H

/* ---------- settings */

/* display.hdr */
enum
{
	XGPU_HDR_OFF,
	XGPU_HDR_AUTO,
	XGPU_HDR_ON,
};

/* display.hdr_mode */
enum
{
	XGPU_HDR_MODE_FAITHFUL,
	XGPU_HDR_MODE_PUNCHY,
};

/* display.hdr_paper_white_nits: paper white in nits, these at most and
least (0: the system's SDR white); display.hdr_paper_white, its percent of
the system's SDR white while that is 0 (the setting before nits), these at
most and least */
#define XGPU_HDR_PAPER_WHITE_NITS_LEAST 80
#define XGPU_HDR_PAPER_WHITE_NITS_MOST 500
#define XGPU_HDR_PAPER_WHITE_LEAST 50
#define XGPU_HDR_PAPER_WHITE_MOST 250
/* display.hdr_ui_nits: the brightness of what is drawn over the tone mapped
view (the HUD, the menus, the screen flashes) in nits, these at most and
least (0: paper white's) */
#define XGPU_HDR_UI_NITS_LEAST 80
#define XGPU_HDR_UI_NITS_MOST 500
/* the system's SDR white in nits where it gives none: BT.2408's reference
white (Vulkan's, without SDL's), and EDR's 1.0 as macOS's reference modes
show it (Metal's: macOS and iOS give no nits, their SDR white following the
display's brightness) */
#define XGPU_HDR_REFERENCE_WHITE_NITS 203.0f
#define XGPU_HDR_EDR_WHITE_NITS 100.0f

/* paper white in nits from the settings (xgpu_backend_hdr's) and the
system's SDR white in nits */
static inline float xgpu_hdr_paper_white_nits(int nits, int percent, float system_white)
{
	if (nits > 0)
		return (float)nits;
	return system_white * (float)(percent > 0 ? percent : 100) / 100.0f;
}

/* the UI's white over paper white (xgpu_hdr_frame.ui), in any one
unit: display.hdr_ui_nits's (ui, 0 for paper white's), paper white's as
the encoding has it (no brighter than the display), and the display's
peak; the UI no brighter than the peak either */
static inline float xgpu_hdr_ui_white(float ui, float paper_white, float peak)
{
	if (!(ui > 0.0f) || !(paper_white > 0.0f))
		return 1.0f;
	if (peak > 0.0f && ui > peak)
		ui = peak;
	return ui / paper_white;
}

/* a display shows HDR (display.hdr "auto" turns it on) when its potential
headroom is more than this: macOS gives an SDR panel up to twice by dimming
its SDR white below the backlight's (a MacBook Air: 2.0), which is not an
HDR display */
#define XGPU_HDR_DISPLAY_HEADROOM 2.0f

/* ---------- the draws' tags (nv2a_pixel_shader_key.hdr: 16 bits) */

/* the draw's blend (XGPU_BLEND_* source and destination factors,
XGPU_BLEND_ADD... operation), where the shader blends it: with a ceiling or
XGPU_HDR_ALPHA_LIMIT */
#define XGPU_HDR_BLEND(source, destination, operation) \
	((unsigned short)(((source) & 15) | (((destination) & 15) << 4) | (((operation) & 7) << 8)))
#define XGPU_HDR_BLEND_SOURCE(hdr) ((hdr) & 15)
#define XGPU_HDR_BLEND_DESTINATION(hdr) (((hdr) >> 4) & 15)
#define XGPU_HDR_BLEND_OPERATION(hdr) (((hdr) >> 8) & 7)
#define XGPU_HDR_CEILING(ceiling) ((unsigned short)(((ceiling) & 7) << 11))
#define XGPU_HDR_CEILING_OF(hdr) (((hdr) >> 11) & 7)
#define XGPU_HDR_ALPHA_LIMIT 0x4000
#define XGPU_HDR_LIGHTS 0x8000
/* the shader blends the draw itself (its fixed-function blend is off) */
#define XGPU_HDR_SHADER_BLENDS(hdr) (XGPU_HDR_CEILING_OF(hdr) != 0 || ((hdr) & XGPU_HDR_ALPHA_LIMIT) != 0)
/* the draw is a glow, brightened toward its ceiling (xgpu_hdr_expand) */
#define XGPU_HDR_GLOW(hdr) (XGPU_HDR_CEILING_OF(hdr) >= XGPU_HDR_CEILING_WEAPON)
/* the pixel stage unclamped above (a blend operation no draw has, without
a ceiling or the alpha limit: no draw the shader blends) */
#define XGPU_HDR_UNCLAMPED XGPU_HDR_BLEND(0, 0, 7)
#define XGPU_HDR_IS_UNCLAMPED(hdr) ((hdr) == XGPU_HDR_UNCLAMPED)
/* a self-illuminated surface's pass (another blend operation no draw has,
without a ceiling or the alpha limit; XGPU_HDR_LIGHTS may be set too): the
combiner stages whose constants are the self-illumination's colours, a bit
each (stage n's 1 << n) */
#define XGPU_HDR_SELF_ILLUMINATION(stages) ((unsigned short)(XGPU_HDR_BLEND(0, 0, 6) | ((stages) & 0xff)))
#define XGPU_HDR_IS_SELF_ILLUMINATED(hdr) (!XGPU_HDR_SHADER_BLENDS(hdr) && XGPU_HDR_BLEND_OPERATION(hdr) == 6)
#define XGPU_HDR_SELF_ILLUMINATION_STAGES(hdr) ((hdr) & 0xff)

/* the ceilings, in the frame's values (sRGB's curve, extended): paper
white; then the glows': the first-person weapon's, 1.75 times paper
white's light; the HUD's, 1.5 times; the light of the world's, 3 times
(a teleporter's field fills much of the screen: well past the surfaces,
short of an explosion's core) */
enum
{
	XGPU_HDR_CEILING_NONE,
	XGPU_HDR_CEILING_PAPER_WHITE,
	XGPU_HDR_CEILING_WEAPON,
	XGPU_HDR_CEILING_HUD,
	XGPU_HDR_CEILING_LIGHT,
};
#define XGPU_HDR_CEILING_PAPER_WHITE_VALUE 1.0f
#define XGPU_HDR_CEILING_WEAPON_VALUE 1.277043f
#define XGPU_HDR_CEILING_HUD_VALUE 1.194177f
#define XGPU_HDR_CEILING_LIGHT_VALUE 1.612451f

/* a glow's expansion: from this brightest channel of the blended pixel
(the frame's values) up, smoothly to the ceiling at 1; the light of the
world's from lower (a teleporter's field, a bolt's volume are drawn
dimmer than the weapon's displays) */
#define XGPU_HDR_EXPAND_KNEE 0.6f
#define XGPU_HDR_EXPAND_LIGHT_KNEE 0.4f
/* ... the light the draw added at most this many times more */
#define XGPU_HDR_EXPAND_MOST 2.0f

/* a self-illuminated surface's glow (XGPU_HDR_SELF_ILLUMINATION): toward 2
times paper white's light, from the light of the world's knee (a screen's
light is drawn as dim as a fixture's): well past the surfaces, short of the
lights and fixtures (a panel lights no room) */
#define XGPU_HDR_SELF_ILLUMINATION_VALUE 1.353256f
#define XGPU_HDR_SELF_ILLUMINATION_KNEE XGPU_HDR_EXPAND_LIGHT_KNEE

/* the enhanced dynamic lights' white with XGPU_HDR_LIGHTS: the linear light
(the lights' own gamma, 2.2) their shoulder rolls off toward, 8 times paper
white's */
#define XGPU_HDR_LIGHTS_WHITE 8.0f
/* (the same, as the shaders' text has it: the room past white, white less
1; and white in the lights' gamma, 8 to the 1 / 2.2) */
#define XGPU_HDR_LIGHTS_ROOM "7.0"
#define XGPU_HDR_LIGHTS_WHITE_VALUE 2.573330f

/* ---------- the tone map and the encoding */

/* Punchy's lift of the whole scene (linear light), its bright parts'
further lift (from SDR luminance PUNCHY_BRIGHT_KNEE to 1, up to 1 +
PUNCHY_BRIGHT), and its shoulder's knee (of the peak, at most paper white) */
#define XGPU_HDR_PUNCHY_LIFT 1.2f
#define XGPU_HDR_PUNCHY_BRIGHT 0.6f
#define XGPU_HDR_PUNCHY_BRIGHT_KNEE 0.5f
#define XGPU_HDR_PUNCHY_KNEE 0.75f
/* Faithful's colour past white: its own hue at 1 (the frame's values), the
SDR frame's clipped colour from this far past it */
#define XGPU_HDR_FAITHFUL_CLIPPED 2.0f

/* the display's encodings (xgpu_hdr_frame.output) */
enum
{
	/* SDR: the frame's values, clamped to [0, 1] (the window's 8 bits) */
	XGPU_HDR_OUTPUT_SDR,
	/* extended linear sRGB, 1.0 the system's SDR white: macOS's EDR */
	XGPU_HDR_OUTPUT_EXTENDED_LINEAR,
	/* scRGB: extended linear sRGB, 1.0 80 nits (Windows') */
	XGPU_HDR_OUTPUT_SCRGB,
	/* HDR10: BT.2020 primaries, SMPTE ST 2084 (PQ), 1.0 10 000 nits */
	XGPU_HDR_OUTPUT_PQ,
};

/* what the tone map and the encoding are given: the front end's settings,
and what the backend's display says. Paper white and the peak are linear
light: paper white in the output's units (over the system's SDR white for
EDR, xgpu_hdr_paper_white_nits over XGPU_HDR_EDR_WHITE_NITS; nits over 80
for scRGB; nits over 10 000 for PQ), the peak in paper whites (the display's headroom over paper
white: 1 for SDR). ui is the UI's white over paper white
(display.hdr_ui_nits: 1, or 0, for paper white's; 1 for SDR): the frame's
1.0 is the UI's white, the tone map leaves the 3D view in it (its light
over ui), and the encoding takes the frame's light times it. */
struct xgpu_hdr_frame
{
	int mode;
	int output;
	float paper_white;
	float peak;
	float ui;
};


/* ---------- the reference (host code and tests: the system's maths) */

#ifdef XGPU_HDR_REFERENCE
#include <math.h>

static inline float xgpu_hdr_linear(float value)
{
	if (value <= 0.0f)
		return 0.0f;
	return value <= 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f);
}

static inline float xgpu_hdr_encoded(float linear)
{
	if (linear <= 0.0f)
		return 0.0f;
	return linear <= 0.0031308f ? linear * 12.92f : 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
}

static inline float xgpu_hdr_ceiling_value(int ceiling)
{
	return ceiling == XGPU_HDR_CEILING_WEAPON ? XGPU_HDR_CEILING_WEAPON_VALUE :
		ceiling == XGPU_HDR_CEILING_HUD ? XGPU_HDR_CEILING_HUD_VALUE :
		ceiling == XGPU_HDR_CEILING_LIGHT ? XGPU_HDR_CEILING_LIGHT_VALUE : XGPU_HDR_CEILING_PAPER_WHITE_VALUE;
}

static inline float xgpu_hdr_expand_knee(int ceiling)
{
	return ceiling == XGPU_HDR_CEILING_LIGHT ? XGPU_HDR_EXPAND_LIGHT_KNEE : XGPU_HDR_EXPAND_KNEE;
}

static inline float xgpu_hdr_smoothstep(float edge0, float edge1, float x)
{
	float t = (x - edge0) / (edge1 - edge0);

	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	return t * t * (3.0f - 2.0f * t);
}

/* a pixel (rgb) brightened toward a ceiling (its value): its brightest
channel by up to (ceiling - 1) times itself, where it is bright (from the
knee), by more of the light added to what the pixel had (had), at most
XGPU_HDR_EXPAND_MOST times more */
static inline void xgpu_hdr_expand_toward(float rgb[3], const float had[3], float ceiling, float knee)
{
	float added[3], most = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2])), added_most, more;
	int channel;

	for (channel = 0; channel < 3; channel++)
		added[channel] = fmaxf(rgb[channel] - had[channel], 0.0f);
	added_most = fmaxf(added[0], fmaxf(added[1], added[2]));
	if (added_most <= 0.0f)
		return;
	more = fminf((ceiling - 1.0f) * xgpu_hdr_smoothstep(knee, 1.0f, most) * most / added_most, XGPU_HDR_EXPAND_MOST);
	for (channel = 0; channel < 3; channel++)
		rgb[channel] += added[channel] * more;
}

/* a glow's blended pixel (rgb) brightened toward its ceiling, by more of
the light the draw added to what the pixel had (had) */
static inline void xgpu_hdr_expand(float rgb[3], const float had[3], int ceiling)
{
	xgpu_hdr_expand_toward(rgb, had, xgpu_hdr_ceiling_value(ceiling), xgpu_hdr_expand_knee(ceiling));
}

/* a self-illuminated surface's pixel (rgb, as its combiners leave it, in
[0, 1]) brightened toward XGPU_HDR_SELF_ILLUMINATION_VALUE by more of its
self-illumination: what it has past the same pixel without it (base) */
static inline void xgpu_hdr_self_illuminate(float rgb[3], const float base[3])
{
	xgpu_hdr_expand_toward(rgb, base, XGPU_HDR_SELF_ILLUMINATION_VALUE, XGPU_HDR_SELF_ILLUMINATION_KNEE);
}

/* the soft shoulder: identity to the knee, then toward the peak, never
reaching it (Reinhard's, meeting the line at its slope) */
static inline float xgpu_hdr_shoulder(float x, float knee, float peak)
{
	float range = peak - knee, t;

	if (x <= knee)
		return x;
	if (range <= 0.0f)
		return peak;
	t = (x - knee) / range;
	return knee + range * t / (1.0f + t);
}

/* the tone map of one pixel's colour, in place: the light (made linear,
lifted in Punchy) keeps its hue under the shoulder, and toward the SDR
frame's colour, white-hot, brightened by the light the SDR frame clipped,
as it is further past white. Faithful leaves a pixel no channel of which is
past white as it is. */
static inline void xgpu_hdr_tone_map(float rgb[3], int mode, float peak)
{
	static const float luma[3] = { 0.2126f, 0.7152f, 0.0722f };
	float sdr[3], light[3], sdr_luminance = 0.0f, luminance = 0.0f, most, gain = 1.0f, knee = 1.0f, clipped, hue,
		brightened, value_most;
	int channel;

	for (channel = 0; channel < 3; channel++)
	{
		float value = rgb[channel] < 0.0f ? 0.0f : rgb[channel];

		sdr[channel] = xgpu_hdr_linear(value > 1.0f ? 1.0f : value);
		light[channel] = xgpu_hdr_linear(value);
		sdr_luminance += luma[channel] * sdr[channel];
		luminance += luma[channel] * light[channel];
	}
	if (mode == XGPU_HDR_MODE_PUNCHY)
	{
		gain = XGPU_HDR_PUNCHY_LIFT * (1.0f + XGPU_HDR_PUNCHY_BRIGHT *
			xgpu_hdr_smoothstep(XGPU_HDR_PUNCHY_BRIGHT_KNEE, 1.0f, sdr_luminance));
		knee = fminf(1.0f, XGPU_HDR_PUNCHY_KNEE * peak);
	}
	else if (luminance <= sdr_luminance)
	{
		for (channel = 0; channel < 3; channel++)
			rgb[channel] = rgb[channel] < 0.0f ? 0.0f : rgb[channel];
		return;
	}
	value_most = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
	clipped = xgpu_hdr_smoothstep(1.0f, XGPU_HDR_FAITHFUL_CLIPPED, value_most);
	most = fmaxf(light[0], fmaxf(light[1], light[2])) * gain;
	hue = most > 0.0f ? gain * xgpu_hdr_shoulder(most, knee, peak) / most : 0.0f;
	brightened = sdr_luminance > 0.0f ? xgpu_hdr_shoulder(gain * luminance / sdr_luminance, knee, peak) : 0.0f;
	for (channel = 0; channel < 3; channel++)
		rgb[channel] = xgpu_hdr_encoded(light[channel] * hue * (1.0f - clipped) + sdr[channel] * brightened * clipped);
}

/* the 3D view's tone mapped colour (xgpu_hdr_tone_map's) in the frame's
values, in place, where the UI's white is not paper white: its light over
the UI's white (xgpu_hdr_frame.ui; 0 or 1, as it is) */
static inline void xgpu_hdr_to_ui(float rgb[3], float ui)
{
	int channel;

	if (!(ui > 0.0f) || ui == 1.0f)
		return;
	for (channel = 0; channel < 3; channel++)
		rgb[channel] = xgpu_hdr_encoded(xgpu_hdr_linear(rgb[channel]) / ui);
}

/* the encoding of one pixel's colour (the frame's values after the tone
map and the HUD) for the display, in place: linear, times the UI's white,
at most the peak, times paper white (and PQ's primaries and curve) */
static inline void xgpu_hdr_encode(float rgb[3], const struct xgpu_hdr_frame *frame)
{
	float light[3], ui = frame->ui > 0.0f ? frame->ui : 1.0f;
	int channel;

	for (channel = 0; channel < 3; channel++)
	{
		if (frame->output == XGPU_HDR_OUTPUT_SDR)
		{
			rgb[channel] = rgb[channel] < 0.0f ? 0.0f : rgb[channel] > 1.0f ? 1.0f : rgb[channel];
			continue;
		}
		light[channel] = fminf(xgpu_hdr_linear(rgb[channel]) * ui, frame->peak) * frame->paper_white;
	}
	if (frame->output == XGPU_HDR_OUTPUT_SDR)
		return;
	if (frame->output != XGPU_HDR_OUTPUT_PQ)
	{
		for (channel = 0; channel < 3; channel++)
			rgb[channel] = light[channel];
		return;
	}
	{
		/* BT.709 to BT.2020 (the same colours: no saturation is added) */
		static const float to2020[3][3] =
		{
			{ 0.627404f, 0.329283f, 0.043313f },
			{ 0.069097f, 0.919540f, 0.011362f },
			{ 0.016391f, 0.088013f, 0.895595f },
		};
		const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;

		for (channel = 0; channel < 3; channel++)
		{
			float y = to2020[channel][0] * light[0] + to2020[channel][1] * light[1] + to2020[channel][2] * light[2];
			float p = powf(fmaxf(y, 0.0f), m1);

			rgb[channel] = powf((c1 + c2 * p) / (1.0f + c3 * p), m2);
		}
	}
}

/* the enhanced dynamic lights' shoulder (xgpu_spirv.c, xgpu_msl.c):
the light a pixel had (base, the lights' gamma 2.2) with the dynamic light
added (linear); with lights (XGPU_HDR_LIGHTS) on past white */
static inline float xgpu_hdr_lights(float base, float light, int lights)
{
	float had = powf(base < 0.0f ? 0.0f : base > 1.0f ? 1.0f : base, 2.2f);
	float room = fmaxf(1.0f - had, 1.0e-3f);
	float result = had + room * (1.0f - expf(-light / room));

	if (lights)
	{
		float past = fmaxf(had + light - 1.0f, 0.0f);

		result += (XGPU_HDR_LIGHTS_WHITE - 1.0f) * (1.0f - expf(-past / (XGPU_HDR_LIGHTS_WHITE - 1.0f)));
	}
	return powf(result, 1.0f / 2.2f);
}

/* a pixel of the frame (its values, RGBA) for an SDR picture, as BGRA8:
tone mapped at a peak of 1, as an SDR display shows HDR (Faithful's, which
leaves a pixel no channel of which is past white as it is), for the
screenshot key's PNG */
static inline void xgpu_hdr_sdr_pixel(const float rgba[4], unsigned char bgra[4])
{
	float rgb[3] = { rgba[0], rgba[1], rgba[2] };
	int channel;

	/* (Faithful leaves a pixel no channel of which is past white as it is:
	most of a frame, whose six powf a pixel took a 4K frame more than a
	second on the render thread) */
	if (rgb[0] > 1.0f || rgb[1] > 1.0f || rgb[2] > 1.0f)
		xgpu_hdr_tone_map(rgb, XGPU_HDR_MODE_FAITHFUL, 1.0f);
	for (channel = 0; channel < 4; channel++)
	{
		float value = channel == 3 ? rgba[3] : rgb[channel];

		bgra[channel == 3 ? 3 : 2 - channel] =
			(unsigned char)(value <= 0.0f ? 0 : value >= 1.0f ? 255 : (int)(value * 255.0f + 0.5f));
	}
}
#endif

/* ---------- the classification (xgpu_hdr.c) */

/* what the front end knows of a draw into the HDR back buffer */
struct xgpu_hdr_draw
{
	/* the blend: enabled, XGPU_BLEND_* factors and operation; the channels
	written (XGPU_WRITE_*) */
	int blend, blend_source, blend_destination, blend_operation;
	int color_mask;
	/* the game's renderer's category (rasterizer.h's _rasterizer_profile_*,
	-1 outside one: halo_hdr_category) */
	int category;
	/* drawn over the tone mapped 3D view: the HUD and menus */
	int interface;
	/* what the game's code says of the draw (halo_hdr_draw_kind:
	HALO_HDR_KIND_*) */
	int kind;
	/* a structure's lightmap pass lit by the enhanced dynamic lights */
	int dynamic_lights;
	int mode;
};

/* halo_hdr_draw_kind's (port/linux/include/halo_linux_source_fixups.h has
them too) */
#ifndef HALO_HDR_KIND_NONE
#define HALO_HDR_KIND_NONE 0
/* a transparent part of the first-person weapon (and the bits below) */
#define HALO_HDR_KIND_FIRST_PERSON 1
/* ... its shader draws a number (the assault rifle's ammunition) */
#define HALO_HDR_KIND_NUMERIC 2
/* ... its shader is a meter (a plasma weapon's heat) */
#define HALO_HDR_KIND_METER 4
/* a glass or water shader's reflection: a surface's specular light */
#define HALO_HDR_KIND_SPECULAR 8
/* a deliberate glow of the HUD's: a meter flashing, the motion tracker */
#define HALO_HDR_KIND_HUD_GLOW 16
/* a surface that is light (in the 3D view): a transparent shader the map
says emits light (its radiosity power: a teleporter's field, a light
fixture), a plasma shader (an energy shield), a light's volume (a plasma
bolt's) */
#define HALO_HDR_KIND_LIGHT 32
/* a surface's pass that adds its self-illumination to its light (a
model's, a structure's lightmap pass), in the 3D view: the combiner stages
whose constants are its colours, a bit each, in the bits above 8
(HALO_HDR_KIND_SELF_ILLUMINATION_STAGES) */
#define HALO_HDR_KIND_SELF_ILLUMINATION 64
#define HALO_HDR_KIND_SELF_ILLUMINATION_STAGES(stages) ((stages) << 8)
#endif

/* ---------- the display, as the menus and the log show it (xgpu_hdr.c) */

/* what the backend says of its display (xgpu_backend_display_hdr): whether
it shows HDR, its headroom over its SDR white now, and that white in nits:
the system's (reported), or, where it gives none, the one the encoding
takes (XGPU_HDR_REFERENCE_WHITE_NITS on Vulkan, XGPU_HDR_EDR_WHITE_NITS on
Metal) */
struct xgpu_hdr_display
{
	int hdr;
	float headroom;
	float white_nits;
	int white_reported;
};

/* xgpu_hdr_display_text's texts: Video Setup's DISPLAY PEAK row's value
("800 NITS"), its panel's words, the log's line */
enum
{
	XGPU_HDR_DISPLAY_VALUE,
	XGPU_HDR_DISPLAY_WORDS,
	XGPU_HDR_DISPLAY_LOG,
};

/* the display's peak (its headroom times its SDR white) and SDR white, as
text of a kind (XGPU_HDR_DISPLAY_*) */
void xgpu_hdr_display_text(const struct xgpu_hdr_display *display, int kind, char *text, unsigned int size);

/* the tag (nv2a_pixel_shader_key.hdr) of a draw into the HDR back buffer */
unsigned short xgpu_hdr_classify(const struct xgpu_hdr_draw *draw);

/* the GLSL the tone map's and the encoding's shaders are made from
(tools/metal_post_shaders.py, tools/vulkan_post_shaders.py read them out of
xgpu_hdr.c) */
extern const char xgpu_hdr_functions_source[];
/* the tone map's (in place: the pixel drawn over read as the pass's input)
and the encoding's main functions */
extern const char *const xgpu_hdr_programs[2];

#endif
