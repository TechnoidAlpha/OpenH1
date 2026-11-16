/*
NV2A_PSH.C

Decoding of Xbox pixel shaders - the NV2A texture shader stages and
register combiners, as held in the pixel shader render states - into the
shader intermediate form (xgpu_shader.h); xgpu_spirv.c and xgpu_msl.c write
the shaders.

A pixel shader runs in three parts:
- texture stages 0-3 (PSTextureModes) fetch t0-t3, some of them using the
  result of an earlier stage (dot product and bump environment modes);
- up to eight general combiner stages (PSRGBInputs/Outputs and
  PSAlphaInputs/Outputs) that each compute A*B, C*D and their sum or mux
  on the registers and write them back;
- the final combiner (PSFinalCombinerInputsABCD/EFG):
  rgb = A*B + (1-A)*C + D, alpha = G.
Register values are clamped to [-1, 1] between stages, as on the hardware.
*/

#include "xgpu.h"
#include "xgpu_shader.h"

#include <string.h>

/* ---------- texture stages */

static unsigned long stage_mode(const struct nv2a_pixel_shader_key *key, int stage)
{
	return (key->texture_modes >> (5 * stage)) & 0x1f;
}

/* the stage whose texel a dependent stage reads */
static int stage_input(const DWORD *state, int stage)
{
	switch (stage)
	{
	case 2: return (state[D3DRS_PSINPUTTEXTURE] >> 16) & 1;
	case 3: return (state[D3DRS_PSINPUTTEXTURE] >> 20) & 3;
	default: return 0;
	}
}

static void texture_stage_decode(const struct nv2a_pixel_shader_key *key, int stage, struct xgpu_texture_stage *decoded)
{
	const DWORD *state = key->combiner_state;
	unsigned long mode = stage_mode(key, stage);

	decoded->declared_mode = (unsigned char)mode;
	decoded->sampler = key->sampler_type[stage];
	/* a stage that samples, with no texture, has nothing (not a pass
	through nor a clip plane, nor a dot product whose texel is unused) */
	if (key->sampler_type[stage] == _xgpu_sampler_none && mode != XGPU_TEXTURE_PASSTHRU &&
		mode != XGPU_TEXTURE_CLIPPLANE && mode != XGPU_TEXTURE_DOT_PRODUCT && mode != XGPU_TEXTURE_DOT_ZW)
	{
		mode = XGPU_TEXTURE_NONE;
	}
	decoded->mode = (unsigned char)mode;
	decoded->input = (unsigned char)stage_input(state, stage);
	decoded->previous = (signed char)(stage - 1);
	decoded->dot_mapping = stage ? (unsigned char)((state[D3DRS_PSDOTMAPPING] >> ((stage - 1) * 4)) & 7) : 0;
	decoded->compare = (unsigned char)((state[D3DRS_PSCOMPAREMODE] >> (4 * stage)) & 0xf);
	decoded->color_sign = key->color_sign[stage];
	decoded->alpha_kill = key->alpha_kill[stage];
}

/* ---------- combiners */

/* the registers a combiner can write (else none) */
static unsigned char destination(unsigned long reg)
{
	switch (reg)
	{
	case XGPU_REGISTER_V0:
	case XGPU_REGISTER_V1:
	case XGPU_REGISTER_T0:
	case XGPU_REGISTER_T1:
	case XGPU_REGISTER_T2:
	case XGPU_REGISTER_T3:
	case XGPU_REGISTER_R0:
	case XGPU_REGISTER_R1:
		return (unsigned char)reg;
	default:
		return XGPU_REGISTER_ZERO;
	}
}

static unsigned char output_mapping(unsigned long flags)
{
	switch (flags & 0x38)
	{
	case 0x08: return XGPU_OUTPUT_BIAS;
	case 0x10: return XGPU_OUTPUT_SHIFT_LEFT_1;
	case 0x18: return XGPU_OUTPUT_SHIFT_LEFT_1_BIAS;
	case 0x20: return XGPU_OUTPUT_SHIFT_LEFT_2;
	case 0x30: return XGPU_OUTPUT_SHIFT_RIGHT_1;
	default: return XGPU_OUTPUT_IDENTITY;
	}
}

/* one combiner input byte */
static void input_decode(unsigned long input, struct xgpu_combiner_input *decoded)
{
	decoded->reg = (unsigned char)(input & 0x0f);
	decoded->alpha = (input & 0x10) != 0;
	decoded->mapping = (unsigned char)((input & 0xe0) >> 5);
}

static void combiner_stage_decode(const DWORD *state, int stage, struct xgpu_combiner_stage *decoded)
{
	DWORD combiner_count = state[D3DRS_PSCOMBINERCOUNT];
	int portion, input;

	decoded->c0 = (unsigned char)((combiner_count & 0x1000) ? stage : 0);
	decoded->c1 = (unsigned char)((combiner_count & 0x10000) ? stage : 0);
	for (portion = 0; portion < 2; portion++)
	{
		BOOL alpha = portion == 1;
		DWORD inputs = alpha ? state[D3DRS_PSALPHAINPUTS0 + stage] : state[D3DRS_PSRGBINPUTS0 + stage];
		DWORD outputs = alpha ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
		unsigned long flags = outputs >> 12;
		struct xgpu_combiner_portion *part = &decoded->portions[portion];

		for (input = 0; input < 4; input++)
			input_decode((inputs >> (24 - 8 * input)) & 0xff, &part->inputs[input]);
		part->ab_dot = !alpha && (flags & 0x02);
		part->cd_dot = !alpha && (flags & 0x01);
		part->mux = (flags & 0x04) != 0;
		part->output_mapping = output_mapping(flags);
		part->ab = destination((outputs >> 4) & 0x0f);
		part->cd = destination(outputs & 0x0f);
		part->sum = destination((outputs >> 8) & 0x0f);
		part->ab_blue_to_alpha = !alpha && (flags & 0x80);
		part->cd_blue_to_alpha = !alpha && (flags & 0x40);
	}
}

/* ---------- the whole shader */

void nv2a_pixel_program_decode(const struct nv2a_pixel_shader_key *key, struct xgpu_pixel_program *program)
{
	const DWORD *state = key->combiner_state;
	DWORD final_abcd = state[D3DRS_PSFINALCOMBINERINPUTSABCD];
	DWORD final_efg = state[D3DRS_PSFINALCOMBINERINPUTSEFG];
	int stage, input;

	memset(program, 0, sizeof(*program));
	for (stage = 0; stage < 4; stage++)
		texture_stage_decode(key, stage, &program->stages[stage]);
	program->combiner_count = state[D3DRS_PSCOMBINERCOUNT] & 0xff;
	if (program->combiner_count > 8)
		program->combiner_count = 8;
	program->mux_msb = (state[D3DRS_PSCOMBINERCOUNT] & 0x100) != 0;
	for (stage = 0; stage < (int)program->combiner_count; stage++)
		combiner_stage_decode(state, stage, &program->combiners[stage]);

	program->final.present = final_abcd != 0 || final_efg != 0;
	/* A to D, then E, F and G; the final combiner maps by inverting or not */
	for (input = 0; input < 7; input++)
	{
		unsigned long byte = input < 4 ? (final_abcd >> (24 - 8 * input)) & 0xff : (final_efg >> (24 - 8 * (input - 4))) & 0xff;

		program->final.inputs[input].reg = (unsigned char)(byte & 0x0f);
		program->final.inputs[input].alpha = (byte & 0x10) != 0;
		program->final.inputs[input].mapping = (byte & 0x20) ? XGPU_MAP_UNSIGNED_INVERT : XGPU_MAP_UNSIGNED_IDENTITY;
	}
	program->final.clamp_sum = (final_efg & 0x80) != 0;
	program->final.invert_v1 = (final_efg & 0x40) != 0;
	program->final.invert_r0 = (final_efg & 0x20) != 0;

	program->fog = key->fog_enable;
	program->fog_mode = key->fog_table_mode;
	program->alpha_test = key->alpha_test_function;
	program->alpha_test_samples = key->alpha_test_samples;
	program->count_samples = key->count_samples;
	program->coverage_alpha = key->coverage_alpha;
	program->point_threshold = key->point_threshold;
	program->per_pixel_lighting = key->per_pixel_lighting;
	program->dynamic_lights = key->dynamic_lights;
	program->multiview = key->multiview;
	program->source_blend = key->source_blend;
	program->ambient_occlusion = key->ambient_occlusion;
	program->hdr = key->hdr;
}
