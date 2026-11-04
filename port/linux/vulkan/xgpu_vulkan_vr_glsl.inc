/*
XGPU_VULKAN_VR_GLSL.INC

The GLSL of the VR mode's fixed passes (the fovea's composition, the light
map, the HUD's and the scope's copies, the visor, its rim, the feed, the
cockpit's display, the vignette, the tint, the eyes' depth, the scope's
window), from which tools/vulkan_vr_shaders.py makes their SPIR-V
(xgpu_vulkan_vr_shaders.c): each pass a function holding its
vertex_source and fragment_source, as OpenCE's OpenGL backend's VR mode
had them. The generator keeps their
equations and uniform names, and changes only interface declarations,
built-ins, row orientation and sRGB framebuffer operations.

Not compiled: the generator reads it. Change a pass here, then run
tools/vulkan_vr_shaders.py.
*/

/* fovea */
static void vr_fovea_composite(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"uniform sampler2DArray fovea;\n"
		/* each eye's inset: its top left (the image's pixels from its top
		left) and size; 1 where its left, top, right or bottom edge is the
		image's */
		"uniform vec4 inset[2];\n"
		"uniform vec4 open[2];\n"
		/* the image's size, the blend's width (pixels) */
		"uniform vec4 image;\n"
		"uniform float show;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		"	int eye = int(gl_ViewID_OVR);\n"
		"	vec2 p = gl_FragCoord.xy;\n"
		"	vec2 point = vec2(p.x, image.y - p.y);\n"
		"	vec4 r = inset[eye];\n"
		"	vec4 d = vec4(point - r.xy, r.xy + r.zw - point) + open[eye] * 1.0e6;\n"
		"	vec2 e = clamp(1.0 - min(d.xy, d.zw) / image.z, 0.0, 1.0);\n"
		"	float w = 1.0 - smoothstep(0.0, 1.0, length(e));\n"
		"	colour = texture(fovea, vec3(p / image.xy, float(2 * eye)));\n"
		"	if (w > 0.0)\n"
		"	{\n"
		"		ivec2 texel = ivec2(p) - ivec2(int(r.x), int(image.y - r.y - r.w));\n"
		"		colour = mix(colour, texelFetch(fovea, ivec3(texel, 2 * eye + 1), 0), w);\n"
		"	}\n"
		"	colour.gb *= mix(1.0, mix(0.55, 1.0, w), show);\n"
		"}\n";
}

/* light */
static void vr_light_map(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"uniform sampler2DArray eyes;\n"
		/* (the map's size, each eye's transform from the head (rows) and
		field of view, as the visor's, and where the map reaches) */
		"uniform vec2 size;\n"
		"uniform vec4 rows[6];\n"
		"uniform vec4 eye_fov[2];\n"
		"uniform vec4 reach;\n"
		"uniform float blend;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		"	float light = 0.0, samples = 0.0;\n"
		"	for (int i = 0; i < 16; i++)\n"
		"	{\n"
		"		vec2 q = (gl_FragCoord.xy + (vec2(float(i & 3), float(i >> 2)) - 1.5) * 0.25) / size;\n"
		"		vec3 d = vec3(mix(reach.x, reach.y, q.x), mix(reach.w, reach.z, q.y), -1.0);\n"
		"		for (int eye = 0; eye < 2; eye++)\n"
		"		{\n"
		/* (in the eye's view, its image's first row its bottom) */
		"			vec3 e = vec3(dot(rows[eye * 3].xyz, d), dot(rows[eye * 3 + 1].xyz, d), dot(rows[eye * 3 + 2].xyz, d));\n"
		"			vec4 f = eye_fov[eye];\n"
		"			vec2 t = e.xy / max(-e.z, 1e-4);\n"
		"			vec2 uv = (t - f.xw) / (f.yz - f.xw);\n"
		"			if (e.z < -1e-3 && uv.x > 0.0 && uv.y > 0.0 && uv.x < 1.0 && uv.y < 1.0)\n"
		"			{\n"
		"				light += dot(texture(eyes, vec3(uv, float(eye))).rgb, vec3(0.2126, 0.7152, 0.0722));\n"
		"				samples += 1.0;\n"
		"			}\n"
		"		}\n"
		"	}\n"
		/* (nothing seen there: what it had) */
		"	colour = vec4(samples > 0.0 ? light / samples : 0.0, 0.0, 0.0, samples > 0.0 ? blend : 0.0);\n"
		"}\n";
}

/* hud */
static void vr_copy_hud_image(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"uniform sampler2D hud_texture;\n"
		"uniform vec2 size;\n"
		"uniform float curve;\n"
		"uniform vec2 half_angles;\n"
		"uniform vec2 extent;\n"
		"uniform float opacity;\n"
		"uniform float glow;\n"
		"uniform float glass;\n"
		/* (the world's light: how much the HUD reacts to it, and where its
		map reaches, in tangents from the middle of the head; the hit's
		disturbance and its pattern; the warning's pulse) */
		"uniform sampler2D light_map;\n"
		"uniform float react;\n"
		"uniform vec4 reach;\n"
		"uniform vec2 hit;\n"
		"uniform float warning;\n"
		/* (a quad's plane: its middle and axes from the head, at a unit's
		distance, and the HUD's region it shows) */
		"uniform vec4 centre;\n"
		"uniform vec4 right_axis;\n"
		"uniform vec4 up_axis;\n"
		"uniform vec4 region;\n"
		/* (its turn across and up along the visor's curve, and its bend) */
		"uniform vec4 wrap;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		/* (row 0 of the image is the target's last) */
		"	vec2 p = gl_FragCoord.xy / size;\n"
		"	vec2 uv = vec2(p.x, 1.0 - p.y);\n"
		/* (the pixel's direction from the head, for the world's light) */
		"	vec3 d = vec3(0.0, 0.0, -1.0);\n"
		"	if (curve > 0.5)\n"
		"	{\n"
		/* the direction's angles across and up: through a quad's plane, or
		a cylinder's angle around and tangent up */
		"		vec2 s = (p * 2.0 - 1.0) * extent;\n"
		"		vec2 angles;\n"
		"		if (curve > 1.5)\n"
		"		{\n"
		"			angles = vec2(s.x, atan(s.y));\n"
		"			d = vec3(sin(s.x), s.y, -cos(s.x));\n"
		"		}\n"
		"		else\n"
		"		{\n"
		"			d = normalize(centre.xyz + right_axis.xyz * s.x + up_axis.xyz * s.y);\n"
		"			angles = vec2(atan(d.x, -d.z), atan(d.y, length(d.xz)));\n"
		"			angles.x -= wrap.x;\n"
		"			angles.y += wrap.z * angles.x * angles.x - wrap.y;\n"
		"		}\n"
		"		uv = vec2(0.5, 0.5) + vec2(0.5, -0.5) * angles / half_angles;\n"
		"		if (uv.x < region.x || uv.y < region.y || uv.x > region.z || uv.y > region.w)\n"
		"		{\n"
		"			colour = vec4(0.0);\n"
		"			return;\n"
		"		}\n"
		"	}\n"
		"	vec2 texel = 1.0 / vec2(textureSize(hud_texture, 0));\n"
		/* a hit: bands of the projection shifted across, each its own way,
		some dimmed */
		"	float dim = 1.0;\n"
		"	if (hit.x > 0.0)\n"
		"	{\n"
		"		float band = floor(uv.y * 48.0);\n"
		"		float h = fract(sin(band * 12.9898 + hit.y * 78.233) * 43758.5453);\n"
		"		float k = fract(sin(band * 39.3468 + hit.y * 11.135) * 24634.6345);\n"
		"		uv.x += (h - 0.5) * 0.008 * hit.x * step(0.4, k);\n"
		"		dim = 1.0 - hit.x * (0.1 + 0.2 * step(0.75, k));\n"
		"	}\n"
		"	vec4 c = texture(hud_texture, uv);\n"
		"	vec4 result = vec4(c.rgb, max(c.r, max(c.g, c.b)));\n"
		"	float coverage = result.a;\n"
		"	vec3 ring = vec3(0.0);\n"
		"	if (glow > 0.0 || react > 0.0)\n"
		"	{\n"
		"		for (int i = 0; i < 8; i++)\n"
		"		{\n"
		"			float a = float(i) * 0.785398 + 0.392699;\n"
		"			ring += texture(hud_texture, uv + vec2(cos(a), sin(a)) * texel * 5.0).rgb;\n"
		"		}\n"
		"	}\n"
		/* (the visor's gold, as the glass has it) */
		"	const vec3 gold = vec3(1.0, 0.80, 0.52);\n"
		"	if (glass > 0.0)\n"
		"	{\n"
		/* light on the glass: a little of its gold, and the world seen
		through it (its alpha, what it covers, less than its light) */
		"		result.rgb *= mix(vec3(1.0), gold, 0.12 * glass);\n"
		"		result.a *= 1.0 - 0.4 * glass;\n"
		"	}\n"
		"	if (glow > 0.0)\n"
		"	{\n"
		"		vec3 light = ring * glow * 0.11, wide = vec3(0.0);\n"
		"		for (int i = 0; i < 8 && glass > 0.0; i++)\n"
		"		{\n"
		"			float a = float(i) * 0.785398 + 0.785398;\n"
		"			wide += texture(hud_texture, uv + vec2(cos(a), sin(a)) * texel * 14.0).rgb;\n"
		"		}\n"
		/* (the bloom, wider, the glass's gold in it) */
		"		light += wide * glow * glass * 0.09 * mix(vec3(1.0), gold, 0.5);\n"
		/* (the warning's red in it, pulsing) */
		"		light = mix(light, vec3(1.0, 0.18, 0.10) * max(light.r, max(light.g, light.b)), 0.6 * warning);\n"
		"		result.rgb += light * (1.0 - result.a);\n"
		"		result.a += max(light.r, max(light.g, light.b)) * (1.0 - result.a);\n"
		"	}\n"
		"	if (glass > 0.0)\n"
		"	{\n"
		/* its reflection in the glass's other surface: faint, blurred, a
		little below (light alone: it covers nothing) */
		"		vec2 ghost = uv - vec2(0.0, 18.0) * texel;\n"
		"		vec3 reflection = texture(hud_texture, ghost + vec2(2.5, 1.5) * texel).rgb +\n"
		"			texture(hud_texture, ghost + vec2(-2.5, -1.5) * texel).rgb +\n"
		"			texture(hud_texture, ghost + vec2(1.5, -2.5) * texel).rgb +\n"
		"			texture(hud_texture, ghost + vec2(-1.5, 2.5) * texel).rgb;\n"
		"		if (curve > 0.5 && (ghost.y < region.y || ghost.y > region.w))\n"
		"			reflection = vec3(0.0);\n"
		"		result.rgb += reflection * 0.25 * 0.10 * glass * gold;\n"
		"	}\n"
		"	if (react > 0.0 && curve > 0.5)\n"
		"	{\n"
		/* the world's light behind it: before bright light the glass
		darkens behind it, softly about each element (its own light and
		its ring's), and it brightens a little; in the dark it dims */
		"		vec2 t = d.xy / max(-d.z, 1e-3);\n"
		"		float world = texture(light_map, (t - reach.xw) / (reach.yz - reach.xw)).r;\n"
		"		float bright = react * smoothstep(0.06, 0.40, world);\n"
		"		float dark = react * (1.0 - smoothstep(0.004, 0.05, world));\n"
		"		float near = clamp(2.5 * coverage + 0.2 * max(ring.r, max(ring.g, ring.b)), 0.0, 1.0);\n"
		"		result.rgb = min(result.rgb * (1.0 + 0.3 * bright), vec3(1.0));\n"
		"		result.a += 0.5 * bright * near * (1.0 - result.a);\n"
		"		result *= 1.0 - 0.3 * dark;\n"
		"	}\n"
		"	colour = result * opacity * dim;\n"
		"}\n";
}

/* scope */
static void vr_copy_scope(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"uniform sampler2D scope_texture;\n"
		"uniform float size;\n"
		/* (the zoom, rounded: up to 99; 0, none; and its cells' size,
		pixels) */
		"uniform int zoom;\n"
		"uniform float zoom_cell;\n"
		/* (the smart-link's window: its half width and half height, of the
		image's half width; the quad's height of its width; the readouts'
		glyphs, -1 past their ends, and their cells' size, pixels) */
		"uniform int smartlink;\n"
		"uniform vec2 window;\n"
		"uniform float height;\n"
		"uniform int text[28];\n"
		"uniform float text_cell;\n"
		/* (drawn as the flat screen's zoomed view, the weapon's own mask and
		HUD in the image: the corner's radius of the rounded window, of the
		image's half width, less than 0 the smart-link's own look; and how
		wide its edge fades, of the same) */
		"uniform float corner;\n"
		"uniform float feather;\n"
		/* (its animation: the seconds since it began to open, how far it
		has closed, the seconds since its level changed) */
		"uniform vec4 animation;\n"
		"out vec4 colour;\n"
		"float hash(vec2 v)\n"
		"{\n"
		"	return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);\n"
		"}\n"
		/* the readouts' glyphs, 3x5 cells, a bit each, from the top row's
		left: the digits, then X, R, A, N, G, E, M, O, /, ., - and a space */
		"const int glyphs[22] = int[22](31599, 11415, 29671, 29647, 23497, 31183, 31215, 29257, 31727,\n"
		"	31695, 2728, 27565, 11245, 27501, 14699, 31143, 24557, 11114, 4772, 2, 448, 0);\n"
		/* whether the zoom's readout lights the cell q (cells from its top
		left, down the display) */
		"float readout(vec2 q)\n"
		"{\n"
		"	if (q.x < 0.0 || q.y < 0.0 || q.y >= 5.0)\n"
		"		return 0.0;\n"
		"	ivec2 cell = ivec2(floor(q));\n"
		"	int count = zoom >= 10 ? 3 : 2, glyph = cell.x / 4, column = cell.x - glyph * 4;\n"
		"	if (glyph >= count || column > 2)\n"
		"		return 0.0;\n"
		"	int code = glyph == count - 1 ? glyphs[10] : glyphs[count == 3 && glyph == 0 ? zoom / 10 : zoom % 10];\n"
		"	return float((code >> (14 - cell.y * 3 - column)) & 1);\n"
		"}\n"
		/* whether the window's readout lights the cell q (as many of its
		glyphs as are typed) */
		"float line_lit(vec2 q, float typed)\n"
		"{\n"
		"	if (q.x < 0.0 || q.y < 0.0 || q.y >= 5.0)\n"
		"		return 0.0;\n"
		"	ivec2 cell = ivec2(floor(q));\n"
		"	int glyph = cell.x / 4, column = cell.x - glyph * 4;\n"
		"	if (glyph >= 28 || float(glyph) >= typed || column > 2 || text[glyph] < 0)\n"
		"		return 0.0;\n"
		"	return float((glyphs[text[glyph]] >> (14 - cell.y * 3 - column)) & 1);\n"
		"}\n"
		/* (how long it is, in glyphs) */
		"int line_length()\n"
		"{\n"
		"	int n = 0;\n"
		"	for (int i = 0; i < 28; i++)\n"
		"		n += text[i] >= 0 ? 1 : 0;\n"
		"	return n;\n"
		"}\n"
		/* (how much a line half w wide lights, d from it: pixels) */
		"float stroke(float d, float w)\n"
		"{\n"
		"	return clamp(w + 0.5 - abs(d), 0.0, 1.0);\n"
		"}\n"
		"void main()\n"
		"{\n"
		/* (pixels from the middle, and the lens's radius in them; row 0
		of the image is the target's last) */
		"	vec2 p = gl_FragCoord.xy - vec2(size * 0.5);\n"
		"	float radius = size * 0.5;\n"
		"	float r = length(p) / radius;\n"
		"	vec2 uv = vec2(0.125 + 0.75 * gl_FragCoord.x / size, 1.0 - gl_FragCoord.y / size);\n"
		"	vec3 c = texture(scope_texture, uv).rgb;\n"
		"	vec3 hud = vec3(0.45, 0.72, 1.0);\n"
		"	float pixel = max(size / 480.0, 1.0);\n"
		"	float alpha;\n"
		"	if (smartlink != 0)\n"
		"	{\n"
		/* the window, w its half extents (pixels); t how far it has opened
		(its size and magnification follow it: vr_scope.c) */
		"		vec2 w = window * radius;\n"
		"		vec2 a = abs(p);\n"
		"		float t = animation.x;\n"
		"		float thin = max(0.6 * size / 1024.0, 0.5);\n"
		"		vec3 white = mix(hud, vec3(1.0), 0.6);\n"
		/* as the flat screen's zoomed view: the image as it is, within the
		rounded window (a circle when the corner is as large as it), its edge
		fading into the world over the feather */
		"		if (corner >= 0.0)\n"
		"		{\n"
		"			float k = min(corner * radius, min(w.x, w.y));\n"
		"			vec2 e = a - (w - vec2(k));\n"
		"			float d = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0) - k;\n"
		"			alpha = clamp(-d / max(feather * radius, 1.0) + 0.5 / max(feather * radius, 1.0), 0.0, 1.0);\n"
		"			colour = vec4(c * alpha, alpha);\n"
		"			return;\n"
		"		}\n"
		/* switching off, as a CRT does: the feed collapses to a bright line
		through the aim (the image squashed into it), then the line to a
		point, and is gone; brighter only as it is thin */
		"		if (animation.y > 0.0)\n"
		"		{\n"
		"			float squash = smoothstep(0.0, 0.6, animation.y);\n"
		"			float shrink = smoothstep(0.6, 1.0, animation.y);\n"
		"			float hh = max(w.y * (1.0 - squash), thin), hw = w.x * (1.0 - shrink);\n"
		"			float e = clamp(min(hw - a.x, hh - a.y) / thin + 1.0, 0.0, 1.0);\n"
		"			vec2 s = vec2(p.x, clamp(p.y * w.y / hh, -w.y, w.y)) + vec2(size * 0.5);\n"
		"			c = texture(scope_texture, vec2(0.125 + 0.75 * s.x / size, 1.0 - s.y / size)).rgb;\n"
		"			c = mix(c * vec3(0.92, 1.0, 1.08), white, pow(squash, 3.0) * 0.9);\n"
		"			colour = vec4(c * e, e);\n"
		"			return;\n"
		"		}\n"
		/* connecting, as it opens: a bright line sweeps down it and
		interference (bands offset, a little noise) settles, as the zoom's
		change of level flickers; the sight and the readouts (typed) last */
		"		float corners = 1.0;\n"
		"		vec2 wf = w;\n"
		"		float scan = t;\n"
		"		float scan_y = w.y - 2.0 * w.y * scan;\n"
		"		float noise = max(1.0 - t, 0.6 * (1.0 - clamp(animation.z / 0.12, 0.0, 1.0)));\n"
		"		float tick_of = floor(t * 12.0) + 7.0 * floor(animation.z * 60.0);\n"
		"		float band = hash(vec2(floor((p.y + w.y) / (0.09 * w.y)), tick_of));\n"
		"		float offset = noise * step(0.7, band) * (band - 0.85) * 0.15 * w.x;\n"
		"		c = texture(scope_texture, vec2(0.125 + 0.75 * (gl_FragCoord.x + offset) / size, uv.y)).rgb;\n"
		"		c += noise * 0.08 * (hash(gl_FragCoord.xy + vec2(tick_of)) - 0.5);\n"
		"		float inside = min(wf.x - a.x, wf.y - a.y);\n"
		/* the feed: tinted, scan lines (about 180 down the window), the
		edges darkened inside */
		"		c *= vec3(0.92, 1.0, 1.08);\n"
		"		c *= 1.0 - 0.06 * (0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159 * 180.0 / w.y));\n"
		"		c *= 0.55 + 0.45 * pow(clamp(inside / (0.12 * w.x), 0.0, 1.0), 0.6);\n"
		/* the crosshair across the window, a gap about the aim; range ticks
		along it (across both ways, down below the aim), every 0.075 of the
		half width, every other longer; the ring and the dot at the aim */
		"		float gap = 0.035 * w.x;\n"
		"		float text_top = -w.y + 0.03 * w.x + 5.0 * text_cell;\n"
		"		float sight = max(stroke(p.y, thin) * step(gap, a.x),\n"
		"			stroke(p.x, thin) * step(gap, a.y) * step(text_top + 2.0 * text_cell, p.y));\n"
		"		float spacing = 0.075 * w.x;\n"
		"		float k = floor(a.x / spacing + 0.5);\n"
		"		float tick = k >= 1.0 ? stroke(a.x - k * spacing, thin) *\n"
		"			step(a.y, (mod(k, 2.0) < 0.5 ? 0.018 : 0.010) * w.x) : 0.0;\n"
		"		float kd = floor(-p.y / spacing + 0.5);\n"
		"		tick = max(tick, kd >= 1.0 ? stroke(-p.y - kd * spacing, thin) *\n"
		"			step(a.x, (mod(kd, 2.0) < 0.5 ? 0.018 : 0.010) * w.x) : 0.0);\n"
		"		float ring = stroke(length(p) - 0.018 * w.x, 1.4 * thin);\n"
		"		float last = clamp((t - 0.75) / 0.25, 0.0, 1.0);\n"
		"		sight = max(max(sight, tick), ring) * step(0.0, inside - 0.02 * w.x) * last;\n"
		"		c = mix(c, vec3(0.0), sight * 0.35);\n"
		"		c = mix(c, hud, sight * 0.85);\n"
		"		float dot_light = clamp(1.6 * pixel + 0.5 - length(p), 0.0, 1.0) * last;\n"
		"		c = mix(c, vec3(1.0, 0.35, 0.15), dot_light * 0.8);\n"
		/* the readout, one line about the middle along the bottom edge
		inside (clear of the HUD's panels above), on its shadow */
		"		int count = line_length();\n"
		"		vec2 q = vec2(p.x / text_cell + float(count * 4 - 1) * 0.5, (text_top - p.y) / text_cell);\n"
		"		float typed = floor(last * float(count) + 0.5);\n"
		"		c = mix(c, vec3(0.0), line_lit(q - vec2(0.5, 0.5), typed) * 0.7 * step(0.0, inside));\n"
		"		c = mix(c, hud, line_lit(q, typed) * 0.95);\n"
		/* the feed; the sweep's line over it */
		"		alpha = inside > 0.0 ? 1.0 : 0.0;\n"
		"		c *= alpha;\n"
		"		float sweep = scan > 0.0 && scan < 1.0 && inside > 0.0 ?\n"
		"			max(stroke(p.y - scan_y, thin * 1.5), 0.5 * exp(-abs(p.y - scan_y) / (0.012 * w.y))) : 0.0;\n"
		"		c = mix(c, white, sweep);\n"
		"		alpha = max(alpha, sweep);\n"
		/* its line, heavier at the corners; the glow outside it */
		"		float corner = step(wf.x - 0.06 * w.x, a.x) * step(wf.y - 0.06 * w.x, a.y);\n"
		"		float edge = corners > 0.0 ?\n"
		"			max(stroke(inside, thin * 1.5) * 0.6, stroke(inside + thin * 1.5, thin * 3.0) * corner) : 0.0;\n"
		"		float glow = inside < 0.0 ? 0.35 * corners * exp(inside / (0.012 * w.x)) : 0.0;\n"
		"		c = mix(c, hud, edge);\n"
		"		alpha = max(alpha, edge);\n"
		"		c += hud * glow * (1.0 - alpha);\n"
		"		alpha = max(alpha, glow * 0.6);\n"
		"		colour = vec4(c, alpha);\n"
		"		return;\n"
		"	}\n"
		"	c *= 1.0 - smoothstep(0.86, 0.97, r);\n"
		/* the reticle: lines' half widths (pixels), the posts from 0.5 of
		the radius out */
		"	vec2 a = abs(p);\n"
		"	float fine = clamp(0.6 * pixel + 0.5 - min(a.x, a.y), 0.0, 1.0);\n"
		"	float post = clamp(2.5 * pixel + 0.5 - min(a.x, a.y), 0.0, 1.0) *\n"
		"		step(0.5 * radius, max(a.x, a.y));\n"
		"	c = mix(c, vec3(0.0), max(fine * 0.85, post));\n"
		"	float dot_light = clamp(1.6 * pixel + 0.5 - length(p), 0.0, 1.0);\n"
		"	c = mix(c, vec3(1.0, 0.35, 0.15), dot_light * 0.8);\n"
		"	alpha = 1.0 - smoothstep(1.0 - 1.5 / radius, 1.0, r);\n"
		/* the zoom: 5 cells tall, from 0.3 of the radius right of and
		below the middle; its shadow half a cell down and right */
		"	if (zoom > 0)\n"
		"	{\n"
		"		vec2 q = (vec2(p.x, -p.y) - vec2(0.3 * radius)) / zoom_cell;\n"
		"		c = mix(c, vec3(0.0), readout(q - vec2(0.5)) * 0.7);\n"
		"		c = mix(c, hud, readout(q) * 0.9);\n"
		"	}\n"
		"	colour = vec4(c * alpha, alpha);\n"
		"}\n";
}

/* visor */
static void vr_draw_visor(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		/* each eye's transform from the head (rows) and its field of view
		(tangents: left, right, up, down), as the rim's */
		"uniform vec4 rows[6];\n"
		"uniform vec4 eye_fov[2];\n"
		/* the eye's ray through the pixel, from the head's middle (its
		direction linear across the view), and where the eye is */
		"out vec3 ray;\n"
		"flat out vec3 origin;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
		"	int eye = int(gl_ViewID_OVR);\n"
		"	vec4 f = eye_fov[eye];\n"
		"	vec3 x = rows[eye * 3].xyz, y = rows[eye * 3 + 1].xyz, z = rows[eye * 3 + 2].xyz;\n"
		/* (the eye pass's clip space is upside down: its upper left
		origin) */
		"	vec3 local = vec3(mix(f.x, f.y, p.x * 0.5 + 0.5), mix(f.w, f.z, -p.y * 0.5 + 0.5), -1.0);\n"
		"	ray = local.x * x + local.y * y + local.z * z;\n"
		"	origin = -(rows[eye * 3].w * x + rows[eye * 3 + 1].w * y + rows[eye * 3 + 2].w * z);\n"
		"	gl_Position = vec4(p, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"uniform float distance;\n"
		/* (tangents: left, right, up, down) */
		"uniform vec4 extent;\n"
		"uniform vec4 fov;\n"
		/* (all that the eyes see: past it, to the extent, the glass fades
		out) */
		"uniform vec4 clear;\n"
		/* the glow on the rim (rgb) and the frame's strength (a) */
		"uniform vec4 rim;\n"
		/* the plasma: the game's glow, the recharge, its level and the flash
		as it is full; each noise map's scale, direction over its period
		(xyz) and time (w) */
		"uniform vec4 plasma;\n"
		"uniform float level;\n"
		"uniform vec4 perpendicular;\n"
		"uniform vec4 parallel;\n"
		"uniform vec4 noise[2];\n"
		"uniform vec4 noise_scale;\n"
		"uniform sampler3D noise0;\n"
		"uniform sampler3D noise1;\n"
		"in vec3 ray;\n"
		"flat in vec3 origin;\n"
		"out vec4 colour;\n"
		"vec4 over(vec4 top, vec4 under) { return top + under * (1.0 - top.a); }\n"
		"void main()\n"
		"{\n"
		/* where the eye's ray meets the glass, in tangents from the
		middle of the head (nothing where the ray does not meet it) */
		"	if (ray.z >= -1e-4)\n"
		"		discard;\n"
		"	vec2 t = (origin.xy + ray.xy * ((-distance - origin.z) / ray.z)) / distance;\n"
		"	vec2 edge = atan(vec2(t.x < 0.0 ? -fov.x : fov.y, t.y > 0.0 ? fov.z : -fov.w));\n"
		"	vec2 n = abs(atan(t)) / edge;\n"
		/* (0 in the middle, 1 at the edge of the view: the visor's rounded
		rim) */
		"	float r = pow(pow(n.x, 4.0) + pow(n.y, 4.0), 0.25);\n"
		/* the helmet's shadow on the glass: from the brow and the chin (the
		visor is wide and low), the sides' faint; never more where they
		meet, so no corners are drawn */
		"	float shade = max(t.y > 0.0 ? smoothstep(0.78, 1.06, n.y) : smoothstep(0.84, 1.06, n.y),\n"
		"		0.5 * smoothstep(0.88, 1.06, n.x));\n"
		"	vec4 light = vec4(vec3(0.85, 0.62, 0.25) * 0.035, 0.035) * rim.a * (1.0 - shade);\n"
		"	light = over(vec4(vec3(0.010, 0.011, 0.013), 1.0) * 0.45 * shade * rim.a, light);\n"
		/* the warning's glow: about the rim, fading inward well before the
		HUD's edges */
		"	light.rgb += rim.rgb * smoothstep(0.80, 1.0, r);\n"
		"	if (plasma.x + plasma.y + plasma.z > 0.0)\n"
		"	{\n"
		/* the noise maps move through the glass as through the armour: its
		directions from the eyes, on a shell about the head */
		"		vec3 d = normalize(vec3(t, -1.0)) * 0.25;\n"
		"		vec4 t0 = texture(noise0, d * noise_scale.x + noise[0].xyz * noise[0].w);\n"
		"		vec4 t1 = texture(noise1, d * noise_scale.y + noise[1].xyz * noise[1].w);\n"
		/* the plasma's combiners (rasterizer_xbox_plasma_energy.c's
		pixel shader, as nv2a_psh.c reads it) */
		"		float x = clamp(t0.b + 0.5 - t1.b, -1.0, 1.0);\n"
		"		float y = clamp(t1.a + 0.5 - t0.a, -1.0, 1.0);\n"
		"		float e = clamp(4.0 * (x >= 0.5 ? max(y, 0.0) * max(y, 0.0) : max(x, 0.0) * max(x, 0.0)), -1.0, 1.0);\n"
		"		float g = e >= 0.5 ? (2.0 * max(e, 0.0) - 1.0) * (2.0 * max(e, 0.0) - 1.0) : 0.0;\n"
		/* (facing the eyes in the middle of the glass, more grazing at its
		edges) */
		"		vec3 tint = mix(parallel.rgb, perpendicular.rgb, 1.0 - 0.15 * r * r);\n"
		"		vec3 energy = clamp((e * tint + 0.5 * g) * e * e, 0.0, 1.0);\n"
		/* how much: the armour's glow, and the recharge filling the glass
		from below to its level, brightest at its front; all of it more at
		the edges than in the middle */
		"		float height = 0.5 + 0.5 * (t.y > 0.0 ? n.y : -n.y);\n"
		"		float fill = plasma.y * (0.5 * smoothstep(level + 0.05, level - 0.05, height) +\n"
		"			0.7 * exp(-pow((height - level) / 0.07, 2.0)));\n"
		"		float amount = clamp(plasma.x + fill + 0.3 * plasma.z, 0.0, 1.0) * plasma.w *\n"
		"			mix(0.06, 1.0, smoothstep(0.45, 0.97, r)) * (1.0 - shade * 0.6);\n"
		"		light.rgb += pow(energy * amount, vec3(2.2)) * 1.0;\n"
		"	}\n"
		/* past all that the eyes see, fading to nothing at its reach, all
		of it (light and shade): no edge to see */
		"	vec4 fade = clamp((vec4(t.x, -t.x, -t.y, t.y) + vec4(-extent.x, extent.y, extent.z, -extent.w)) /\n"
		"		(vec4(-extent.x, extent.y, extent.z, -extent.w) - vec4(-clear.x, clear.y, clear.z, -clear.w)), 0.0, 1.0);\n"
		"	fade = fade * fade * (3.0 - 2.0 * fade);\n"
		"	light *= fade.x * fade.y * fade.z * fade.w;\n"
		"	colour = clamp(light, 0.0, 1.0);\n"
		"}\n";
}

/* rim */
static void vr_draw_rim(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		/* each eye's transform from the head (rows), its field of view
		(tangents: left, right, up, down) */
		"uniform vec4 rows[6];\n"
		"uniform vec4 fov[2];\n"
		/* the edges of the eyes' views on the lip's plane (metres: left,
		right, up, down), that plane's distance before the head */
		"uniform vec4 edges;\n"
		"uniform float distance;\n"
		/* (how far in from the edges its lip begins: a fraction of them) */
		"uniform float reach;\n"
		"out float ring_at;\n"
		"out float ring_shade;\n"
		"out float presence;\n"
		"const int SEGMENTS = 128;\n"
		/* (the rings: out from the opening, toward the face) */
		"const float out_by[4] = float[4](0.0, 0.0, 1.03, 1.7);\n"
		"const float nearer[4] = float[4](1.0, 0.99, 0.9, 0.5);\n"
		"const float shading[4] = float[4](1.7, 1.0, 0.55, 0.3);\n"
		"const ivec2 corners[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(0, 1), ivec2(1, 0), ivec2(1, 1));\n"
		"void main()\n"
		"{\n"
		"	int band = gl_VertexID / (SEGMENTS * 6);\n"
		"	int rest = gl_VertexID - band * SEGMENTS * 6;\n"
		"	ivec2 corner = corners[rest % 6];\n"
		"	int ring = band + corner.y;\n"
		"	float angle = float(rest / 6 + corner.x) * 6.2831853 / float(SEGMENTS);\n"
		"	vec2 c = vec2(cos(angle), sin(angle));\n"
		/* (the visor's opening, wide and low: its brow and its chin a little
		lower toward the temples) */
		"	vec2 u = sign(c) * pow(abs(c), vec2(0.25));\n"
		"	vec2 point = vec2(u.x * (u.x > 0.0 ? edges.y : -edges.x), u.y * (u.y > 0.0 ? edges.z : -edges.w) *\n"
		"		(1.0 - 0.04 * u.x * u.x));\n"
		/* the brow and the chin, the sides barely, the corners not at all */
		"	presence = 0.85 * pow(abs(c.y), 4.0) + 0.2 * pow(abs(c.x), 8.0);\n"
		"	float by = ring == 0 ? 1.0 - reach : ring == 1 ? 1.0 - 0.36 * reach : out_by[ring];\n"
		"	vec3 v = vec3(point * by, -distance) * nearer[ring];\n"
		"	int eye = int(gl_ViewID_OVR);\n"
		"	vec3 e = vec3(dot(rows[eye * 3], vec4(v, 1.0)), dot(rows[eye * 3 + 1], vec4(v, 1.0)),\n"
		"		dot(rows[eye * 3 + 2], vec4(v, 1.0)));\n"
		"	vec4 f = fov[eye];\n"
		/* (linear in the eye's coordinates, not divided here, so that what
		is behind the eye is clipped, not thrown across the view) */
		"	float w = -e.z;\n"
		"	vec2 clip = vec2((2.0 * e.x - (f.y + f.x) * w) / (f.y - f.x), (2.0 * e.y - (f.z + f.w) * w) / (f.z - f.w));\n"
		/* (the eye pass's clip space is upside down: its upper left origin) */
		"	gl_Position = vec4(clip.x, -clip.y, 0.0, w);\n"
		"	ring_at = float(ring);\n"
		"	ring_shade = shading[ring];\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"uniform float strength;\n"
		"uniform float depth_pass;\n"
		"in float ring_at;\n"
		"in float ring_shade;\n"
		"in float presence;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		/* (from nothing at the lip to the bevel's 0.85 and back to nothing at
		the far ring, smoothly: no crease to read as a line) */
		"	float a = 0.85 * smoothstep(0.0, 2.0, ring_at) * (1.0 - smoothstep(2.0, 3.0, ring_at)) * presence * strength;\n"
		"	if (depth_pass > 0.5 && a < 0.5)\n"
		"		discard;\n"
		/* dark armour, olive in the dark */
		"	vec3 armour = vec3(0.085, 0.092, 0.075) * ring_shade;\n"
		"	colour = vec4(armour * a, a);\n"
		"}\n";
}

/* feed */
static void vr_draw_feed(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"uniform sampler2DArray eyes;\n"
		/* (squash, shrink, bright, noise; red, the feed's look,
		interference, a seed; the image's size) */
		"uniform vec4 crt;\n"
		"uniform vec4 look;\n"
		"uniform vec2 image;\n"
		"out vec4 colour;\n"
		"float hash(vec2 v)\n"
		"{\n"
		"	return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);\n"
		"}\n"
		"void main()\n"
		"{\n"
		"	vec2 p = gl_FragCoord.xy, c = image * 0.5, d = p - c;\n"
		"	float hh = max(c.y * (1.0 - crt.x), 1.5), hw = c.x * (1.0 - crt.y);\n"
		"	float n = max(crt.w, look.z * 0.6);\n"
		"	float band = hash(vec2(floor(p.y / (image.y / 32.0)), look.w));\n"
		"	float offset = n * step(0.65, band) * (band - 0.82) * 0.12 * image.x;\n"
		"	vec2 s = vec2(p.x + offset, c.y + d.y * c.y / hh);\n"
		"	vec3 col = texture(eyes, vec3(s / image, float(gl_ViewID_OVR))).rgb;\n"
		"	col += n * 0.25 * (hash(p + vec2(look.w)) - 0.5);\n"
		"	col = mix(col, vec3(1.0, 0.16, 0.12), look.x * 0.35);\n"
		"	if (look.y > 0.5)\n"
		"	{\n"
		"		col *= vec3(0.92, 1.0, 1.08);\n"
		"		col *= 1.0 - 0.07 * (0.5 + 0.5 * sin(p.y * 3.14159 / 3.0));\n"
		"		col *= 1.0 - 0.35 * clamp((length(d) / c.y - 0.55) / 0.6, 0.0, 1.0);\n"
		"	}\n"
		"	vec3 white = vec3(0.45, 0.72, 1.0) * 0.4 + 0.6;\n"
		"	bool inside = abs(d.y) <= hh && abs(d.x) <= hw;\n"
		"	col = inside ? mix(col, white, crt.z) : vec3(0.0);\n"
		"	float glow = !inside && abs(d.x) <= hw ? exp(-(abs(d.y) - hh) / 6.0) * crt.z * 0.6 : 0.0;\n"
		"	colour = vec4(clamp(col + white * glow, 0.0, 1.0), 1.0);\n"
		"}\n";
}

/* cockpit */
static void vr_draw_cockpit_display(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"uniform sampler2DArray eyes;\n"
		"uniform sampler2DArray depth_texture;\n"
		/* each eye's: the wall's axis's point, its up, the way to the
		display's middle and its right (its space), its field of view
		(tangents: left, right, up, down); the wall's radius, the display's
		half angle (radians) and half height (metres); the image's size; the
		wash (0 to 1) and a seed; the readout's glyphs (-1 past its end); the
		depth's mapping (0: none) and layers (4: foveated) */
		"uniform vec4 centre[2];\n"
		"uniform vec4 up[2];\n"
		"uniform vec4 forward[2];\n"
		"uniform vec4 right[2];\n"
		"uniform vec4 fov[2];\n"
		"uniform vec4 shape;\n"
		"uniform vec2 image;\n"
		"uniform vec2 wash;\n"
		"uniform int text[44];\n"
		"uniform vec2 depth_map;\n"
		"uniform int layers;\n"
		"out vec4 colour;\n"
		"float hash(vec2 v)\n"
		"{\n"
		"	return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);\n"
		"}\n"
		/* (the smart-link's glyphs: 3x5 cells from the top row's left, the
		digits, then X, R, A, N, G, E, M, O, /, ., - and a space) */
		"const int glyphs[25] = int[25](31599, 11415, 29671, 29647, 23497, 31183, 31215, 29257, 31727,\n"
		"	31695, 2728, 27565, 11245, 27501, 14699, 31143, 24557, 11114, 4772, 2, 448, 0, 18727, 23533, 23407);\n"
		/* (the hull cameras' labels: L HULL, R HULL, REAR; L, H and U after
		the space) */
		"const int labels[18] = int[18](22, 21, 23, 24, 22, 22, 11, 21, 23, 24, 22, 22, 11, 15, 12, 11, -1, -1);\n"
		"float label_lit(vec2 q, int which, float typed)\n"
		"{\n"
		"	if (q.x < 0.0 || q.y < 0.0 || q.y >= 5.0)\n"
		"		return 0.0;\n"
		"	ivec2 cell = ivec2(floor(q));\n"
		"	int glyph = cell.x / 4, column = cell.x - glyph * 4, count = which == 2 ? 4 : 6;\n"
		"	if (glyph >= count || float(glyph) >= typed || column > 2)\n"
		"		return 0.0;\n"
		"	return float((glyphs[labels[which * 6 + glyph]] >> (14 - cell.y * 3 - column)) & 1);\n"
		"}\n"
		"float line_lit(vec2 q, float typed)\n"
		"{\n"
		"	if (q.x < 0.0 || q.y < 0.0 || q.y >= 5.0)\n"
		"		return 0.0;\n"
		"	ivec2 cell = ivec2(floor(q));\n"
		"	int glyph = cell.x / 4, column = cell.x - glyph * 4;\n"
		"	if (glyph >= 44 || float(glyph) >= typed || column > 2 || text[glyph] < 0)\n"
		"		return 0.0;\n"
		"	return float((glyphs[text[glyph]] >> (14 - cell.y * 3 - column)) & 1);\n"
		"}\n"
		"void main()\n"
		"{\n"
		"	int eye = int(gl_ViewID_OVR);\n"
		"	vec2 f = gl_FragCoord.xy / image;\n"
		"	vec4 t = fov[eye];\n"
		"	vec3 ray = vec3(mix(t.x, t.y, f.x), mix(t.w, t.z, f.y), -1.0);\n"
		"	vec3 u = up[eye].xyz, c = centre[eye].xyz;\n"
		/* (where the ray leaves the wall's cylinder: the eye is inside it) */
		"	vec3 rp = ray - dot(ray, u) * u, op = -c - dot(-c, u) * u;\n"
		"	float a = dot(rp, rp), b = 2.0 * dot(op, rp), k = dot(op, op) - shape.x * shape.x;\n"
		"	float disc = b * b - 4.0 * a * k;\n"
		"	if (a < 1.0e-9 || disc < 0.0)\n"
		"		discard;\n"
		"	float s = (-b + sqrt(disc)) / (2.0 * a);\n"
		"	if (s <= 0.0)\n"
		"		discard;\n"
		"	vec3 rel = ray * s - c;\n"
		"	float angle = atan(dot(rel, right[eye].xyz), dot(rel, forward[eye].xyz));\n"
		"	float v = dot(rel, u) / shape.z;\n"
		"	float av = abs(v);\n"
		"	float px = 1.5 * length(vec2(dFdx(v), dFdy(v)));\n"
		"	if (abs(angle) > shape.y + 6.0 * px * shape.z / shape.x || av > 1.0 + 6.0 * px)\n"
		"		discard;\n"
		"	if (depth_map.x > 0.0)\n"
		"	{\n"
		/* (the eyes' depth is kept reversed: as the game has it, 1 - it) */
		"		float z = 1.0 - texture(depth_texture, vec3(f, float(layers == 4 ? 2 * eye : eye))).r;\n"
		"		if (z < depth_map.x * (1.0 - depth_map.y / (0.96 * s)))\n"
		"			discard;\n"
		"	}\n"
		"	vec3 hud = vec3(0.45, 0.72, 1.0), white = mix(hud, vec3(1.0), 0.6);\n"
		"	float pi = 3.14159265, ad = abs(angle);\n"
		"	float centre = ad < 0.25 * pi ? 0.0 : ad < 0.5 * pi ? 0.375 * pi : ad < 0.75 * pi ? 0.625 * pi : pi;\n"
		"	float halfw = ad < 0.25 * pi || ad >= 0.75 * pi ? 0.25 * pi : 0.125 * pi;\n"
		"	float lx = (ad - centre) / halfw;\n"
		"	if (shape.y < pi - 0.01)\n"
		"	{\n"
		"		centre = 0.0;\n"
		"		halfw = shape.y;\n"
		"		lx = angle / shape.y;\n"
		"	}\n"
		"	float alx = abs(lx);\n"
		"	bool front = ad < 0.25 * pi || shape.y < pi - 0.01;\n"
		"	float line_y = 1.0 - 2.0 * wash.x;\n"
		"	float washed = wash.x >= 1.0 ? 1.0 : smoothstep(line_y + 0.02, line_y - 0.02, v);\n"
		"	float near_line = wash.x < 1.0 ? exp(-abs(v - line_y) / 0.08) : 0.0;\n"
		"	float band = hash(vec2(floor((v + 1.0) * 24.0), wash.y));\n"
		"	float offset = near_line * step(0.6, band) * (band - 0.8) * 0.04 * image.x;\n"
		"	vec3 col = texture(eyes, vec3((gl_FragCoord.xy + vec2(offset, 0.0)) / image, float(eye))).rgb;\n"
		"	vec3 world = texture(eyes, vec3(f, float(eye))).rgb;\n"
		"	col += near_line * 0.15 * (hash(gl_FragCoord.xy + vec2(wash.y)) - 0.5);\n"
		"	float edge = min((1.0 - alx) * halfw * shape.x / shape.z, 1.0 - av);\n"
		"	vec3 feed = col * vec3(0.92, 1.0, 1.08) * (front ? 1.0 : 0.78);\n"
		"	feed *= 1.0 - 0.07 * (0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159 / 3.0));\n"
		"	feed *= 0.6 + 0.4 * smoothstep(0.0, 0.12, edge);\n"
		"	vec3 result = mix(world, feed, washed);\n"
		"	float sweep = wash.x > 0.0 && wash.x < 1.0 ? exp(-abs(v - line_y) / 0.006) : 0.0;\n"
		"	result = mix(result, white, sweep * 0.9);\n"
		"	float outside = max((alx - 1.0) * halfw * shape.x / shape.z, av - 1.0);\n"
		"	float frame = (1.0 - smoothstep(0.0, 2.0 * px, abs(outside - 2.0 * px))) * 0.7;\n"
		"	float corner = step(1.0 - 0.07 * shape.z / (halfw * shape.x), alx) * step(0.85, av);\n"
		"	frame = max(frame, corner * (1.0 - smoothstep(0.0, 3.0 * px, abs(outside - 3.0 * px))));\n"
		"	result = mix(result, hud, frame * clamp(wash.x * 3.0, 0.0, 1.0) * (front ? 1.0 : 0.7));\n"
		"	float cell = 0.0038 / shape.z;\n"
		"	float across = 0.0038 / shape.x;\n"
		"	float typed_part = clamp((wash.x - 0.7) / 0.3, 0.0, 1.0);\n"
		"	if (front)\n"
		"	{\n"
		"		int count = 0;\n"
		"		for (int i = 0; i < 44; i++)\n"
		"			count += text[i] >= 0 ? 1 : 0;\n"
		"		vec2 g = vec2(angle / across + float(count * 4 - 1) * 0.5, (-1.0 + 0.045 / shape.z + 5.0 * cell - v) / cell);\n"
		"		float typed = floor(typed_part * float(count) + 0.5);\n"
		"		result = mix(result, vec3(0.0), line_lit(g - vec2(0.5, 0.5), typed) * 0.7 * washed);\n"
		"		result = mix(result, hud, line_lit(g, typed) * 0.95 * washed);\n"
		"	}\n"
		"	else\n"
		"	{\n"
		"		int which = ad >= 0.75 * pi ? 2 : angle < 0.0 ? 0 : 1;\n"
		"		int count = which == 2 ? 4 : 6;\n"
		"		vec2 g = vec2((ad - centre) * sign(angle) / across + float(count * 4 - 1) * 0.5, (-1.0 + 0.045 / shape.z + 5.0 * cell - v) / cell);\n"
		"		if (which == 2)\n"
		"			g.x = (angle >= 0.0 ? angle - pi : angle + pi) / across + float(count * 4 - 1) * 0.5;\n"
		"		float lit = label_lit(g, which, floor(typed_part * float(count) + 0.5));\n"
		"		float shadow = label_lit(g - vec2(0.5, 0.5), which, floor(typed_part * float(count) + 0.5));\n"
		"		result = mix(result, vec3(0.0), shadow * 0.6 * washed);\n"
		"		result = mix(result, hud * 0.85, lit * 0.9 * washed);\n"
		"	}\n"
		"	float off = shape.w;\n"
		"	if (off > 0.0)\n"
		"	{\n"
		"		float open_half = max(1.0 - smoothstep(0.0, 0.7, off), 0.004);\n"
		"		float shown = step(av, open_half) * step(alx, 1.0);\n"
		"		float line = (1.0 - smoothstep(0.0, 0.02, abs(av - open_half))) * step(alx, 1.0 - smoothstep(0.7, 1.0, off));\n"
		"		result = mix(vec3(0.01), result, shown * (1.0 - smoothstep(0.85, 1.0, off)));\n"
		"		result = mix(result, white, line * (1.0 - smoothstep(0.9, 1.0, off)) * step(0.05, off));\n"
		"		if (alx > 1.0 || av > 1.0)\n"
		"			result = mix(result, vec3(0.01), off);\n"
		"	}\n"
		"	colour = vec4(clamp(result, 0.0, 1.0), 1.0);\n"
		"}\n";
}

/* vignette */
static void vr_draw_vignette(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"out vec2 at;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	at = p;\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		/* (each eye's tangents: left, right, up, down; the angles, radians) */
		"uniform vec4 fov[2];\n"
		"uniform float clear;\n"
		"uniform float fade;\n"
		"uniform float dimming;\n"
		"in vec2 at;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		"	vec4 f = fov[int(gl_ViewID_OVR)];\n"
		"	vec2 t = vec2(mix(f.x, f.y, at.x), mix(f.w, f.z, at.y));\n"
		"	float angle = atan(length(t));\n"
		"	float m = 1.0 - dimming * smoothstep(clear, clear + fade, angle);\n"
		"	colour = vec4(m, m, m, 1.0);\n"
		"}\n";
}

/* tint */
static void vr_draw_tint(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"uniform vec4 tint;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		"	colour = tint;\n"
		"}\n";
}

/* depth */
static void vr_copy_depth(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"out vec2 uv;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		/* (the upper-left clip origin: row 0 at the top) */
		"	uv = vec2(p.x, 1.0 - p.y);\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"uniform sampler2DArray depth_texture;\n"
		"in vec2 uv;\n"
		"void main()\n"
		"{\n"
		"	gl_FragDepth = texture(depth_texture, vec3(uv, float(gl_ViewID_OVR))).r;\n"
		"}\n";
}

/* scope_window */
static void vr_draw_scope_window(void)
{
	static const char vertex_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"layout(num_views = 2) in;\n"
		"void main()\n"
		"{\n"
		"	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char fragment_source[] =
		"#version 450 core\n"
		"#extension GL_OVR_multiview2 : require\n"
		"uniform sampler2D window_texture;\n"
		"uniform sampler2DArray depth_texture;\n"
		/* each eye's: the quad's middle, right and up (its space), its
		field of view (tangents: left, right, up, down); the quad's half
		width and height (metres), its height of its width in the image; the
		eye's image's size; how the weapon shows (0 hidden, 1 ghost, 2
		visible), the depth nearer than which it is, the depth's layers (4:
		foveated, the periphery's) */
		"uniform vec4 centre[2];\n"
		"uniform vec4 right[2];\n"
		"uniform vec4 up[2];\n"
		"uniform vec4 fov[2];\n"
		"uniform vec2 half_size;\n"
		"uniform float height;\n"
		"uniform vec2 image;\n"
		"uniform int gun;\n"
		"uniform float gun_depth;\n"
		"uniform int layers;\n"
		/* (how far the world about the window is dimmed: 0 to 1) */
		"uniform float dim;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		"	int eye = int(gl_ViewID_OVR);\n"
		"	vec2 f = gl_FragCoord.xy / image;\n"
		/* (the eye images' rows from the bottom of the view, as OpenXR's GL
		images are: the first row the view's lowest) */
		"	vec4 t = fov[eye];\n"
		"	vec3 ray = vec3(mix(t.x, t.y, f.x), mix(t.w, t.z, f.y), -1.0);\n"
		"	vec3 n = cross(right[eye].xyz, up[eye].xyz);\n"
		"	float facing = dot(ray, n);\n"
		"	if (abs(facing) < 1.0e-6)\n"
		"	{\n"
		"		colour = vec4(0.0, 0.0, 0.0, dim);\n"
		"		return;\n"
		"	}\n"
		"	float along = dot(centre[eye].xyz, n) / facing;\n"
		"	if (along <= 0.0)\n"
		"	{\n"
		"		colour = vec4(0.0, 0.0, 0.0, dim);\n"
		"		return;\n"
		"	}\n"
		"	vec3 d = ray * along - centre[eye].xyz;\n"
		"	vec2 q = vec2(dot(d, right[eye].xyz) / half_size.x, dot(d, up[eye].xyz) / half_size.y);\n"
		"	if (abs(q.x) > 1.0 || abs(q.y) > 1.0)\n"
		"	{\n"
		"		colour = vec4(0.0, 0.0, 0.0, dim);\n"
		"		return;\n"
		"	}\n"
		"	vec4 c = texture(window_texture, vec2(0.5 + 0.5 * q.x, 0.5 + 0.5 * q.y * height));\n"
		"	if (gun != 0)\n"
		"	{\n"
		/* (the eyes' depth is kept reversed: as the game has it, 1 - it) */
		"		float z = 1.0 - texture(depth_texture, vec3(f, float(layers == 4 ? 2 * eye : eye))).r;\n"
		"		if (z < gun_depth)\n"
		"			c *= gun == 1 ? 0.8 : 0.0;\n"
		"	}\n"
		"	colour = vec4(c.rgb, c.a + (1.0 - c.a) * dim);\n"
		"}\n";
}
