/*
XGPU_SHADER.H

The shader intermediate form: the NV2A's programs decoded once, every bit
field resolved, for the emitters to write in a shading language (SPIR-V:
xgpu_spirv.c; MSL: xgpu_msl.c). nv2a_vsh.c decodes vertex programs, nv2a_psh.c pixel
shaders.

The form is the hardware's own operations, typed: these programs have no
control flow, a few dozen kinds of operation, and each kind has a spelling
in every target language, so an emitter is a walk over the form and a table
of spellings.
*/

#ifndef __HALO_LINUX_XGPU_SHADER_H
#define __HALO_LINUX_XGPU_SHADER_H

/* ---------- vertex programs */

#define XGPU_VERTEX_PROGRAM_MAXIMUM 136

/* the MAC (vector) unit's operations, as the hardware numbers them */
enum
{
	XGPU_MAC_NOP, XGPU_MAC_MOV, XGPU_MAC_MUL, XGPU_MAC_ADD, XGPU_MAC_MAD, XGPU_MAC_DP3, XGPU_MAC_DPH, XGPU_MAC_DP4,
	XGPU_MAC_DST, XGPU_MAC_MIN, XGPU_MAC_MAX, XGPU_MAC_SLT, XGPU_MAC_SGE, XGPU_MAC_ARL,
};

/* the ILU (scalar) unit's */
enum
{
	XGPU_ILU_NOP, XGPU_ILU_MOV, XGPU_ILU_RCP, XGPU_ILU_RCC, XGPU_ILU_RSQ, XGPU_ILU_EXP, XGPU_ILU_LOG, XGPU_ILU_LIT,
};

/* where an operand is read from */
enum
{
	XGPU_VERTEX_NONE,
	/* r0 to r11, and r12, which reads back the position output */
	XGPU_VERTEX_TEMPORARY,
	XGPU_VERTEX_INPUT,
	XGPU_VERTEX_CONSTANT,
};

/* the temporary that is the position output */
#define XGPU_VERTEX_POSITION 12

/* the output registers (o[]) */
enum
{
	XGPU_OUTPUT_POSITION, XGPU_OUTPUT_DIFFUSE, XGPU_OUTPUT_SPECULAR, XGPU_OUTPUT_FOG, XGPU_OUTPUT_POINT_SIZE,
	XGPU_OUTPUT_BACK_DIFFUSE, XGPU_OUTPUT_BACK_SPECULAR,
	XGPU_OUTPUT_TEXTURE0, XGPU_OUTPUT_TEXTURE1, XGPU_OUTPUT_TEXTURE2, XGPU_OUTPUT_TEXTURE3,
	XGPU_OUTPUT_UNUSED,
};

struct xgpu_vertex_operand
{
	/* XGPU_VERTEX_* */
	unsigned char file;
	/* the temporary (to 15: past 12 the hardware has none), the input, or
	the constant (the instruction's: every constant operand of an
	instruction reads the same one) */
	unsigned char index;
	/* x, y, z, w: 0 to 3 */
	unsigned char swizzle[4];
	unsigned char negate;
};

struct xgpu_vertex_instruction
{
	/* XGPU_MAC_* and XGPU_ILU_* (the values past them do nothing but
	write 0) */
	unsigned char mac, ilu;
	struct xgpu_vertex_operand a, b, c;
	/* the constant operands are c[a0 + index] */
	unsigned char relative;
	/* the units' destinations: masks with bit 3 x ... bit 0 w, and the
	temporary each writes (XGPU_VERTEX_POSITION, the position): the ILU's
	is r1 when both units run */
	unsigned char mac_mask, mac_temporary;
	unsigned char ilu_mask, ilu_temporary;
	/* the output: its mask, whether it is an output register (else the
	constant memory, which Halo's programs do not write), the register
	(XGPU_OUTPUT_*; output_address as the hardware numbers it), and which
	unit feeds it */
	unsigned char output_mask, output_is_register, output, output_address, output_from_ilu;
	/* the output is written: a register, by a unit that runs */
	unsigned char writes_output;
	/* the screen conversion's reciprocal of the position's w (an rcc of
	r12.w): the clip position is what r12 holds before it */
	unsigned char captures_clip;
	/* the program's last */
	unsigned char last;
};

struct xgpu_vertex_program
{
	unsigned long instruction_count;
	struct xgpu_vertex_instruction instructions[XGPU_VERTEX_PROGRAM_MAXIMUM];
	/* of the microcode: the program's identity from run to run */
	unsigned long long hash;
	/* an instruction captures the clip position (the program projects
	through clip space) */
	unsigned char projects;
};

/* the instructions whose dot products are the clip position's z and w as
the screen conversion captures it (the last to write each before the last
capture): the MAC's DP4 or DPH, the same for both, of one input or
temporary that does not change between them, each with a constant row (the
game's projection: c[-94] and c[-93]). The backends keep depth reversed
(xgpu_backend.h, "Reversed depth"), w - z: which, from these, the emitters
compute as one dot product with the difference of the rows, as exact as w
is, where z and w apart lose the depth's last bits to the size of the
world's coordinates. 0 if the program has none. */
static __inline int xgpu_vertex_program_depth_rows(const struct xgpu_vertex_program *program,
	unsigned long *z_instruction, unsigned long *w_instruction)
{
	long writers[2] = { -1, -1 }, captured[2] = { -1, -1 };
	const struct xgpu_vertex_instruction *z, *w;
	unsigned long index, first, last;
	int component;

	for (index = 0; index < program->instruction_count; index++)
	{
		const struct xgpu_vertex_instruction *instruction = &program->instructions[index];
		BOOL mac = instruction->mac != XGPU_MAC_NOP && instruction->mac != XGPU_MAC_ARL;
		BOOL dot = instruction->mac == XGPU_MAC_DP4 || instruction->mac == XGPU_MAC_DPH;

		/* (the capture reads the position before the instruction writes) */
		if (instruction->captures_clip)
		{
			captured[0] = writers[0];
			captured[1] = writers[1];
		}
		/* z is mask bit 1, w bit 0: a write by anything but a dot product
		leaves none */
		for (component = 0; component < 2; component++)
		{
			unsigned char bit = (unsigned char)(2 >> component);
			BOOL by_mac = (mac && instruction->mac_temporary == XGPU_VERTEX_POSITION && (instruction->mac_mask & bit)) ||
				(instruction->writes_output && instruction->output == XGPU_OUTPUT_POSITION &&
				!instruction->output_from_ilu && (instruction->output_mask & bit));
			BOOL by_ilu = (instruction->ilu != XGPU_ILU_NOP && instruction->ilu_temporary == XGPU_VERTEX_POSITION &&
				(instruction->ilu_mask & bit)) || (instruction->writes_output &&
				instruction->output == XGPU_OUTPUT_POSITION && instruction->output_from_ilu &&
				(instruction->output_mask & bit));

			if (by_ilu || (by_mac && !dot))
				writers[component] = -2;
			else if (by_mac)
				writers[component] = (long)index;
		}
	}
	if (captured[0] < 0 || captured[1] < 0 || captured[0] == captured[1])
		return 0;
	z = &program->instructions[captured[0]];
	w = &program->instructions[captured[1]];
	if (z->mac != w->mac || z->b.file != XGPU_VERTEX_CONSTANT || w->b.file != XGPU_VERTEX_CONSTANT ||
		(z->a.file != XGPU_VERTEX_INPUT && z->a.file != XGPU_VERTEX_TEMPORARY) ||
		(z->a.file == XGPU_VERTEX_TEMPORARY && z->a.index >= XGPU_VERTEX_POSITION) ||
		z->a.file != w->a.file || z->a.index != w->a.index || z->a.negate != w->a.negate ||
		z->a.swizzle[0] != w->a.swizzle[0] || z->a.swizzle[1] != w->a.swizzle[1] ||
		z->a.swizzle[2] != w->a.swizzle[2] || z->a.swizzle[3] != w->a.swizzle[3])
	{
		return 0;
	}
	/* (the operand the same in both: not written from the first on) */
	first = (unsigned long)(captured[0] < captured[1] ? captured[0] : captured[1]);
	last = (unsigned long)(captured[0] < captured[1] ? captured[1] : captured[0]);
	for (index = first; z->a.file == XGPU_VERTEX_TEMPORARY && index < last; index++)
	{
		const struct xgpu_vertex_instruction *instruction = &program->instructions[index];

		if ((instruction->mac != XGPU_MAC_NOP && instruction->mac != XGPU_MAC_ARL && instruction->mac_mask &&
			instruction->mac_temporary == z->a.index) ||
			(instruction->ilu != XGPU_ILU_NOP && instruction->ilu_mask && instruction->ilu_temporary == z->a.index))
		{
			return 0;
		}
	}
	*z_instruction = (unsigned long)captured[0];
	*w_instruction = (unsigned long)captured[1];
	return 1;
}

/* decodes NV2A vertex program microcode (the instruction words after the
program header, four each) */
void nv2a_vertex_program_decode(const DWORD *instructions, unsigned long instruction_count,
	struct xgpu_vertex_program *program);

/* the bit of a stage's border axes (the emitters' options, border) that
says it samples a region of its texture (XGPU_ADDRESS_REGION): its
texture_scale the region's width and height, then its left and top, and
the border drawn beyond the region, on u and v */
#define XGPU_BORDER_REGION 8

/* the bit of a stage's border axes (the emitters' options, border) that
says its texels are the high-res text's distance fields (XGPU_ADDRESS_MSDF,
of a 2D texture): the texel sampled is white, its alpha the coverage of the
field's edge, the median of red, green and blue at 0.5, smoothed over a
pixel of the screen (XGPU_MSDF_RANGE texels of the field from its 0 to its
1, as many pixels as the lookup's derivatives make them, at least one) */
#define XGPU_BORDER_MSDF 16
/* the range of the high-res text's distance fields (text_hires.c), in
texels from the field's 0 to its 1; and as the shaders' source has it */
#define XGPU_MSDF_RANGE 6.0f
#define XGPU_MSDF_RANGE_TEXT "6.0"

/* ---------- pixel programs

A pixel shader runs in three parts: texture stages 0-3 fetch t0-t3, some of
them from the result of an earlier stage; up to eight general combiner
stages compute A*B, C*D and their sum or mux on the registers and write them
back; the final combiner computes rgb = A*B + (1-A)*C + D, alpha = G. */

/* the texture stages' modes (PSTextureModes), as the hardware numbers them */
enum
{
	XGPU_TEXTURE_NONE, XGPU_TEXTURE_PROJECT2D, XGPU_TEXTURE_PROJECT3D, XGPU_TEXTURE_CUBEMAP, XGPU_TEXTURE_PASSTHRU,
	XGPU_TEXTURE_CLIPPLANE, XGPU_TEXTURE_BUMPENVMAP, XGPU_TEXTURE_BUMPENVMAP_LUMINANCE, XGPU_TEXTURE_BRDF,
	XGPU_TEXTURE_DOT_ST, XGPU_TEXTURE_DOT_ZW, XGPU_TEXTURE_DOT_REFLECT_DIFFUSE, XGPU_TEXTURE_DOT_REFLECT_SPECULAR,
	XGPU_TEXTURE_DOT_STR_3D, XGPU_TEXTURE_DOT_STR_CUBE, XGPU_TEXTURE_DEPENDENT_AR, XGPU_TEXTURE_DEPENDENT_GB,
	XGPU_TEXTURE_DOT_PRODUCT, XGPU_TEXTURE_DOT_REFLECT_SPECULAR_CONSTANT,
};

/* how a dot product stage maps its input texel (PSDotMapping) */
enum
{
	XGPU_DOT_ZERO_TO_ONE, XGPU_DOT_MINUS_ONE_TO_ONE_D3D, XGPU_DOT_MINUS_ONE_TO_ONE_GL, XGPU_DOT_MINUS_ONE_TO_ONE,
	XGPU_DOT_HILO_ONE, XGPU_DOT_HILO_HEMISPHERE_D3D, XGPU_DOT_HILO_HEMISPHERE_GL, XGPU_DOT_HILO_HEMISPHERE,
};

struct xgpu_texture_stage
{
	/* XGPU_TEXTURE_*: as PSTextureModes declares it, and as it runs (none
	where the stage would sample and has no texture) */
	unsigned char declared_mode, mode;
	/* _xgpu_sampler_* (xgpu.h) */
	unsigned char sampler;
	/* the stage whose texel a dependent or dot product stage reads
	(PSInputTexture), and the stage before it, which a bump environment
	stage reads (-1 for stage 0) */
	unsigned char input;
	signed char previous;
	/* XGPU_DOT_*: how a dot product stage maps its input */
	unsigned char dot_mapping;
	/* a clip plane stage's comparisons: bit n set, component n kept where
	it is at least 0, else where it is less (PSCompareMode) */
	unsigned char compare;
	/* channels (bit 0 alpha ... bit 3 blue) whose texels are signed
	(D3DTSS_COLORSIGN), and the texel discarded where its alpha is 0
	(D3DTSS_ALPHAKILL) */
	unsigned char color_sign, alpha_kill;
};

/* the combiners' registers, as the hardware numbers them */
enum
{
	XGPU_REGISTER_ZERO = 0, XGPU_REGISTER_C0 = 1, XGPU_REGISTER_C1 = 2, XGPU_REGISTER_FOG = 3,
	XGPU_REGISTER_V0 = 4, XGPU_REGISTER_V1 = 5,
	XGPU_REGISTER_T0 = 8, XGPU_REGISTER_T1 = 9, XGPU_REGISTER_T2 = 10, XGPU_REGISTER_T3 = 11,
	XGPU_REGISTER_R0 = 12, XGPU_REGISTER_R1 = 13,
	/* the final combiner's alone */
	XGPU_REGISTER_V1R0_SUM = 14, XGPU_REGISTER_EF_PRODUCT = 15,
};

/* a general combiner input's mapping */
enum
{
	XGPU_MAP_UNSIGNED_IDENTITY, XGPU_MAP_UNSIGNED_INVERT, XGPU_MAP_EXPAND_NORMAL, XGPU_MAP_EXPAND_NEGATE,
	XGPU_MAP_HALFBIAS_NORMAL, XGPU_MAP_HALFBIAS_NEGATE, XGPU_MAP_SIGNED_IDENTITY, XGPU_MAP_SIGNED_NEGATE,
};

/* a combiner output's mapping */
enum
{
	XGPU_OUTPUT_IDENTITY, XGPU_OUTPUT_BIAS, XGPU_OUTPUT_SHIFT_LEFT_1, XGPU_OUTPUT_SHIFT_LEFT_1_BIAS,
	XGPU_OUTPUT_SHIFT_LEFT_2, XGPU_OUTPUT_SHIFT_LEFT_2_BIAS, XGPU_OUTPUT_SHIFT_RIGHT_1, XGPU_OUTPUT_SHIFT_RIGHT_1_BIAS,
};

struct xgpu_combiner_input
{
	/* XGPU_REGISTER_* */
	unsigned char reg;
	/* its alpha (else its rgb, or its blue in an alpha portion) */
	unsigned char alpha;
	/* XGPU_MAP_* (the final combiner's: invert or not) */
	unsigned char mapping;
};

/* the rgb or alpha portion of a general combiner stage */
struct xgpu_combiner_portion
{
	struct xgpu_combiner_input inputs[4];
	/* A.B and C.D as dot products (rgb only), and the sum a mux by r0's
	alpha */
	unsigned char ab_dot, cd_dot, mux;
	/* XGPU_OUTPUT_* (a left shift by 2 with the bias, and the right
	shift's bias, as identity: the translator never handled them) */
	unsigned char output_mapping;
	/* the registers written (XGPU_REGISTER_ZERO: none), and A.B or C.D's
	blue copied into its register's alpha (rgb only) */
	unsigned char ab, cd, sum;
	unsigned char ab_blue_to_alpha, cd_blue_to_alpha;
};

struct xgpu_combiner_stage
{
	/* [0] rgb, [1] alpha */
	struct xgpu_combiner_portion portions[2];
	/* the stage's C0 and C1 among the eight (the same for every stage
	unless PSCombinerCount makes them unique) */
	unsigned char c0, c1;
};

struct xgpu_final_combiner
{
	/* written (else the result is r0) */
	unsigned char present;
	/* A to G (mapping: XGPU_MAP_UNSIGNED_IDENTITY or _INVERT) */
	struct xgpu_combiner_input inputs[7];
	/* the V1 + R0 sum clamped, and each of its terms inverted */
	unsigned char clamp_sum, invert_v1, invert_r0;
};

struct xgpu_pixel_program
{
	struct xgpu_texture_stage stages[4];
	unsigned long combiner_count;
	struct xgpu_combiner_stage combiners[8];
	/* the muxes read r0's alpha's high bit (else its low bit) */
	unsigned char mux_msb;
	struct xgpu_final_combiner final;
	/* fog: enabled, and its D3DFOG_* table mode */
	unsigned char fog, fog_mode;
	/* the alpha test's D3DCMP_* function (0: none), and in a multisampled
	target, its samples a pixel (coverage in proportion to alpha) */
	unsigned long alpha_test;
	unsigned char alpha_test_samples;
	/* the draw's features, as nv2a_pixel_shader_key has them (xgpu.h):
	samples counted for a visibility test, the HUD meters' coverage alpha,
	their thresholds read at level 0 (texture 0), per-pixel model lighting
	(its lights), the enhanced dynamic lights (XGPU_DYNAMIC_LIGHTS_*), the
	views drawn (XGPU_MULTIVIEW_*), the source blend factor applied here
	(1 to 4), the ambient occlusion's light (XGPU_AMBIENT_OCCLUSION_LIGHT_*) */
	unsigned char count_samples, coverage_alpha, point_threshold, per_pixel_lighting, dynamic_lights, multiview;
	unsigned char source_blend, ambient_occlusion;
	/* display.hdr's tag (nv2a_pixel_shader_key.hdr: xgpu_hdr.h) */
	unsigned short hdr;
};

/* the texture stage that replaces the fragment's depth, DOT_ZW (the game's
z-sprites: a bullet impact's puff), with the previous stage's dot product
over its own (the previous stage a dot product), in the 24-bit depth buffer's units: the depth of a point up to
the sprite's radius nearer than its card, shaped by its second map, so that
the surface it stands on does not cut it in half; 0 if none. The emitters
write min(that, the card's depth), a conservative depth (only ever nearer),
or, in the VR mode's soft effects, fade the effect from it (xgpu_spirv.h). */
#define XGPU_REPLACED_DEPTH_UNITS 16777215.0f
static __inline int xgpu_pixel_program_depth_stage(const struct xgpu_pixel_program *program)
{
	int stage;

	/* (after a dot product stage: the hardware's is the last stage's) */
	for (stage = 2; stage < 4; stage++)
	{
		if (program->stages[stage].mode == XGPU_TEXTURE_DOT_ZW && program->stages[stage - 1].mode == XGPU_TEXTURE_DOT_PRODUCT)
			return stage;
	}
	return 0;
}

struct nv2a_pixel_shader_key;

/* decodes a pixel shader key (xgpu.h): the combiner render states, texture
modes and the draw's other pixel state */
void nv2a_pixel_program_decode(const struct nv2a_pixel_shader_key *key, struct xgpu_pixel_program *program);

#endif
