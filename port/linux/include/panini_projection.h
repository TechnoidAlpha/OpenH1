/*
PANINI_PROJECTION.H

display.panini's projection, the arithmetic alone, which the game
(port/linux/game/panini.c: the HUD's marks, the reticle, the weapon), the
renderers' pass (tools/vulkan_post_shaders.py's PANINI_SOURCE, the same
formulas in GLSL, which Metal's are compiled from), the screen's resolution
(port/linux/src/d3d8_device.c) and the tests share.

The 3D view is drawn as ever, rectilinear, at the field of view the game
gives it; a pass then draws it again through the Panini projection (Sharpless,
Postle and German, "Pannini: A New Projection for Rendering Wide Angle
Perspective Images", 2010) before the HUD and the menus are drawn over it.
A direction at an angle phi across from the view's axis, its height on the
unit cylinder about the vertical axis y_c (its rectilinear height y times
cos phi), is drawn at

	S = (d + 1) / (d + cos phi)
	x' = S sin phi
	y' = S y_c lerp(1, 1 / cos phi, s) = S y (s + (1 - s) cos phi)

d (0 to 1) is the strength: 0 the rectilinear view across, 1 the full
Panini projection, under which a thing at the side of the view keeps its
shape along the horizon (dx'/dphi = S there). Vertical lines stay vertical
for any d and s, and the horizontal line through the centre straight. s (0
to 1) is the vertical compression (Unreal's r.LensDistortion.Panini.S):
0 the Panini projection's own, where lines across above and below the
centre bow toward it; raising it straightens them (at d = 1 and s = 0.5
they are straight), stretching the sides taller. d = 0 and s = 0 is the
rectilinear view itself.

Coordinates here are tangents, as the view's projection has them: a point
of the view at (x, y) (right and up over its distance) is where its
rectilinear projection draws it, x / tx and y / ty across the 3D view from
its centre to its edges (tx, ty the view's half tangents). The Panini image
is scaled to the screen so that the view's sides are at the screen's sides:
the field of view across the screen stays the one chosen, and since x'
depends on x alone, every column of the screen sees as far to the side as
the rectilinear view's. The same scale up and down keeps the pixels square.
That scale (tx over x' of the edge) is the magnification at the centre,
where the projection is the rectilinear one's times it. The rectilinear
view at the field of view chosen covers the whole screen through it, for
any d and s from 0 to 1: at the top and bottom corners exactly when s is 0
(the inverse's y there is ty), within them otherwise; so the game draws the
view it always did, and the pass only redraws it. To keep the centre as
sharp as it was, the view is drawn at the centre's magnification times the
pixels each way (the screen's supersampling, d3d8_device.c).

The widest field of view (display.fov's and display.vehicle_fov's, degrees
across 16:9) follows from that. A rectilinear view never reaches 180
degrees, and as it nears it the view the game draws widens without bound
for the same screen: its tangent, and with it the centre's magnification,
grows as 1 / cos of half the angle. The centre is then drawn from fewer of
its pixels than the screen has, whatever the supersampling (at most twice
the pixels each way). Through the projection at full strength, the centre
keeps 82% of the screen's sharpness at 150 degrees, 59% at 160, 32% at 170
and 17% at 175 (the magnification 2.43, 3.38, 6.24, 12.0 over the
supersampling's 2); without it, 150's rectilinear view already stretches
what is at its sides 15 times across and 3.9 times up. So the widest is 150, or 160 while the projection is on,
the widest whose centre keeps over half its sharpness. Wider would need the
view drawn as several views (each a part of the angle) and joined, which
this pass does not do.
*/

#ifndef PANINI_PROJECTION_H
#define PANINI_PROJECTION_H

#include <math.h>

/* display.fov's and display.vehicle_fov's widest, in degrees across 16:9:
the rectilinear view's, and while the projection is on (above) */
#define FIELD_OF_VIEW_MOST 150.0
#define PANINI_FIELD_OF_VIEW_MOST 160.0

/* the field of view drawn for one a setting names (degrees across 16:9, at
the projection's strength, 0 off): 0 (the stock view) outside 20 to
PANINI_FIELD_OF_VIEW_MOST, and FIELD_OF_VIEW_MOST at most while the
projection is off */
static __inline double panini_field_of_view(double degrees, float strength)
{
	if (!(degrees >= 20.0 && degrees <= PANINI_FIELD_OF_VIEW_MOST))
		return 0.0;
	return strength > 0.0f || degrees <= FIELD_OF_VIEW_MOST ? degrees : FIELD_OF_VIEW_MOST;
}

struct panini_projection
{
	/* the strength (d) and the vertical compression (s), each 0 to 1 */
	float d, s;
	/* the rectilinear view's half tangents, across and up */
	float tx, ty;
	/* the screen's scale of the Panini image (the view's sides at the
	screen's): the magnification at the centre, 1 or more */
	float scale;
};

/* the projection of a view of half tangents tx and ty at strength d and
vertical compression s (each limited to 0 to 1); 0 (and the rectilinear
view: d, s 0, scale 1) where the view is no view */
static __inline int panini_projection_fit(struct panini_projection *projection, float d, float s, float tx, float ty)
{
	float c;

	projection->d = d > 0.0f ? (d < 1.0f ? d : 1.0f) : 0.0f;
	projection->s = s > 0.0f ? (s < 1.0f ? s : 1.0f) : 0.0f;
	projection->tx = tx;
	projection->ty = ty;
	projection->scale = 1.0f;
	if (!(tx > 0.0f && tx < 1.0e6f && ty > 0.0f && ty < 1.0e6f))
	{
		projection->d = projection->s = 0.0f;
		return 0;
	}
	/* (tx over x' at the edge: S sin phi over tan phi is S cos phi) */
	c = 1.0f / sqrtf(1.0f + tx * tx);
	projection->scale = (projection->d + c) / ((projection->d + 1.0f) * c);
	return 1;
}

/* the magnification at the centre alone, for a view tx across */
static __inline float panini_projection_scale(float d, float tx)
{
	struct panini_projection projection;

	panini_projection_fit(&projection, d, 0.0f, tx, 1.0f);
	return projection.scale;
}

/* where a direction of the view, at rectilinear tangents x and y, is drawn on
the screen: its tangents there (x' and y' at the screen's scale: the screen's
side at tx, its top at ty, as the rectilinear view's) */
static __inline void panini_projection_forward(const struct panini_projection *projection, float x, float y,
	float *screen_x, float *screen_y)
{
	float c = 1.0f / sqrtf(1.0f + x * x);
	float S = (projection->d + 1.0f) / (projection->d + c);

	*screen_x = projection->scale * S * x * c;
	*screen_y = projection->scale * S * y * (projection->s + (1.0f - projection->s) * c);
}

/* the inverse: the direction a point of the screen, at tangents x' and y'
(as the forward's), shows, as rectilinear tangents. cos phi from x':
(k + 1) c^2 + 2 k d c + k d^2 - 1 = 0, k = (x' / (d + 1))^2 */
static __inline void panini_projection_inverse(const struct panini_projection *projection, float screen_x,
	float screen_y, float *x, float *y)
{
	float d = projection->d;
	float u = screen_x / projection->scale;
	float v = screen_y / projection->scale;
	float k = u * u / ((d + 1.0f) * (d + 1.0f));
	float c = (sqrtf(1.0f + k * (1.0f - d * d)) - k * d) / (k + 1.0f);
	float S;

	/* (past 90 degrees across, which no screen shows: the farthest the
	rectilinear view goes) */
	if (!(c > 1.0e-6f))
		c = 1.0e-6f;
	S = (d + 1.0f) / (d + c);
	*x = u / (S * c);
	*y = v / (S * (projection->s + (1.0f - projection->s) * c));
}

#endif
