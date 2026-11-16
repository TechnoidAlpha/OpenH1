/*
XGPU_MSL.C

Metal Shading Language from the shader intermediate form (xgpu_shader.h),
for the Metal backend; the conventions are in xgpu_msl.h. The programs are
the SPIR-V emitter's (xgpu_spirv.c) operation for operation, so that the
two draw alike.

Metal's compiler warns of locals that are declared and not read, which the
GLSL's fixed set of registers would have in every program: the registers a
program reads are found first, and only they are declared and written (a
write to a register nothing reads is dead on the hardware too).
*/

#include "xgpu.h"
#include "xgpu_shader.h"
#include "xgpu_msl.h"
#include "xgpu_hdr.h"
#include "xgpu_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the draw's values, as struct xgpu_msl_uniforms lays them out */
static const char uniforms_declaration[] =
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct xgpu_uniforms\n"
	"{\n"
	"\tfloat4 viewport_scale;\n"
	"\tfloat4 viewport_offset;\n"
	"\tfloat point_size, screen_offset, alpha_reference, depth_bias;\n"
	"\tfloat4 ps_c0[8];\n"
	"\tfloat4 ps_c1[8];\n"
	"\tfloat4 ps_final_c0;\n"
	"\tfloat4 ps_final_c1;\n"
	"\tfloat4 fog_color;\n"
	"\tfloat4 fog_parameters;\n"
	"\tfloat4 bump_matrix[4];\n"
	"\tfloat4 bump_luminance[4];\n"
	"\tfloat4 texture_scale[4];\n"
	"\tfloat4 texture_lod_bias;\n"
	"\tfloat4 blend_constant;\n"
	"\tfloat4 texture_border[4];\n"
	"\tfloat4 texture_border_linear;\n"
	"\tfloat4 model_lights[%d];\n%s"
	"};\n";

/* the functions of a list ("static inline" each) that the text past at
calls, or that one of them calls, put in at */
static void functions_insert(struct xgpu_text *text, unsigned long at, const char *functions)
{
	const char *starts[16], *ends[16];
	BOOL used[16];
	struct xgpu_text result = { 0 };
	int count = 0, index, other;
	const char *next;

	for (next = strstr(functions, "static inline "); next && count < 16; count++)
	{
		starts[count] = next;
		next = strstr(next + 1, "\nstatic inline ");
		ends[count] = next ? ++next : functions + strlen(functions);
	}
	/* (a function calls only those before it) */
	for (index = count - 1; index >= 0; index--)
	{
		const char *name = strchr(starts[index] + strlen("static inline "), ' ') + 1;
		char call[64];

		snprintf(call, sizeof(call), "%.*s(", (int)(strchr(name, '(') - name), name);
		used[index] = strstr(text->buffer + at, call) != NULL;
		for (other = index + 1; other < count && !used[index]; other++)
		{
			const char *found = used[other] ? strstr(starts[other], call) : NULL;

			used[index] = found && found < ends[other];
		}
	}
	xgpu_text_append(&result, "%.*s", (int)at, text->buffer);
	for (index = 0; index < count; index++)
	{
		if (used[index])
			xgpu_text_append(&result, "%.*s", (int)(ends[index] - starts[index]), starts[index]);
	}
	xgpu_text_append(&result, "%s", text->buffer + at);
	free(text->buffer);
	*text = result;
}

/* ---------- vertex programs */

static const char *output_name(unsigned char output)
{
	switch (output)
	{
	case XGPU_OUTPUT_POSITION: return "oPos";
	case XGPU_OUTPUT_DIFFUSE: return "oD0";
	case XGPU_OUTPUT_SPECULAR: return "oD1";
	case XGPU_OUTPUT_FOG: return "oFog";
	case XGPU_OUTPUT_POINT_SIZE: return "oPts";
	case XGPU_OUTPUT_TEXTURE0: return "oT0";
	case XGPU_OUTPUT_TEXTURE1: return "oT1";
	case XGPU_OUTPUT_TEXTURE2: return "oT2";
	case XGPU_OUTPUT_TEXTURE3: return "oT3";
	/* the back faces' colors, which no pixel shader reads (the GLSL hands
	them on unread), and none */
	default: return NULL;
	}
}

static void write_mask(unsigned long mask, char *out)
{
	/* bit 3 is x */
	int count = 0;

	if (mask & 8) out[count++] = 'x';
	if (mask & 4) out[count++] = 'y';
	if (mask & 2) out[count++] = 'z';
	if (mask & 1) out[count++] = 'w';
	out[count] = 0;
}

/* the operands a unit's operation reads: bit 0 A, 1 B, 2 C */
static unsigned int mac_operands(unsigned char mac)
{
	switch (mac)
	{
	case XGPU_MAC_MOV: case XGPU_MAC_ARL: return 1;
	case XGPU_MAC_ADD: return 5;
	case XGPU_MAC_MAD: return 7;
	case XGPU_MAC_MUL: case XGPU_MAC_DP3: case XGPU_MAC_DPH: case XGPU_MAC_DP4: case XGPU_MAC_DST:
	case XGPU_MAC_MIN: case XGPU_MAC_MAX: case XGPU_MAC_SLT: case XGPU_MAC_SGE:
		return 3;
	default: return 0;
	}
}

static unsigned int ilu_operands(unsigned char ilu)
{
	return ilu != XGPU_ILU_NOP && ilu <= XGPU_ILU_LIT ? 4 : 0;
}

/* what a program reads and computes: the units whose results reach the
position, an output, a temporary read later or a0 where it is read; the
temporaries (bit n rn, 0 to 11) and inputs they read */
struct vertex_usage
{
	unsigned char mac[XGPU_VERTEX_PROGRAM_MAXIMUM], ilu[XGPU_VERTEX_PROGRAM_MAXIMUM];
	unsigned long temporaries, inputs;
	BOOL a0, operands[3], mac_used, ilu_used;
};

static BOOL temporary_read(const struct vertex_usage *usage, unsigned char temporary)
{
	return temporary == XGPU_VERTEX_POSITION || (temporary < XGPU_VERTEX_POSITION && (usage->temporaries & (1UL << temporary)));
}

static void operand_usage(struct vertex_usage *usage, const struct xgpu_vertex_operand *operand, int relative)
{
	if (operand->file == XGPU_VERTEX_TEMPORARY && operand->index < XGPU_VERTEX_POSITION)
		usage->temporaries |= 1UL << operand->index;
	else if (operand->file == XGPU_VERTEX_INPUT && operand->index < XGPU_VERTEX_ATTRIBUTE_COUNT)
		usage->inputs |= 1UL << operand->index;
	else if (operand->file == XGPU_VERTEX_CONSTANT && relative)
		usage->a0 = TRUE;
}

static void vertex_usage(const struct xgpu_vertex_program *program, const struct nv2a_vertex_lighting *lighting,
	struct vertex_usage *usage)
{
	unsigned long previous_temporaries = ~0UL, index;
	BOOL previous_a0 = TRUE;

	/* from everything read, down to what the units that are needed read,
	until nothing more drops out */
	usage->temporaries = ~0UL;
	usage->a0 = TRUE;
	do
	{
		struct vertex_usage next;

		memset(&next, 0, sizeof(next));
		previous_temporaries = usage->temporaries;
		previous_a0 = usage->a0;
		for (index = 0; index < program->instruction_count; index++)
		{
			const struct xgpu_vertex_instruction *instruction = &program->instructions[index];
			BOOL output = instruction->writes_output && output_name(instruction->output);
			unsigned int operands = 0;

			next.mac[index] = instruction->mac != XGPU_MAC_NOP &&
				((instruction->mac == XGPU_MAC_ARL && usage->a0) ||
				(instruction->mac != XGPU_MAC_ARL && instruction->mac_mask && temporary_read(usage, instruction->mac_temporary)) ||
				(output && !instruction->output_from_ilu));
			next.ilu[index] = instruction->ilu != XGPU_ILU_NOP &&
				((instruction->ilu_mask && temporary_read(usage, instruction->ilu_temporary)) ||
				(output && instruction->output_from_ilu));
			if (next.mac[index])
				operands |= mac_operands(instruction->mac);
			if (next.ilu[index])
				operands |= ilu_operands(instruction->ilu);
			if (operands & 1)
				operand_usage(&next, &instruction->a, instruction->relative);
			if (operands & 2)
				operand_usage(&next, &instruction->b, instruction->relative);
			if (operands & 4)
				operand_usage(&next, &instruction->c, instruction->relative);
			next.operands[0] |= (operands & 1) != 0;
			next.operands[1] |= (operands & 2) != 0;
			next.operands[2] |= (operands & 4) != 0;
			next.mac_used |= next.mac[index];
			next.ilu_used |= next.ilu[index];
		}
		/* the lighting's normal and position, read where the GLSL reads them */
		if (lighting && lighting->lights != XGPU_ENVIRONMENT_LIGHTING)
		{
			if (lighting->normal_instruction < program->instruction_count && lighting->normal_register < XGPU_VERTEX_POSITION)
				next.temporaries |= 1UL << lighting->normal_register;
			if (lighting->lights == 2 && lighting->position_instruction < program->instruction_count &&
				lighting->position_register < XGPU_VERTEX_POSITION)
			{
				next.temporaries |= 1UL << lighting->position_register;
			}
			else if (lighting->lights == 1 && lighting->world_found && lighting->world_instruction < program->instruction_count &&
				lighting->world_register < XGPU_VERTEX_POSITION)
			{
				next.temporaries |= 1UL << lighting->world_register;
			}
		}
		if (lighting && lighting->lights == XGPU_ENVIRONMENT_LIGHTING)
			next.inputs |= 0xf;
		*usage = next;
	}
	while (usage->temporaries != previous_temporaries || usage->a0 != previous_a0);
}

static void operand(struct xgpu_text *text, const struct xgpu_vertex_operand *operand, int relative)
{
	static const char swizzle_names[] = "xyzw";

	xgpu_text_append(text, "%s", operand->negate ? "-" : "");
	switch (operand->file)
	{
	case XGPU_VERTEX_TEMPORARY:
		/* r12 reads back the position output; past it there are none */
		if (operand->index == XGPU_VERTEX_POSITION)
			xgpu_text_append(text, "oPos");
		else if (operand->index < XGPU_VERTEX_POSITION)
			xgpu_text_append(text, "r%lu", (unsigned long)operand->index);
		else
			xgpu_text_append(text, "float4(0.0)");
		break;
	case XGPU_VERTEX_INPUT:
		xgpu_text_append(text, "v%lu", (unsigned long)operand->index);
		break;
	case XGPU_VERTEX_CONSTANT:
		if (relative)
			xgpu_text_append(text, "c[clamp(a0 + %lu, 0, %d)]", (unsigned long)operand->index,
				XGPU_VERTEX_CONSTANT_COUNT - 1);
		else
			xgpu_text_append(text, "c[%lu]", (unsigned long)operand->index);
		break;
	default:
		xgpu_text_append(text, "float4(0.0)");
		break;
	}
	xgpu_text_append(text, ".%c%c%c%c", swizzle_names[operand->swizzle[0]], swizzle_names[operand->swizzle[1]],
		swizzle_names[operand->swizzle[2]], swizzle_names[operand->swizzle[3]]);
}

static const char vertex_functions[] =
	"static inline float4 nv2a_finite(float4 v)\n"
	"{\n"
	"\treturn select(v, float4(0.0), (as_type<uint4>(v) & 0x7f800000u) == 0x7f800000u);\n"
	"}\n"
	"static inline float4 unpack_normpacked3(uint p)\n"
	"{\n"
	"\tint x = int(p << 21) >> 21;\n"
	"\tint y = int(p << 10) >> 21;\n"
	"\tint z = int(p) >> 22;\n"
	"\treturn float4(float(x) / 1023.0, float(y) / 1023.0, float(z) / 511.0, 1.0);\n"
	"}\n"
	"static inline float4 nv2a_rcc(float x)\n"
	"{\n"
	"\tfloat r = 1.0 / x;\n"
	"\tif (r > 0.0) r = clamp(r, 5.42101e-20, 1.884467e+19);\n"
	"\telse r = clamp(r, -1.884467e+19, -5.42101e-20);\n"
	"\treturn float4(r);\n"
	"}\n"
	"static inline float4 nv2a_exp(float x)\n"
	"{\n"
	"\treturn float4(exp2(floor(x)), fract(x), exp2(x), 1.0);\n"
	"}\n"
	"static inline float4 nv2a_log(float x)\n"
	"{\n"
	"\tx = abs(x);\n"
	"\tif (x == 0.0) return float4(-1.0e30, 1.0, -1.0e30, 1.0);\n"
	"\tfloat e = floor(log2(x));\n"
	"\treturn float4(e, x / exp2(e), log2(x), 1.0);\n"
	"}\n"
	"static inline float4 nv2a_lit(float4 s)\n"
	"{\n"
	"\tfloat specular = s.x > 0.0 ? pow(max(s.y, 0.0), clamp(s.w, -127.9961, 127.9961)) : 0.0;\n"
	"\treturn float4(1.0, max(s.x, 0.0), specular, 1.0);\n"
	"}\n";

/* the lighting's outputs (the pixel shader's model_lighting) */
static BOOL lighting_position(const struct nv2a_vertex_lighting *lighting)
{
	return lighting && (lighting->lights == 2 || lighting->world_found || lighting->lights == XGPU_ENVIRONMENT_LIGHTING);
}

char *xgpu_msl_vertex_shader(const struct xgpu_vertex_program *program, unsigned long packed_attribute_mask,
	unsigned long integer_attribute_mask, const struct nv2a_vertex_lighting *lighting,
	const struct xgpu_msl_options *options)
{
	struct xgpu_text text = { 0 };
	struct vertex_usage usage;
	unsigned long index, functions_at, depth_z, depth_w;
	BOOL environment = lighting && lighting->lights == XGPU_ENVIRONMENT_LIGHTING;
	BOOL depth_rows = xgpu_vertex_program_depth_rows(program, &depth_z, &depth_w);

	(void)options;
	vertex_usage(program, lighting, &usage);
	xgpu_text_append(&text, uniforms_declaration, XGPU_MODEL_LIGHT_COUNT, "");

	/* the attributes read, as the vertex descriptor gives them */
	if (usage.inputs)
	{
		xgpu_text_append(&text, "struct xgpu_vertex_in\n{\n");
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			if (!(usage.inputs & (1UL << index)))
				continue;
			xgpu_text_append(&text, "\t%s v%lu [[attribute(%lu)]];\n", (packed_attribute_mask & (1UL << index)) ? "uint" :
				(integer_attribute_mask & (1UL << index)) ? "int4" : "float4", index, index);
		}
		xgpu_text_append(&text, "};\n");
	}
	/* (the position invariant: the passes of a surface meet exactly, as
	GLSL's invariant gl_Position; MTLCompileOptions preserveInvariance) */
	xgpu_text_append(&text,
		"struct xgpu_vertex_out\n"
		"{\n"
		"\tfloat4 position [[position, invariant]];\n"
		"\tfloat point_size [[point_size]];\n"
		"\tfloat4 xD0 [[user(xD0)]];\n"
		"\tfloat4 xD1 [[user(xD1)]];\n"
		"\tfloat4 xT0 [[user(xT0)]];\n"
		"\tfloat4 xT1 [[user(xT1)]];\n"
		"\tfloat4 xT2 [[user(xT2)]];\n"
		"\tfloat4 xT3 [[user(xT3)]];\n"
		"\tfloat xFog [[user(xFog)]];\n");
	/* (the pixel shader's model_lighting; the normal's length in w) */
	if (lighting)
		xgpu_text_append(&text, "\tfloat4 xWorldNormal [[user(xWorldNormal)]];\n");
	if (lighting_position(lighting))
		xgpu_text_append(&text, "\tfloat3 xWorldPosition [[user(xWorldPosition)]];\n");
	if (environment)
		xgpu_text_append(&text, "\tfloat3 xWorldBinormal [[user(xWorldBinormal)]];\n\tfloat3 xWorldTangent [[user(xWorldTangent)]];\n");
	/* (display.ambient_occlusion: lit for each vertex, the ambient light
	apart: xgpu_spirv.c's vertex_light_split) */
	if (lighting && lighting->split && !environment)
		xgpu_text_append(&text, "\tfloat3 xLightAmbient [[user(xLightAmbient)]];\n\tfloat3 xLightPoint [[user(xLightPoint)]];\n");
	xgpu_text_append(&text, "};\n");

	functions_at = text.length;
	xgpu_text_append(&text, "vertex xgpu_vertex_out xgpu_vertex(%sconstant float4 *c [[buffer(%d)]],\n"
		"\tconstant xgpu_uniforms &u [[buffer(%d)]])\n{\n",
		usage.inputs ? "xgpu_vertex_in in [[stage_in]], " : "", XGPU_MSL_BUFFER_CONSTANTS, XGPU_MSL_BUFFER_UNIFORMS);
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (!(usage.inputs & (1UL << index)))
			continue;
		if (packed_attribute_mask & (1UL << index))
			xgpu_text_append(&text, "\tfloat4 v%lu = unpack_normpacked3(in.v%lu);\n", index, index);
		else if (integer_attribute_mask & (1UL << index))
			xgpu_text_append(&text, "\tfloat4 v%lu = float4(in.v%lu);\n", index, index);
		else
			xgpu_text_append(&text, "\tfloat4 v%lu = in.v%lu;\n", index, index);
	}
	for (index = 0; index < XGPU_VERTEX_POSITION; index++)
	{
		if (usage.temporaries & (1UL << index))
			xgpu_text_append(&text, "\tfloat4 r%lu = float4(0.0);\n", index);
	}
	xgpu_text_append(&text,
		"\tfloat4 oPos = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oD0 = float4(0.0, 0.0, 0.0, 1.0), oD1 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oT0 = float4(0.0, 0.0, 0.0, 1.0), oT1 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oT2 = float4(0.0, 0.0, 0.0, 1.0), oT3 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oFog = float4(1.0), oPts = float4(u.point_size);\n");
	if (usage.a0)
		xgpu_text_append(&text, "\tint a0 = 0;\n");
	if (usage.operands[0])
		xgpu_text_append(&text, "\tfloat4 A;\n");
	if (usage.operands[1])
		xgpu_text_append(&text, "\tfloat4 B;\n");
	if (usage.operands[2])
		xgpu_text_append(&text, "\tfloat4 C;\n");
	if (usage.mac_used)
		xgpu_text_append(&text, "\tfloat4 mac;\n");
	if (usage.ilu_used)
		xgpu_text_append(&text, "\tfloat4 ilu;\n");
	xgpu_text_append(&text, "\tfloat4 clip_position = float4(0.0);\n\tbool clip_captured = false;\n");
	/* the point and the projection's rows its depth is the dot products of
	(xgpu_vertex_program_depth_rows) */
	if (depth_rows)
		xgpu_text_append(&text, "\tfloat4 depth_point = float4(0.0), depth_z_row = float4(0.0), depth_w_row = float4(0.0);\n");
	if (lighting)
		xgpu_text_append(&text, "\tfloat4 xWorldNormal = float4(0.0);\n");
	if (lighting_position(lighting))
		xgpu_text_append(&text, "\tfloat3 xWorldPosition = float3(0.0);\n");
	/* a structure's vertices are in the world: as they are */
	if (environment)
	{
		xgpu_text_append(&text, "\txWorldPosition = v0.xyz;\n\txWorldNormal = float4(v1.xyz, 1.0);\n"
			"\tfloat3 xWorldBinormal = v2.xyz;\n\tfloat3 xWorldTangent = v3.xyz;\n");
	}

	for (index = 0; index < program->instruction_count; index++)
	{
		const struct xgpu_vertex_instruction *instruction = &program->instructions[index];
		unsigned int operands = (usage.mac[index] ? mac_operands(instruction->mac) : 0) |
			(usage.ilu[index] ? ilu_operands(instruction->ilu) : 0);
		const char *output = output_name(instruction->output);
		char mask[5];

		xgpu_text_append(&text, "\t/* %lu */\n", index);
		/* the lighting's normal and position, before the program reuses them */
		if (lighting && !environment && index == lighting->normal_instruction)
		{
			xgpu_text_append(&text, "\txWorldNormal = float4(r%lu.xyz, length(r%lu.xyz));\n",
				lighting->normal_register, lighting->normal_register);
		}
		if (lighting && lighting->lights == 2 && index == lighting->position_instruction)
			xgpu_text_append(&text, "\txWorldPosition = r%lu.xyz;\n", lighting->position_register);
		else if (lighting && lighting->lights == 1 && lighting->world_found && index == lighting->world_instruction)
			xgpu_text_append(&text, "\txWorldPosition = r%lu.xyz;\n", lighting->world_register);
		if (operands & 1)
		{
			xgpu_text_append(&text, "\tA = "); operand(&text, &instruction->a, instruction->relative); xgpu_text_append(&text, ";\n");
		}
		if (operands & 2)
		{
			xgpu_text_append(&text, "\tB = "); operand(&text, &instruction->b, instruction->relative); xgpu_text_append(&text, ";\n");
		}
		if (operands & 4)
		{
			xgpu_text_append(&text, "\tC = "); operand(&text, &instruction->c, instruction->relative); xgpu_text_append(&text, ";\n");
		}
		if (depth_rows && index == depth_z)
			xgpu_text_append(&text, "\tdepth_point = A;\n\tdepth_z_row = B;\n");
		if (depth_rows && index == depth_w)
			xgpu_text_append(&text, "\tdepth_w_row = B;\n");

		if (usage.mac[index])
		{
			switch (instruction->mac)
			{
			case XGPU_MAC_MOV: xgpu_text_append(&text, "\tmac = A;\n"); break;
			case XGPU_MAC_MUL: xgpu_text_append(&text, "\tmac = A * B;\n"); break;
			case XGPU_MAC_ADD: xgpu_text_append(&text, "\tmac = A + C;\n"); break;
			case XGPU_MAC_MAD: xgpu_text_append(&text, "\tmac = A * B + C;\n"); break;
			case XGPU_MAC_DP3: xgpu_text_append(&text, "\tmac = float4(dot(A.xyz, B.xyz));\n"); break;
			case XGPU_MAC_DPH: xgpu_text_append(&text, "\tmac = float4(dot(A.xyz, B.xyz) + B.w);\n"); break;
			case XGPU_MAC_DP4: xgpu_text_append(&text, "\tmac = float4(dot(A, B));\n"); break;
			case XGPU_MAC_DST: xgpu_text_append(&text, "\tmac = float4(1.0, A.y * B.y, A.z, B.w);\n"); break;
			case XGPU_MAC_MIN: xgpu_text_append(&text, "\tmac = min(A, B);\n"); break;
			case XGPU_MAC_MAX: xgpu_text_append(&text, "\tmac = max(A, B);\n"); break;
			case XGPU_MAC_SLT: xgpu_text_append(&text, "\tmac = float4(A < B);\n"); break;
			case XGPU_MAC_SGE: xgpu_text_append(&text, "\tmac = float4(A >= B);\n"); break;
			case XGPU_MAC_ARL: xgpu_text_append(&text, "\tmac = A;\n"); break;
			default: xgpu_text_append(&text, "\tmac = float4(0.0);\n"); break;
			}
		}
		if (usage.ilu[index])
		{
			switch (instruction->ilu)
			{
			case XGPU_ILU_MOV: xgpu_text_append(&text, "\tilu = C;\n"); break;
			case XGPU_ILU_RCP: xgpu_text_append(&text, "\tilu = float4(1.0 / C.x);\n"); break;
			case XGPU_ILU_RCC: xgpu_text_append(&text, "\tilu = nv2a_rcc(C.x);\n"); break;
			case XGPU_ILU_RSQ: xgpu_text_append(&text, "\tilu = float4(rsqrt(abs(C.x)));\n"); break;
			case XGPU_ILU_EXP: xgpu_text_append(&text, "\tilu = nv2a_exp(C.x);\n"); break;
			case XGPU_ILU_LOG: xgpu_text_append(&text, "\tilu = nv2a_log(C.x);\n"); break;
			case XGPU_ILU_LIT: xgpu_text_append(&text, "\tilu = nv2a_lit(C);\n"); break;
			default: xgpu_text_append(&text, "\tilu = float4(0.0);\n"); break;
			}
		}
		/* the screen-space conversion takes the reciprocal of the clip-space
		position's w (rcc of r12.w); keep the position it converts */
		if (instruction->captures_clip)
			xgpu_text_append(&text, "\tclip_position = oPos;\n\tclip_captured = true;\n");

		/* results are written only after both units have read their inputs */
		if (usage.mac[index] && instruction->mac == XGPU_MAC_ARL && usage.a0)
		{
			xgpu_text_append(&text, "\ta0 = int(floor(mac.x + 0.001));\n");
		}
		else if (usage.mac[index] && instruction->mac != XGPU_MAC_ARL && instruction->mac_mask && temporary_read(&usage, instruction->mac_temporary))
		{
			write_mask(instruction->mac_mask, mask);
			if (instruction->mac_temporary == XGPU_VERTEX_POSITION)
				xgpu_text_append(&text, "\toPos.%s = mac.%s;\n", mask, mask);
			else
				xgpu_text_append(&text, "\tr%lu.%s = mac.%s;\n", (unsigned long)instruction->mac_temporary, mask, mask);
		}
		if (usage.ilu[index] && instruction->ilu_mask && temporary_read(&usage, instruction->ilu_temporary))
		{
			write_mask(instruction->ilu_mask, mask);
			if (instruction->ilu_temporary == XGPU_VERTEX_POSITION)
				xgpu_text_append(&text, "\toPos.%s = ilu.%s;\n", mask, mask);
			else
				xgpu_text_append(&text, "\tr%lu.%s = ilu.%s;\n", (unsigned long)instruction->ilu_temporary, mask, mask);
		}
		if (instruction->writes_output && output)
		{
			write_mask(instruction->output_mask, mask);
			xgpu_text_append(&text, "\t%s.%s = %s.%s;\n", output, mask, instruction->output_from_ilu ? "ilu" : "mac", mask);
		}
	}

	/* the clip position, as the SPIR-V's (xgpu_spirv.c: the conversion done
	with c[-38] and c[-37] undone, pixel centres at half-integers, a w of
	zero behind the camera); Metal's clip space is Direct3D's, as the
	game's: nothing to flip, depth 0 to 1 (and then reversed) */
	xgpu_text_append(&text,
		"\t/* undo the screen-space conversion done with c[-38] and c[-37] */\n"
		"\tfloat3 scale = float3(u.viewport_scale.x != 0.0 ? u.viewport_scale.x : 1.0,\n"
		"\t\tu.viewport_scale.y != 0.0 ? u.viewport_scale.y : 1.0,\n"
		"\t\tu.viewport_scale.z != 0.0 ? u.viewport_scale.z : 1.0);\n"
		"\tfloat4 position;\n"
		"\tif (clip_captured)\n"
		"\t\tposition = float4((clip_position.xyz * c[%d].xyz + (c[%d].xyz + float3(0.5 + u.screen_offset, 0.5, 0.0)\n"
		"\t\t\t- u.viewport_offset.xyz) * clip_position.w) / scale, clip_position.w);\n"
		"\telse\n"
		"\t{\n"
		"\t\tfloat3 ndc = (float3(oPos.xy + float2(0.5 + u.screen_offset, 0.5), oPos.z) - u.viewport_offset.xyz) / scale;\n"
		"\t\tposition = float4(ndc * oPos.w, oPos.w);\n"
		"\t}\n",
		XGPU_VERTEX_CONSTANT_BIAS - 38, XGPU_VERTEX_CONSTANT_BIAS - 37);
	/* the depth reversed (xgpu_backend.h, "Reversed depth"): w - z, as one
	dot product where the program's z and w are the projection's rows',
	which keeps every bit of the depth the world's coordinates would round
	away; the draw's constant depth bias toward the camera */
	xgpu_text_append(&text, "\tposition.z = position.w - position.z;\n");
	if (depth_rows)
	{
		xgpu_text_append(&text,
			"\tif (clip_captured && c[%d].z == scale.z && c[%d].z == u.viewport_offset.z)\n"
			"\t{\n"
			"\t\tfloat4 depth_row = depth_w_row - depth_z_row;\n"
			"\t\tposition.z = %s;\n"
			"\t}\n",
			XGPU_VERTEX_CONSTANT_BIAS - 38, XGPU_VERTEX_CONSTANT_BIAS - 37,
			program->instructions[depth_z].mac == XGPU_MAC_DPH ? "dot(depth_point.xyz, depth_row.xyz) + depth_row.w" :
			"dot(depth_point, depth_row)");
	}
	xgpu_text_append(&text,
		"\tposition.z += u.depth_bias * position.w;\n"
		"\tif (!(abs(position.w) > 0.0))\n"
		"\t\tposition = float4(0.0, 0.0, 0.0, -1.0);\n"
		"\txgpu_vertex_out output;\n"
		"\toutput.position = position;\n"
		"\toutput.point_size = oPts.x;\n"
		"\toutput.xD0 = saturate(nv2a_finite(oD0));\n"
		"\toutput.xD1 = saturate(nv2a_finite(oD1));\n"
		"\toutput.xT0 = nv2a_finite(oT0);\n"
		"\toutput.xT1 = nv2a_finite(oT1);\n"
		"\toutput.xT2 = nv2a_finite(oT2);\n"
		"\toutput.xT3 = nv2a_finite(oT3);\n"
		"\toutput.xFog = nv2a_finite(oFog).x;\n");
	if (lighting)
		xgpu_text_append(&text, "\toutput.xWorldNormal = xWorldNormal;\n");
	if (lighting_position(lighting))
		xgpu_text_append(&text, "\toutput.xWorldPosition = xWorldPosition;\n");
	if (environment)
		xgpu_text_append(&text, "\toutput.xWorldBinormal = xWorldBinormal;\n\toutput.xWorldTangent = xWorldTangent;\n");
	if (lighting && lighting->split && !environment)
	{
		int light;

		/* (the model lighting registers, c[-82] and c[-79] to c[-69]: the
		pixel shader's model_lighting, at the vertex) */
		xgpu_text_append(&text,
			"\t{\n"
			"\t\tfloat3 n = xWorldNormal.xyz;\n"
			"\t\tfloat facing = dot(n, -c[%d].xyz);\n"
			"\t\toutput.xLightAmbient = c[%d].xyz + max(max(facing, -facing * c[%d].z), 0.0) * c[%d].xyz +\n"
			"\t\t\tmax(dot(n, -c[%d].xyz), 0.0) * c[%d].xyz;\n"
			"\t\toutput.xLightPoint = float3(0.0);\n",
			XGPU_VERTEX_CONSTANT_BIAS - 73, XGPU_VERTEX_CONSTANT_BIAS - 69, XGPU_VERTEX_CONSTANT_BIAS - 82,
			XGPU_VERTEX_CONSTANT_BIAS - 72, XGPU_VERTEX_CONSTANT_BIAS - 71, XGPU_VERTEX_CONSTANT_BIAS - 70);
		for (light = 0; lighting->lights == 2 && light < 2; light++)
		{
			int first = XGPU_VERTEX_CONSTANT_BIAS - 79 + light * 3;

			xgpu_text_append(&text,
				"\t\t{\n"
				"\t\t\tfloat3 to_light = c[%d].xyz - xWorldPosition;\n"
				"\t\t\tfloat distance_squared = dot(to_light, to_light);\n"
				"\t\t\tfloat3 l = to_light * rsqrt(max(distance_squared, 1.0e-12));\n"
				"\t\t\toutput.xLightPoint += max(1.0 - distance_squared * c[%d].w, 0.0) * saturate(dot(l, n)) *\n"
				"\t\t\t\tsaturate(dot(l, -c[%d].xyz) * c[%d].w + c[%d].w) * c[%d].xyz;\n"
				"\t\t}\n",
				first, first, first + 1, first + 1, first + 2, first + 2);
		}
		xgpu_text_append(&text, "\t}\n");
	}
	xgpu_text_append(&text, "\treturn output;\n}\n");
	functions_insert(&text, functions_at, vertex_functions);
	return text.buffer;
}

/* ---------- pixel programs: what a program reads */

/* the registers read (bit n XGPU_REGISTER_n), and each stage's dot product */
struct pixel_usage
{
	unsigned long registers;
	unsigned char dots[4];
	/* the final combiner's sum and product */
	BOOL v1r0_sum, ef_product;
};

static BOOL register_read(const struct pixel_usage *usage, unsigned long reg)
{
	return (usage->registers & (1UL << reg)) != 0;
}

/* a stage that samples its texture */
static BOOL stage_samples(unsigned char mode)
{
	switch (mode)
	{
	case XGPU_TEXTURE_PROJECT2D:
	case XGPU_TEXTURE_PROJECT3D:
	case XGPU_TEXTURE_CUBEMAP:
	case XGPU_TEXTURE_BUMPENVMAP:
	case XGPU_TEXTURE_BUMPENVMAP_LUMINANCE:
	case XGPU_TEXTURE_DOT_ST:
	case XGPU_TEXTURE_DOT_REFLECT_DIFFUSE:
	case XGPU_TEXTURE_DOT_REFLECT_SPECULAR:
	case XGPU_TEXTURE_DOT_REFLECT_SPECULAR_CONSTANT:
	case XGPU_TEXTURE_DOT_STR_3D:
	case XGPU_TEXTURE_DOT_STR_CUBE:
	case XGPU_TEXTURE_DEPENDENT_AR:
	case XGPU_TEXTURE_DEPENDENT_GB:
		return TRUE;
	default:
		return FALSE;
	}
}

/* a dot product stage's own product (dot<stage>) */
static BOOL stage_dot(unsigned char mode)
{
	switch (mode)
	{
	case XGPU_TEXTURE_DOT_PRODUCT:
	case XGPU_TEXTURE_DOT_ST:
	case XGPU_TEXTURE_DOT_ZW:
	case XGPU_TEXTURE_DOT_REFLECT_DIFFUSE:
	case XGPU_TEXTURE_DOT_REFLECT_SPECULAR:
	case XGPU_TEXTURE_DOT_REFLECT_SPECULAR_CONSTANT:
	case XGPU_TEXTURE_DOT_STR_3D:
	case XGPU_TEXTURE_DOT_STR_CUBE:
		return TRUE;
	default:
		return FALSE;
	}
}

static void input_usage(struct pixel_usage *usage, const struct xgpu_combiner_input *input, BOOL final)
{
	if (input->reg == XGPU_REGISTER_V1R0_SUM && final)
		usage->v1r0_sum = TRUE;
	else if (input->reg == XGPU_REGISTER_EF_PRODUCT && final)
		usage->ef_product = TRUE;
	else if (input->reg < XGPU_REGISTER_V1R0_SUM)
		usage->registers |= 1UL << input->reg;
}

static void pixel_usage(const struct xgpu_pixel_program *program, struct pixel_usage *usage)
{
	const struct xgpu_final_combiner *final = &program->final;
	int stage, portion, input;

	memset(usage, 0, sizeof(*usage));
	for (stage = 0; stage < (int)program->combiner_count; stage++)
	{
		for (portion = 0; portion < 2; portion++)
		{
			const struct xgpu_combiner_portion *part = &program->combiners[stage].portions[portion];

			for (input = 0; input < 4; input++)
				input_usage(usage, &part->inputs[input], FALSE);
			/* (a mux reads r0's alpha) */
			if (part->mux)
				usage->registers |= 1UL << XGPU_REGISTER_R0;
		}
	}
	if (final->present)
	{
		for (input = 0; input < 7; input++)
		{
			if (input != 4 && input != 5)
				input_usage(usage, &final->inputs[input], TRUE);
		}
		/* E and F make the product, which is read as a register */
		if (usage->ef_product)
		{
			input_usage(usage, &final->inputs[4], TRUE);
			input_usage(usage, &final->inputs[5], TRUE);
		}
		if (usage->v1r0_sum)
			usage->registers |= (1UL << XGPU_REGISTER_V1) | (1UL << XGPU_REGISTER_R0);
	}
	else
	{
		usage->registers |= 1UL << XGPU_REGISTER_R0;
	}
	/* r0 starts as t0's alpha */
	if (register_read(usage, XGPU_REGISTER_R0))
		usage->registers |= 1UL << XGPU_REGISTER_T0;
	/* the features that read texture 0 after the stages */
	if (program->coverage_alpha || program->dynamic_lights == XGPU_DYNAMIC_LIGHTS_ENVIRONMENT)
		usage->registers |= 1UL << XGPU_REGISTER_T0;
	/* a z-sprite's depth (xgpu_shader.h) reads its stage's dot product and
	the one before */
	if (xgpu_pixel_program_depth_stage(program))
	{
		int depth_stage = xgpu_pixel_program_depth_stage(program);

		usage->dots[depth_stage] = usage->dots[depth_stage - 1] = 1;
	}
	/* the stages read earlier stages' texels and dot products; a stage that
	discards by its texel reads it */
	for (stage = 3; stage >= 0; stage--)
	{
		const struct xgpu_texture_stage *decoded = &program->stages[stage];
		BOOL read = register_read(usage, XGPU_REGISTER_T0 + stage) || (decoded->alpha_kill && decoded->mode != XGPU_TEXTURE_NONE);

		if (read)
			usage->registers |= 1UL << (XGPU_REGISTER_T0 + stage);
		switch (decoded->mode)
		{
		case XGPU_TEXTURE_BUMPENVMAP:
		case XGPU_TEXTURE_BUMPENVMAP_LUMINANCE:
			if (read && decoded->previous >= 0)
				usage->registers |= 1UL << (XGPU_REGISTER_T0 + decoded->previous);
			break;
		case XGPU_TEXTURE_DEPENDENT_AR:
		case XGPU_TEXTURE_DEPENDENT_GB:
			if (read)
				usage->registers |= 1UL << (XGPU_REGISTER_T0 + decoded->input);
			break;
		case XGPU_TEXTURE_DOT_ST:
			if (read && stage > 0)
				usage->dots[stage] = usage->dots[stage - 1] = 1;
			break;
		case XGPU_TEXTURE_DOT_REFLECT_DIFFUSE:
		case XGPU_TEXTURE_DOT_REFLECT_SPECULAR:
		case XGPU_TEXTURE_DOT_REFLECT_SPECULAR_CONSTANT:
		case XGPU_TEXTURE_DOT_STR_3D:
		case XGPU_TEXTURE_DOT_STR_CUBE:
			if (read)
				usage->dots[1] = usage->dots[2] = usage->dots[3] = 1;
			break;
		default:
			break;
		}
		/* (a diffuse reflection takes stage 3's dot product itself) */
		if (read && decoded->mode == XGPU_TEXTURE_DOT_REFLECT_DIFFUSE)
			usage->registers |= 1UL << (XGPU_REGISTER_T0 + program->stages[3].input);
		/* a stage's dot product, read by it or a later stage, reads its
		input's texel */
		if (usage->dots[stage] && stage_dot(decoded->mode))
			usage->registers |= 1UL << (XGPU_REGISTER_T0 + decoded->input);
	}
}

/* ---------- pixel programs: combiner inputs */

/* (zeroed: the stage's constants read as 0, display.hdr's self-illuminated
surface without its self-illumination: XGPU_HDR_SELF_ILLUMINATION) */
static const char *register_expression(unsigned long reg, int stage, const struct xgpu_combiner_stage *combiner,
	BOOL zeroed)
{
	static char buffer[4][32];
	static int next = 0;
	char *result = buffer[next++ & 3];

	if (zeroed && stage >= 0 && (reg == XGPU_REGISTER_C0 || reg == XGPU_REGISTER_C1))
		return "float4(0.0)";
	switch (reg)
	{
	case XGPU_REGISTER_C0:
		if (stage < 0)
			return "u.ps_final_c0";
		snprintf(result, sizeof(buffer[0]), "u.ps_c0[%d]", combiner->c0);
		return result;
	case XGPU_REGISTER_C1:
		if (stage < 0)
			return "u.ps_final_c1";
		snprintf(result, sizeof(buffer[0]), "u.ps_c1[%d]", combiner->c1);
		return result;
	case XGPU_REGISTER_FOG: return "fog";
	case XGPU_REGISTER_V0: return "v0";
	case XGPU_REGISTER_V1: return "v1";
	case XGPU_REGISTER_T0: return "t0";
	case XGPU_REGISTER_T1: return "t1";
	case XGPU_REGISTER_T2: return "t2";
	case XGPU_REGISTER_T3: return "t3";
	case XGPU_REGISTER_R0: return "r0";
	case XGPU_REGISTER_R1: return "r1";
	case XGPU_REGISTER_V1R0_SUM: return stage < 0 ? "v1r0_sum" : "float4(0.0)";
	case XGPU_REGISTER_EF_PRODUCT: return stage < 0 ? "ef_product" : "float4(0.0)";
	default: return "float4(0.0)";
	}
}

/* an input's value: a float3 (rgb) or float (alpha) expression */
static void input_value(char *value, unsigned long size, const char *source, const struct xgpu_combiner_input *input,
	BOOL alpha_portion)
{
	if (alpha_portion)
		snprintf(value, size, "%s.%s", source, input->alpha ? "a" : "b");
	else if (input->alpha)
		snprintf(value, size, "float3(%s.a)", source);
	else
		snprintf(value, size, "%s.rgb", source);
}

/* one combiner input as a float3 (rgb) or float (alpha) expression */
static void combiner_input(struct xgpu_text *text, const struct xgpu_combiner_input *input, BOOL alpha_portion, int stage,
	const struct xgpu_combiner_stage *combiner, BOOL zeroed)
{
	const char *zero = alpha_portion ? "0.0" : "float3(0.0)";
	char value[64];

	input_value(value, sizeof(value), register_expression(input->reg, stage, combiner, zeroed), input, alpha_portion);
	switch (input->mapping)
	{
	case XGPU_MAP_UNSIGNED_IDENTITY: xgpu_text_append(text, "max(%s, %s)", value, zero); break;
	case XGPU_MAP_UNSIGNED_INVERT: xgpu_text_append(text, "(1.0 - saturate(%s))", value); break;
	case XGPU_MAP_EXPAND_NORMAL: xgpu_text_append(text, "(2.0 * max(%s, %s) - 1.0)", value, zero); break;
	case XGPU_MAP_EXPAND_NEGATE: xgpu_text_append(text, "(1.0 - 2.0 * max(%s, %s))", value, zero); break;
	case XGPU_MAP_HALFBIAS_NORMAL: xgpu_text_append(text, "(max(%s, %s) - 0.5)", value, zero); break;
	case XGPU_MAP_HALFBIAS_NEGATE: xgpu_text_append(text, "(0.5 - max(%s, %s))", value, zero); break;
	case XGPU_MAP_SIGNED_IDENTITY: xgpu_text_append(text, "(%s)", value); break;
	default: xgpu_text_append(text, "(-%s)", value); break;
	}
}

/* final combiner inputs only have the unsigned identity and invert mappings
(the identity's colour not clamped above: display.hdr's XGPU_HDR_UNCLAMPED) */
static void final_input(struct xgpu_text *text, const struct xgpu_combiner_input *input, BOOL alpha_portion,
	BOOL unclamped)
{
	char value[64];

	input_value(value, sizeof(value), register_expression(input->reg, -1, NULL, FALSE), input, alpha_portion);
	if (input->mapping == XGPU_MAP_UNSIGNED_INVERT)
		xgpu_text_append(text, "(1.0 - saturate(%s))", value);
	else if (unclamped && !alpha_portion)
		xgpu_text_append(text, "max(%s, float3(0.0))", value);
	else
		xgpu_text_append(text, "saturate(%s)", value);
}

static const char *destination_name(unsigned long reg, const struct pixel_usage *usage)
{
	/* (a register nothing reads is not written) */
	if (!register_read(usage, reg))
		return NULL;
	switch (reg)
	{
	case XGPU_REGISTER_V0: return "v0";
	case XGPU_REGISTER_V1: return "v1";
	case XGPU_REGISTER_T0: return "t0";
	case XGPU_REGISTER_T1: return "t1";
	case XGPU_REGISTER_T2: return "t2";
	case XGPU_REGISTER_T3: return "t3";
	case XGPU_REGISTER_R0: return "r0";
	case XGPU_REGISTER_R1: return "r1";
	default: return NULL;
	}
}

static const char *output_mapping(unsigned char mapping)
{
	switch (mapping)
	{
	case XGPU_OUTPUT_BIAS: return "(%s - 0.5)";
	case XGPU_OUTPUT_SHIFT_LEFT_1: return "(%s * 2.0)";
	case XGPU_OUTPUT_SHIFT_LEFT_1_BIAS: return "((%s - 0.5) * 2.0)";
	case XGPU_OUTPUT_SHIFT_LEFT_2: return "(%s * 4.0)";
	case XGPU_OUTPUT_SHIFT_RIGHT_1: return "(%s * 0.5)";
	default: return "(%s)";
	}
}

/* ---------- one general combiner stage */

static void combiner_stage(struct xgpu_text *text, const struct xgpu_pixel_program *program, int stage,
	const struct pixel_usage *usage, BOOL zeroed)
{
	const struct xgpu_combiner_stage *combiner = &program->combiners[stage];
	int portion;

	xgpu_text_append(text, "\t/* combiner stage %d */\n\t{\n", stage);
	for (portion = 0; portion < 2; portion++)
	{
		const struct xgpu_combiner_portion *part = &combiner->portions[portion];
		BOOL alpha = portion == 1;
		const char *type = alpha ? "float" : "float3";
		const char *prefix = alpha ? "a" : "c";
		/* (display.hdr's XGPU_HDR_UNCLAMPED: the colour not above, a half
		float's largest) */
		const char *bounds = alpha ? "-1.0, 1.0" : XGPU_HDR_IS_UNCLAMPED(program->hdr) ?
			"float3(-1.0), float3(65504.0)" : "float3(-1.0), float3(1.0)";
		const char *mapping = output_mapping(part->output_mapping);
		char mapped[64];

		xgpu_text_append(text, "\t\t%s %sA = ", type, prefix);
		combiner_input(text, &part->inputs[0], alpha, stage, combiner, zeroed);
		xgpu_text_append(text, ";\n\t\t%s %sB = ", type, prefix);
		combiner_input(text, &part->inputs[1], alpha, stage, combiner, zeroed);
		xgpu_text_append(text, ";\n\t\t%s %sC = ", type, prefix);
		combiner_input(text, &part->inputs[2], alpha, stage, combiner, zeroed);
		xgpu_text_append(text, ";\n\t\t%s %sD = ", type, prefix);
		combiner_input(text, &part->inputs[3], alpha, stage, combiner, zeroed);
		xgpu_text_append(text, ";\n");

		if (part->ab_dot)
			xgpu_text_append(text, "\t\tfloat3 cAB = float3(dot(cA, cB));\n");
		else
			xgpu_text_append(text, "\t\t%s %sAB = %sA * %sB;\n", type, prefix, prefix, prefix);
		if (part->cd_dot)
			xgpu_text_append(text, "\t\tfloat3 cCD = float3(dot(cC, cD));\n");
		else
			xgpu_text_append(text, "\t\t%s %sCD = %sC * %sD;\n", type, prefix, prefix, prefix);
		if (part->mux)
		{
			if (program->mux_msb)
				xgpu_text_append(text, "\t\t%s %sSUM = r0.a >= 0.5 ? %sCD : %sAB;\n", type, prefix, prefix, prefix);
			else
				xgpu_text_append(text, "\t\t%s %sSUM = (int(r0.a * 255.0 + 0.5) & 1) != 0 ? %sCD : %sAB;\n",
					type, prefix, prefix, prefix);
		}
		else
		{
			xgpu_text_append(text, "\t\t%s %sSUM = %sAB + %sCD;\n", type, prefix, prefix, prefix);
		}
		snprintf(mapped, sizeof(mapped), mapping, "%sAB");
		xgpu_text_append(text, "\t\t%sAB = clamp(", prefix);
		xgpu_text_append(text, mapped, prefix);
		xgpu_text_append(text, ", %s);\n", bounds);
		snprintf(mapped, sizeof(mapped), mapping, "%sCD");
		xgpu_text_append(text, "\t\t%sCD = clamp(", prefix);
		xgpu_text_append(text, mapped, prefix);
		xgpu_text_append(text, ", %s);\n", bounds);
		snprintf(mapped, sizeof(mapped), mapping, "%sSUM");
		xgpu_text_append(text, "\t\t%sSUM = clamp(", prefix);
		xgpu_text_append(text, mapped, prefix);
		xgpu_text_append(text, ", %s);\n", bounds);
	}

	/* write back only after both portions have read their inputs */
	for (portion = 0; portion < 2; portion++)
	{
		const struct xgpu_combiner_portion *part = &combiner->portions[portion];
		BOOL alpha = portion == 1;
		const char *prefix = alpha ? "a" : "c";
		const char *component = alpha ? "a" : "rgb";
		const char *ab = destination_name(part->ab, usage);
		const char *cd = destination_name(part->cd, usage);
		const char *sum = destination_name(part->sum, usage);

		if (ab)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sAB;\n", ab, component, prefix);
			if (part->ab_blue_to_alpha)
				xgpu_text_append(text, "\t\t%s.a = cAB.b;\n", ab);
		}
		if (cd)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sCD;\n", cd, component, prefix);
			if (part->cd_blue_to_alpha)
				xgpu_text_append(text, "\t\t%s.a = cCD.b;\n", cd);
		}
		if (sum)
			xgpu_text_append(text, "\t\t%s.%s = %sSUM;\n", sum, component, prefix);
	}
	xgpu_text_append(text, "\t}\n");
}

/* ---------- texture stages */

static const char *texture_declaration(unsigned char type)
{
	switch (type)
	{
	case _xgpu_sampler_3d: return "texture3d<float>";
	case _xgpu_sampler_cube: return "texturecube<float>";
	case _xgpu_sampler_2d_layered: return "texture2d_array<float>";
	default: return "texture2d<float>";
	}
}

/* the input texel of a dot product stage, mapped per PSDotMapping */
static void dot_input(struct xgpu_text *text, const struct xgpu_texture_stage *stage)
{
	switch (stage->dot_mapping)
	{
	case XGPU_DOT_ZERO_TO_ONE: xgpu_text_append(text, "t%d.rgb", stage->input); break;
	case XGPU_DOT_MINUS_ONE_TO_ONE_D3D: xgpu_text_append(text, "((t%d.rgb * 255.0 - 128.0) * (1.0 / 127.0))", stage->input); break;
	case XGPU_DOT_MINUS_ONE_TO_ONE: xgpu_text_append(text, "signed_bytes(t%d.rgb)", stage->input); break;
	default: xgpu_text_append(text, "(t%d.rgb * 2.0 - 1.0)", stage->input); break;
	}
}

/* the stage's lookup at coordinates (a float4 expression), its LOD bias the
lookup's (Metal samplers have none); where the shader draws the border,
the texel mixed with the border color by how much of it is inside */
static void sample(struct xgpu_text *text, const struct xgpu_pixel_program *program, int stage, const char *coordinates,
	const struct xgpu_msl_options *options)
{
	unsigned char sampler = program->stages[stage].sampler;
	unsigned int border = options->border[stage] & (sampler == _xgpu_sampler_3d ? 7 : 3);
	char uv[160];

	if ((options->border[stage] & XGPU_BORDER_REGION) && sampler == _xgpu_sampler_2d)
	{
		xgpu_text_append(text, "region_sample(tex%d, tex%d_sampler, (%s).xy, u.texture_scale[%d], u.texture_lod_bias[%d], "
			"u.texture_border[%d], u.texture_border_linear[%d])", stage, stage, coordinates, stage, stage, stage, stage);
		return;
	}
	if (sampler == _xgpu_sampler_3d || sampler == _xgpu_sampler_cube)
		snprintf(uv, sizeof(uv), "(%s).xyz", coordinates);
	else
		snprintf(uv, sizeof(uv), "(%s).xy * u.texture_scale[%d].xy", coordinates, stage);
	/* (the high-res text's distance fields: msdf_coverage) */
	if ((options->border[stage] & XGPU_BORDER_MSDF) && sampler == _xgpu_sampler_2d)
	{
		xgpu_text_append(text, "msdf_coverage(tex%d.sample(tex%d_sampler, %s, bias(u.texture_lod_bias[%d])), %s, "
			"float2(float(tex%d.get_width()), float(tex%d.get_height())))", stage, stage, uv, stage, uv, stage, stage);
		return;
	}
	if (border && sampler != _xgpu_sampler_cube && sampler != _xgpu_sampler_2d_layered)
		xgpu_text_append(text, "border_mix(");
	else
		border = 0;
	/* (the Mac app is flat: a layered target is its first layer) */
	xgpu_text_append(text, "tex%d.sample(tex%d_sampler, %s, %sbias(u.texture_lod_bias[%d]))", stage, stage, uv,
		sampler == _xgpu_sampler_2d_layered ? "0, " : "", stage);
	if (border && sampler == _xgpu_sampler_3d)
	{
		xgpu_text_append(text, ", %s, float3(float(tex%d.get_width()), float(tex%d.get_height()), float(tex%d.get_depth())), "
			"%uu, u.texture_border[%d], u.texture_border_linear[%d])", uv, stage, stage, stage, border, stage, stage);
	}
	else if (border)
	{
		xgpu_text_append(text, ", float3(%s, 0.5), float3(float(tex%d.get_width()), float(tex%d.get_height()), 1.0), "
			"%uu, u.texture_border[%d], u.texture_border_linear[%d])", uv, stage, stage, border, stage, stage);
	}
}

static void texture_stage(struct xgpu_text *text, const struct xgpu_pixel_program *program, int stage,
	const struct pixel_usage *usage, const struct xgpu_msl_options *options)
{
	const struct xgpu_texture_stage *decoded = &program->stages[stage];
	unsigned long mode = decoded->mode;
	/* (a texel nothing reads is not fetched; a dot product nothing reads is
	not taken) */
	BOOL read = register_read(usage, XGPU_REGISTER_T0 + stage);
	BOOL dot = usage->dots[stage] != 0;
	char coordinates[128];

	xgpu_text_append(text, "\t/* texture stage %d, mode %lu */\n", stage, (unsigned long)decoded->declared_mode);
	switch (mode)
	{
	case XGPU_TEXTURE_PROJECT2D:
	case XGPU_TEXTURE_PROJECT3D:
		if (!read)
			break;
		snprintf(coordinates, sizeof(coordinates), "float4(fast::divide(in.xT%d.xyz, float3(in.xT%d.w != 0.0 ? in.xT%d.w : 1.0)), 1.0)",
			stage, stage, stage);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, program, stage, coordinates, options);
		xgpu_text_append(text, ";\n");
		if (stage == 0 && program->point_threshold && decoded->sampler == _xgpu_sampler_2d)
		{
			/* IDs must not be averaged with another segment or corrupted by
			low-alpha artwork. Zero marks the atlas's continuous meters. */
			xgpu_text_append(text,
				"\t{\n\t\tfloat2 uv = (%s).xy * u.texture_scale[0].xy;\n"
				"\t\tint2 size = int2(int(tex0.get_width()), int(tex0.get_height()));\n"
				"\t\tint2 pixel = clamp(int2(floor(uv * float2(size))), int2(0), size - 1);\n"
				"\t\tfloat threshold = tex0.read(uint2(pixel)).r;\n"
				"\t\tif (threshold > 0.0) t0.rb = float2(threshold);\n\t}\n", coordinates);
		}
		break;
	case XGPU_TEXTURE_CUBEMAP:
		if (!read)
			break;
		snprintf(coordinates, sizeof(coordinates), "in.xT%d", stage);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, program, stage, coordinates, options);
		xgpu_text_append(text, ";\n");
		break;
	case XGPU_TEXTURE_PASSTHRU:
		if (read)
			xgpu_text_append(text, "\tt%d = saturate(in.xT%d);\n", stage, stage);
		break;
	case XGPU_TEXTURE_CLIPPLANE:
	{
		static const char components[] = "xyzw";
		int component;

		for (component = 0; component < 4; component++)
		{
			xgpu_text_append(text, "\tif (in.xT%d.%c %s 0.0) discard_fragment();\n", stage, components[component],
				(decoded->compare & (1 << component)) ? ">=" : "<");
		}
		break;
	}
	case XGPU_TEXTURE_BUMPENVMAP:
	case XGPU_TEXTURE_BUMPENVMAP_LUMINANCE:
	{
		int input = decoded->previous;

		if (!read)
			break;
		xgpu_text_append(text, "\t{\n\t\tfloat2 d = signed_bytes(t%d.rgb).rg;\n", input);
		xgpu_text_append(text, "\t\tfloat2 coordinates = in.xT%d.xy + float2(u.bump_matrix[%d].x * d.x + u.bump_matrix[%d].z * d.y,"
			" u.bump_matrix[%d].y * d.x + u.bump_matrix[%d].w * d.y);\n", stage, stage, stage, stage, stage);
		xgpu_text_append(text, "\t\tt%d = ", stage);
		sample(text, program, stage, "float4(coordinates, 0.0, 1.0)", options);
		xgpu_text_append(text, ";\n");
		if (mode == XGPU_TEXTURE_BUMPENVMAP_LUMINANCE)
		{
			xgpu_text_append(text, "\t\tt%d.rgb *= saturate(u.bump_luminance[%d].x * t%d.b + u.bump_luminance[%d].y);\n",
				stage, stage, input, stage);
		}
		xgpu_text_append(text, "\t}\n");
		break;
	}
	case XGPU_TEXTURE_DOT_PRODUCT:
	case XGPU_TEXTURE_DOT_ZW:
		if (dot)
		{
			xgpu_text_append(text, "\tdot%d = dot(in.xT%d.xyz, ", stage, stage);
			dot_input(text, decoded);
			xgpu_text_append(text, ");\n");
		}
		/* the depth it replaces the fragment's with (xgpu_spirv.c) */
		if (mode == XGPU_TEXTURE_DOT_ZW && stage == xgpu_pixel_program_depth_stage(program))
		{
			xgpu_text_append(text, "\treplaced_depth = dot%d > 0.0 ? saturate(dot%d / dot%d / %.1f) : 1.0;\n",
				stage, stage - 1, stage, (double)XGPU_REPLACED_DEPTH_UNITS);
		}
		break;
	case XGPU_TEXTURE_DOT_ST:
		if (dot)
		{
			xgpu_text_append(text, "\tdot%d = dot(in.xT%d.xyz, ", stage, stage);
			dot_input(text, decoded);
			xgpu_text_append(text, ");\n");
		}
		if (!read)
			break;
		xgpu_text_append(text, "\tt%d = ", stage);
		snprintf(coordinates, sizeof(coordinates), "float4(dot%d, dot%d, 0.0, 1.0)", stage - 1, stage);
		sample(text, program, stage, coordinates, options);
		xgpu_text_append(text, ";\n");
		break;
	case XGPU_TEXTURE_DOT_REFLECT_DIFFUSE:
		/* the normal takes its third component from stage 3's dot product */
		if (dot)
		{
			xgpu_text_append(text, "\tdot%d = dot(in.xT%d.xyz, ", stage, stage);
			dot_input(text, decoded);
			xgpu_text_append(text, ");\n");
		}
		if (!read)
			break;
		xgpu_text_append(text, "\tdot3 = dot(in.xT3.xyz, ");
		dot_input(text, &program->stages[3]);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		sample(text, program, stage, "float4(dot1, dot2, dot3, 1.0)", options);
		xgpu_text_append(text, ";\n");
		break;
	case XGPU_TEXTURE_DOT_REFLECT_SPECULAR:
	case XGPU_TEXTURE_DOT_REFLECT_SPECULAR_CONSTANT:
		if (dot)
		{
			xgpu_text_append(text, "\tdot%d = dot(in.xT%d.xyz, ", stage, stage);
			dot_input(text, decoded);
			xgpu_text_append(text, ");\n");
		}
		if (!read)
			break;
		xgpu_text_append(text, "\t{\n\t\tfloat3 n = float3(dot1, dot2, dot3);\n");
		if (mode == XGPU_TEXTURE_DOT_REFLECT_SPECULAR)
			xgpu_text_append(text, "\t\tfloat3 e = float3(in.xT1.w, in.xT2.w, in.xT3.w);\n");
		else
			xgpu_text_append(text, "\t\tfloat3 e = u.ps_c0[0].xyz;\n");
		xgpu_text_append(text, "\t\tfloat3 r = fast::divide(2.0 * n * dot(n, e), float3(max(dot(n, n), 1.0e-20))) - e;\n\t\tt%d = ", stage);
		sample(text, program, stage, "float4(r, 1.0)", options);
		xgpu_text_append(text, ";\n\t}\n");
		break;
	case XGPU_TEXTURE_DOT_STR_3D:
	case XGPU_TEXTURE_DOT_STR_CUBE:
		if (dot)
		{
			xgpu_text_append(text, "\tdot%d = dot(in.xT%d.xyz, ", stage, stage);
			dot_input(text, decoded);
			xgpu_text_append(text, ");\n");
		}
		if (!read)
			break;
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, program, stage, "float4(dot1, dot2, dot3, 1.0)", options);
		xgpu_text_append(text, ";\n");
		break;
	case XGPU_TEXTURE_DEPENDENT_AR:
		if (!read)
			break;
		snprintf(coordinates, sizeof(coordinates), "float4(t%d.a, t%d.r, 0.0, 1.0)", decoded->input, decoded->input);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, program, stage, coordinates, options);
		xgpu_text_append(text, ";\n");
		break;
	case XGPU_TEXTURE_DEPENDENT_GB:
		if (!read)
			break;
		snprintf(coordinates, sizeof(coordinates), "float4(t%d.g, t%d.b, 0.0, 1.0)", decoded->input, decoded->input);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, program, stage, coordinates, options);
		xgpu_text_append(text, ";\n");
		break;
	default:
		break;
	}

	/* channels the application marked signed (D3DTSS_COLORSIGN) */
	if (decoded->color_sign && mode != XGPU_TEXTURE_NONE && read)
	{
		static const char channels[] = "argb";
		int bit;

		for (bit = 0; bit < 4; bit++)
		{
			if (decoded->color_sign & (1 << bit))
				xgpu_text_append(text, "\tt%d.%c = signed_byte(t%d.%c);\n", stage, channels[bit], stage, channels[bit]);
		}
	}
	if (decoded->alpha_kill && mode != XGPU_TEXTURE_NONE)
		xgpu_text_append(text, "\tif (t%d.a == 0.0) discard_fragment();\n", stage);
}

/* ---------- the whole pixel shader */

static const char *comparison_operator(unsigned long function)
{
	switch (function)
	{
	case D3DCMP_NEVER: return NULL;
	case D3DCMP_LESS: return "<";
	case D3DCMP_EQUAL: return "==";
	case D3DCMP_LESSEQUAL: return "<=";
	case D3DCMP_GREATER: return ">";
	case D3DCMP_NOTEQUAL: return "!=";
	case D3DCMP_GREATEREQUAL: return ">=";
	default: return "";
	}
}

/* per-pixel model lighting (xgpu_spirv.c model_lighting) */
static void model_lighting(struct xgpu_text *text, BOOL point_lights, BOOL dynamic_lights, BOOL occlusion)
{
	int light;

	point_lights = point_lights && !dynamic_lights;
	xgpu_text_append(text,
		"static inline float3 model_lighting(constant xgpu_uniforms &u, float4 xWorldNormal%s%s)\n"
		"{\n"
		"\tfloat3 n = xWorldNormal.xyz * (xWorldNormal.w * rsqrt(max(dot(xWorldNormal.xyz, xWorldNormal.xyz), 1.0e-24)));\n"
		"\tfloat facing = dot(n, -u.model_lights[7].xyz);\n"
		"\tfloat3 light = u.model_lights[11].xyz + max(max(facing, -facing * u.model_lights[0].z), 0.0) * u.model_lights[8].xyz +\n"
		"\t\tmax(dot(n, -u.model_lights[9].xyz), 0.0) * u.model_lights[10].xyz;\n",
		point_lights ? ", float3 xWorldPosition" : "", occlusion ? ", float occlusion" : "");
	/* (display.ambient_occlusion: the ambient and distant lights alone by
	the occlusion, not the point lights: xgpu.h) */
	if (occlusion)
		xgpu_text_append(text, "\tlight *= occlusion;\n");
	for (light = 0; point_lights && light < 2; light++)
	{
		int first = 1 + light * 3;

		xgpu_text_append(text,
			"\t{\n"
			"\t\tfloat3 to_light = u.model_lights[%d].xyz - xWorldPosition;\n"
			"\t\tfloat distance_squared = dot(to_light, to_light);\n"
			"\t\tfloat3 l = to_light * rsqrt(max(distance_squared, 1.0e-12));\n"
			"\t\tlight += max(1.0 - distance_squared * u.model_lights[%d].w, 0.0) * saturate(dot(l, n)) *\n"
			"\t\t\tsaturate(dot(l, -u.model_lights[%d].xyz) * u.model_lights[%d].w + u.model_lights[%d].w) *\n"
			"\t\t\tu.model_lights[%d].xyz;\n"
			"\t}\n",
			first, first, first + 1, first + 1, first + 2, first + 2);
	}
	xgpu_text_append(text, "\treturn saturate(light);\n}\n");
}

/* the enhanced dynamic lights (xgpu.h): the block (struct
xgpu_dynamic_lights_block, 6 KB: a buffer, past setFragmentBytes' 4) and
the cells (a buffer of four words a cell, which Vulkan's integer 3D
texture holds, in its order) */
static void dynamic_lights_functions(struct xgpu_text *text, struct xgpu_text *functions)
{
	xgpu_text_append(text,
		"struct xgpu_dynamic_lights\n"
		"{\n"
		"\tfloat4 grid[3];\n"
		"\tfloat4 position[%d];\n"
		"\tfloat4 color[%d];\n"
		"\tfloat4 cone[%d];\n"
		"};\n", XGPU_DYNAMIC_LIGHTS_MAXIMUM, XGPU_DYNAMIC_LIGHTS_MAXIMUM, XGPU_DYNAMIC_LIGHTS_MAXIMUM);
	/* (the lights that reach a point's cluster: its cluster's mask, found down
	the world's tree; every light where the world has none, or puts the point
	in no cluster: xgpu.h, xgpu_dynamic_lights_world.h) */
	xgpu_text_append(functions,
		"static inline uint4 dynamic_light_cluster_mask(const device uint4 *cells, const device uint4 *world, float3 q)\n"
		"{\n"
		"\tuint node = world[0].y;\n"
		"\tfor (int depth = 0; depth < %d; depth++)\n"
		"\t{\n"
		"\t\tuint at = 1u + 2u * min(node, %du);\n"
		"\t\tfloat4 plane = as_type<float4>(world[at]);\n"
		"\t\tuint4 children = world[at + 1u];\n"
		"\t\tuint child = dot(plane.xyz, q) - plane.w >= 0.0 ? children.y : children.x;\n"
		"\t\tif ((child & 0x80000000u) != 0u)\n"
		"\t\t\treturn child == 0xffffffffu ? uint4(0xffffffffu) : cells[%du + (child & %du)];\n"
		"\t\tnode = child;\n"
		"\t}\n"
		"\treturn uint4(0xffffffffu);\n"
		"}\n", XGPU_DYNAMIC_LIGHTS_WORLD_DEPTH, (XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS - 3) / 2,
		XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * XGPU_DYNAMIC_LIGHTS_GRID_Z,
		XGPU_DYNAMIC_LIGHTS_CLUSTERS - 1);
	xgpu_text_append(functions,
		"static inline float3 dynamic_light_sum(constant xgpu_dynamic_lights &lights, const device uint4 *cells,\n"
		"\tconst device uint4 *world, float3 p, float3 n, float3 surface_normal, bool environment, float4 own)\n"
		"{\n"
		"\tfloat3 offset = p - lights.grid[0].xyz;\n"
		"\tfloat3 cell = sign(offset) * log2(1.0 + abs(offset) * lights.grid[1].w) * lights.grid[1].xyz +\n"
		"\t\tfloat3(%d.0, %d.0, %d.0);\n"
		"\tint3 at = clamp(int3(floor(cell)), int3(0), int3(%d, %d, %d));\n"
		"\tuint4 mask = cells[(at.z * %d + at.y) * %d + at.x];\n"
		"\tif (any((mask & cells[environment ? %d : %d]) != 0u) && world[0].x != 0u)\n"
		"\t\tmask &= dynamic_light_cluster_mask(cells, world, p + surface_normal * %.4f);\n"
		"\tfloat3 sum = float3(0.0);\n"
		"\tfor (int word = 0; word < 4; word++)\n"
		"\t{\n"
		"\t\tuint bits = mask[word];\n"
		"\t\twhile (bits != 0u)\n"
		"\t\t{\n"
		"\t\t\tint light = word * 32 + int(ctz(bits));\n"
		"\t\t\tbits &= bits - 1u;\n"
		"\t\t\tfloat4 position = lights.position[light];\n"
		"\t\t\tif ((environment && position.w < 0.0) || any(own == float4(float(light))))\n"
		"\t\t\t\tcontinue;\n"
		"\t\t\tfloat3 to_light = position.xyz - p;\n"
		"\t\t\tfloat distance_squared = dot(to_light, to_light);\n"
		"\t\t\tfloat x2 = distance_squared * position.w * position.w;\n"
		"\t\t\tif (x2 >= 1.0)\n"
		"\t\t\t\tcontinue;\n"
		"\t\t\tfloat3 l = to_light * rsqrt(max(distance_squared, 1.0e-8));\n"
		"\t\t\tfloat window = 1.0 - x2 * x2;\n"
		"\t\t\tfloat core = max(lights.grid[2].y * position.w * position.w, %.4f);\n"
		"\t\t\tfloat falloff = window * window / (1.0 + distance_squared * core);\n"
		"\t\t\tfloat facing = environment ?\n"
		"\t\t\t\tmax(dot(n, l), 0.0) * saturate(dot(surface_normal, l) * 4.0 + 0.5) :\n"
		"\t\t\t\tsaturate((dot(n, l) + lights.grid[2].w) / (1.0 + lights.grid[2].w));\n"
		"\t\t\tfloat4 cone = lights.cone[light];\n"
		"\t\t\tfloat4 color = lights.color[light];\n"
		"\t\t\tfloat spot = saturate(dot(l, -cone.xyz) * cone.w + color.w);\n"
		"\t\t\tsum += color.rgb * (falloff * facing * spot);\n"
		"\t\t}\n"
		"\t}\n"
		"\treturn sum;\n"
		"}\n"
		"static inline float3 dynamic_light_add(constant xgpu_dynamic_lights &lights, float3 base, float3 light)\n"
		"{\n"
		"\tif (all(light == 0.0))\n"
		"\t\treturn saturate(base);\n"
		"\tlight += float3(max(max(light.r, max(light.g, light.b)) - 1.0, 0.0) * lights.grid[2].z);\n"
		"\tfloat3 had = pow(saturate(base), float3(2.2));\n"
		"\tfloat3 room = max(1.0 - had, float3(1.0e-3));\n"
		"\treturn pow(had + room * (1.0 - exp(-light / room)), float3(1.0 / 2.2));\n"
		"}\n"
		/* (display.hdr: the same below white, and what its shoulder squeezes
		under white rolled off toward the lights' white instead: xgpu_hdr.h's
		xgpu_hdr_lights) */
		"static inline float3 dynamic_light_add_hdr(constant xgpu_dynamic_lights &lights, float3 base, float3 light)\n"
		"{\n"
		"\tif (all(light == 0.0))\n"
		"\t\treturn saturate(base);\n"
		"\tlight += float3(max(max(light.r, max(light.g, light.b)) - 1.0, 0.0) * lights.grid[2].z);\n"
		"\tfloat3 had = pow(saturate(base), float3(2.2));\n"
		"\tfloat3 room = max(1.0 - had, float3(1.0e-3));\n"
		"\tfloat3 past = max(had + light - 1.0, float3(0.0));\n"
		"\treturn pow(had + room * (1.0 - exp(-light / room)) + " XGPU_HDR_LIGHTS_ROOM " * (1.0 - exp(-past / " XGPU_HDR_LIGHTS_ROOM ")),\n"
		"\t\tfloat3(1.0 / 2.2));\n"
		"}\n",
		XGPU_DYNAMIC_LIGHTS_GRID_X / 2, XGPU_DYNAMIC_LIGHTS_GRID_Y / 2, XGPU_DYNAMIC_LIGHTS_GRID_Z / 2,
		XGPU_DYNAMIC_LIGHTS_GRID_X - 1, XGPU_DYNAMIC_LIGHTS_GRID_Y - 1, XGPU_DYNAMIC_LIGHTS_GRID_Z - 1,
		XGPU_DYNAMIC_LIGHTS_GRID_Y, XGPU_DYNAMIC_LIGHTS_GRID_X,
		XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * XGPU_DYNAMIC_LIGHTS_GRID_Z + XGPU_DYNAMIC_LIGHTS_WALK + 1,
		XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * XGPU_DYNAMIC_LIGHTS_GRID_Z + XGPU_DYNAMIC_LIGHTS_WALK,
		(double)XGPU_DYNAMIC_LIGHTS_CLUSTER_OFFSET,
		1.0 / (XGPU_DYNAMIC_LIGHTS_CORE * XGPU_DYNAMIC_LIGHTS_CORE));
}

static const char pixel_functions[] =
	"static inline float4 nv2a_finite(float4 v)\n"
	"{\n"
	"\treturn select(v, float4(0.0), (as_type<uint4>(v) & 0x7f800000u) == 0x7f800000u);\n"
	"}\n"
	"static inline float signed_byte(float x)\n"
	"{\n"
	"\tfloat b = floor(x * 255.0 + 0.5);\n"
	"\treturn (b >= 128.0 ? b - 256.0 : b) * (1.0 / 127.0);\n"
	"}\n"
	"static inline float3 signed_bytes(float3 x)\n"
	"{\n"
	"\treturn float3(signed_byte(x.r), signed_byte(x.g), signed_byte(x.b));\n"
	"}\n";

/* a stage's border color (xgpu_msl.h): the texel,
sampled clamped to the edge, mixed with the color by how much of the
lookup's footprint is inside the texture along each axis the border is on:
with the nearest texel, all or none; filtered linearly, the half texel
either side of the edge shares it with the border (the texture's first
level's texels: a smaller level's are wider) */
static const char border_function[] =
	"static inline float4 border_mix(float4 texel, float3 coordinates, float3 size, uint axes, float4 color, float linear)\n"
	"{\n"
	"\tfloat inside = 1.0;\n"
	"\tfor (uint axis = 0; axis < 3; axis++)\n"
	"\t{\n"
	"\t\tif ((axes & (1u << axis)) == 0u)\n"
	"\t\t\tcontinue;\n"
	"\t\tfloat x = coordinates[axis];\n"
	"\t\tfloat nearest = (x >= 0.0 && x < 1.0) ? 1.0 : 0.0;\n"
	"\t\tfloat filtered = saturate(x * size[axis] + 0.5) * saturate((1.0 - x) * size[axis] + 0.5);\n"
	"\t\tinside *= linear != 0.0 ? filtered : nearest;\n"
	"\t}\n"
	"\treturn mix(color, texel, float4(inside));\n"
	"}\n"
	/* a region of the texture (XGPU_BORDER_REGION: region its size, then
	its origin) sampled as a texture of the region's own whose border is
	beyond it: clamped to the region's edge texels, and the border mixed in
	by how much of the lookup is inside the region */
	"static inline float4 region_sample(texture2d<float> t, sampler s, float2 r, float4 region, float lod_bias, float4 color,\n"
	"\tfloat linear)\n"
	"{\n"
	"\tfloat2 size = float2(float(t.get_width()), float(t.get_height())) * region.xy;\n"
	"\tfloat2 half_texel = 0.5 / size;\n"
	"\tfloat4 texel = t.sample(s, region.zw + clamp(r, half_texel, 1.0 - half_texel) * region.xy, bias(lod_bias));\n"
	"\treturn border_mix(texel, float3(r, 0.5), float3(size, 1.0), 3u, color, linear);\n"
	"}\n"
	/* the high-res text's distance fields (XGPU_BORDER_MSDF): white, its
	alpha the coverage of the edge where the median of the channels is 0.5,
	over as many of the screen's pixels as XGPU_MSDF_RANGE texels are (at
	least one) */
	"static inline float4 msdf_coverage(float4 texel, float2 uv, float2 size)\n"
	"{\n"
	"\tfloat distance = max(min(texel.r, texel.g), min(max(texel.r, texel.g), texel.b));\n"
	"\tfloat2 texels = fwidth(uv * size);\n"
	"\tfloat pixels = max(" XGPU_MSDF_RANGE_TEXT " * 0.5 * (1.0 / max(texels.x, 1e-6) + 1.0 / max(texels.y, 1e-6)), 1.0);\n"
	"\treturn float4(1.0, 1.0, 1.0, saturate((distance - 0.5) * pixels + 0.5));\n"
	"}\n";

/* ---------- display.hdr's tags (xgpu_hdr.h) */

/* a blend factor, of the source (s), the destination (d: the pixel drawn
over, which may be past white: one less it is never below 0) and the blend
colour (c), for every channel at once (a colour's factor acts on alpha as
its alpha's) */
static const char *hdr_factor(int factor)
{
	switch (factor)
	{
	case XGPU_BLEND_ZERO: return "float4(0.0)";
	case XGPU_BLEND_ONE: return "float4(1.0)";
	case XGPU_BLEND_SOURCE_COLOR: return "s";
	case XGPU_BLEND_ONE_MINUS_SOURCE_COLOR: return "(1.0 - s)";
	case XGPU_BLEND_SOURCE_ALPHA: return "float4(s.a)";
	case XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA: return "float4(1.0 - s.a)";
	case XGPU_BLEND_DESTINATION_ALPHA: return "float4(d.a)";
	case XGPU_BLEND_ONE_MINUS_DESTINATION_ALPHA: return "float4(max(1.0 - d.a, 0.0))";
	case XGPU_BLEND_DESTINATION_COLOR: return "d";
	case XGPU_BLEND_ONE_MINUS_DESTINATION_COLOR: return "max(1.0 - d, float4(0.0))";
	case XGPU_BLEND_SOURCE_ALPHA_SATURATE: return "float4(float3(min(s.a, max(1.0 - d.a, 0.0))), 1.0)";
	case XGPU_BLEND_CONSTANT_COLOR: return "u.blend_constant";
	case XGPU_BLEND_ONE_MINUS_CONSTANT_COLOR: return "(1.0 - u.blend_constant)";
	case XGPU_BLEND_CONSTANT_ALPHA: return "float4(u.blend_constant.a)";
	case XGPU_BLEND_ONE_MINUS_CONSTANT_ALPHA: return "float4(1.0 - u.blend_constant.a)";
	default: return "float4(1.0)";
	}
}

static float hdr_ceiling(int ceiling)
{
	return ceiling == XGPU_HDR_CEILING_WEAPON ? XGPU_HDR_CEILING_WEAPON_VALUE :
		ceiling == XGPU_HDR_CEILING_HUD ? XGPU_HDR_CEILING_HUD_VALUE :
		ceiling == XGPU_HDR_CEILING_LIGHT ? XGPU_HDR_CEILING_LIGHT_VALUE : XGPU_HDR_CEILING_PAPER_WHITE_VALUE;
}

/* the colour written for a tagged draw, from its source (source: the
pixel shader's result as the game's blend takes it): a glow brightened
toward its ceiling; blended here with the pixel drawn over
(xgpu_destination), no channel taken past what the pixel had or the
ceiling, nor below 0, destination alpha past 1 */
static void hdr_output(struct xgpu_text *text, unsigned short hdr)
{
	int ceiling = XGPU_HDR_CEILING_OF(hdr);
	int operation = XGPU_HDR_BLEND_OPERATION(hdr);
	const char *source = hdr_factor(XGPU_HDR_BLEND_SOURCE(hdr));
	const char *destination = hdr_factor(XGPU_HDR_BLEND_DESTINATION(hdr));

	if (!XGPU_HDR_SHADER_BLENDS(hdr))
	{
		xgpu_text_append(text, "\toutput.color = source;\n");
		return;
	}
	xgpu_text_append(text, "\t{\n\t\tfloat4 s = source, d = xgpu_destination;\n");
	switch (operation)
	{
	case XGPU_BLEND_SUBTRACT:
		xgpu_text_append(text, "\t\tfloat4 v = s * %s - d * %s;\n", source, destination);
		break;
	case XGPU_BLEND_REVERSE_SUBTRACT:
		xgpu_text_append(text, "\t\tfloat4 v = d * %s - s * %s;\n", destination, source);
		break;
	case XGPU_BLEND_MIN:
		xgpu_text_append(text, "\t\tfloat4 v = min(s, d);\n");
		break;
	case XGPU_BLEND_MAX:
		xgpu_text_append(text, "\t\tfloat4 v = max(s, d);\n");
		break;
	default:
		xgpu_text_append(text, "\t\tfloat4 v = s * %s + d * %s;\n", source, destination);
		break;
	}
	xgpu_text_append(text, "\t\tv = max(v, float4(0.0));\n");
	/* (a glow: more of the light the draw added, where it is bright:
	xgpu_hdr_expand) */
	if (XGPU_HDR_GLOW(hdr))
		xgpu_text_append(text,
			"\t\tfloat3 added = max(v.rgb - d.rgb, float3(0.0));\n"
			"\t\tfloat added_most = max(added.r, max(added.g, added.b)), most = max(v.r, max(v.g, v.b));\n"
			"\t\tif (added_most > 0.0)\n"
			"\t\t\tv.rgb += added * min(%.6f * smoothstep(%.6f, 1.0, most) * most / added_most, %.6f);\n",
			hdr_ceiling(ceiling) - 1.0f,
			ceiling == XGPU_HDR_CEILING_LIGHT ? XGPU_HDR_EXPAND_LIGHT_KNEE : XGPU_HDR_EXPAND_KNEE, XGPU_HDR_EXPAND_MOST);
	/* (a glow's ceiling keeps its hue: its brightest channel's; any other's
	each channel's) */
	if (XGPU_HDR_GLOW(hdr))
		xgpu_text_append(text,
			"\t\tfloat limit = max(max(d.r, max(d.g, d.b)), %.6f), brightest = max(v.r, max(v.g, v.b));\n"
			"\t\tif (brightest > limit)\n"
			"\t\t\tv.rgb *= limit / brightest;\n", hdr_ceiling(ceiling));
	else if (ceiling)
		xgpu_text_append(text, "\t\tv.rgb = min(v.rgb, max(d.rgb, float3(%.6f)));\n", hdr_ceiling(ceiling));
	if (hdr & XGPU_HDR_ALPHA_LIMIT)
		xgpu_text_append(text, "\t\tv.a = min(v.a, 1.0);\n");
	xgpu_text_append(text, "\t\toutput.color = v;\n\t}\n");
}

/* the combiners and the final combiner, which leave the pixel's colour in
result (zeroed: the stages' constants of a self-illuminated surface's
self-illumination read as 0, XGPU_HDR_SELF_ILLUMINATION) */
static void combiners(struct xgpu_text *text, const struct xgpu_pixel_program *program, const struct pixel_usage *usage,
	BOOL unclamped, unsigned int zeroed_stages)
{
	const struct xgpu_final_combiner *final = &program->final;
	int stage;

	for (stage = 0; stage < (int)program->combiner_count; stage++)
		combiner_stage(text, program, stage, usage, (zeroed_stages >> stage) & 1);

	if (!final->present)
	{
		xgpu_text_append(text, "\tfloat4 result = r0;\n");
	}
	else
	{
		if (usage->ef_product)
		{
			xgpu_text_append(text, "\tfloat4 ef_product = float4(");
			final_input(text, &final->inputs[4], FALSE, unclamped);
			xgpu_text_append(text, " * ");
			final_input(text, &final->inputs[5], FALSE, unclamped);
			xgpu_text_append(text, ", 0.0);\n");
		}
		if (usage->v1r0_sum)
		{
			xgpu_text_append(text, "\tfloat4 v1r0_sum = float4(%s + %s, 0.0);\n",
				final->invert_v1 ? "(1.0 - saturate(v1.rgb))" : "saturate(v1.rgb)",
				final->invert_r0 ? "(1.0 - saturate(r0.rgb))" : "saturate(r0.rgb)");
			if (final->clamp_sum)
				xgpu_text_append(text, "\tv1r0_sum = saturate(v1r0_sum);\n");
		}
		xgpu_text_append(text, "\tfloat3 fA = ");
		final_input(text, &final->inputs[0], FALSE, unclamped);
		xgpu_text_append(text, ";\n\tfloat3 fB = ");
		final_input(text, &final->inputs[1], FALSE, unclamped);
		xgpu_text_append(text, ";\n\tfloat3 fC = ");
		final_input(text, &final->inputs[2], FALSE, unclamped);
		xgpu_text_append(text, ";\n\tfloat3 fD = ");
		final_input(text, &final->inputs[3], FALSE, unclamped);
		xgpu_text_append(text, ";\n\tfloat fG = ");
		final_input(text, &final->inputs[6], TRUE, unclamped);
		xgpu_text_append(text, ";\n\tfloat4 result = float4(fA * fB + (1.0 - fA) * fC + fD, fG);\n");
	}
}

/* ---------- display.ambient_occlusion "world"

The occlusion found in the world at the pixel (xgpu.h's
XGPU_AMBIENT_OCCLUSION_WORLD; xgpu_spirv.c's world_ao_*, operation for
operation; port/linux/game/world_ao.c's world_ao_occlusion, its CPU
reference): the window's block at XGPU_MSL_BUFFER_WORLD_AO, the
structure's distance field at XGPU_MSL_TEXTURE_WORLD_SDF (a constexpr
linear sampler, clamped to its edge). Its functions go before the
fragment function whole: every one is called. */

/* the shapes' occlusion, world_proxies.c's world_proxy_occlusion
(sphere_occlusion, capsule_occlusion, box_occlusion) line for line, and
their reach, each kind's own factor below (their kinds: xgpu.h's XGPU_WORLD_AO_*) */
#define WORLD_AO_TEXT(value) WORLD_AO_TEXT_OF(value)
#define WORLD_AO_TEXT_OF(value) #value
static const char world_proxies_shapes[] =
	"static inline float world_proxy_sphere(float3 p, float3 n, float3 c, float radius)\n"
	"{\n"
	"\tfloat3 d = c - p;\n"
	"\tfloat length_squared = dot(d, d);\n"
	"\tif (length_squared <= radius * radius)\n"
	"\t\treturn 1.0;\n"
	"\tfloat l = sqrt(length_squared);\n"
	"\tfloat nl = dot(n, d) / l;\n"
	"\tfloat h2 = length_squared / (radius * radius);\n"
	"\tfloat k2 = 1.0 - h2 * nl * nl;\n"
	"\tif (k2 > 0.001)\n"
	"\t{\n"
	"\t\tfloat cosine = clamp(-nl * sqrt((h2 - 1.0) / (1.0 - nl * nl)), -1.0, 1.0);\n"
	"\t\tfloat occlusion = (nl * acos(cosine) - sqrt(k2 * (h2 - 1.0))) / h2 + atan(sqrt(k2 / (h2 - 1.0)));\n"
	"\t\treturn clamp(occlusion / 3.14159265358979, 0.0, 1.0);\n"
	"\t}\n"
	"\treturn nl > 0.0 ? nl / h2 : 0.0;\n"
	"}\n"
	"static inline float world_proxy_capsule(float3 p, float3 n, float3 a, float3 b, float radius)\n"
	"{\n"
	"\tfloat3 axis = b - a;\n"
	"\tfloat l = length(axis);\n"
	"\tif (l < 1.0e-4)\n"
	"\t\treturn world_proxy_sphere(p, n, a, radius);\n"
	"\taxis /= l;\n"
	"\tfloat3 q = a - p;\n"
	"\tfloat s0 = -dot(q, axis);\n"
	"\tfloat3 w = q + s0 * axis;\n"
	"\tfloat h = length(w);\n"
	"\tfloat nw = dot(n, w), nu = dot(n, axis);\n"
	"\tfloat t0 = -s0, t1 = l - s0;\n"
	"\tfloat line = 0.0;\n"
	"\tif (abs(nu) < 1.0e-6)\n"
	"\t{\n"
	"\t\tif (nw <= 0.0)\n"
	"\t\t\tt1 = t0;\n"
	"\t}\n"
	"\telse\n"
	"\t{\n"
	"\t\tfloat t_horizon = -nw / nu;\n"
	"\t\tif (nu > 0.0)\n"
	"\t\t\tt0 = t0 > t_horizon ? t0 : t_horizon;\n"
	"\t\telse\n"
	"\t\t\tt1 = t1 < t_horizon ? t1 : t_horizon;\n"
	"\t}\n"
	"\tif (t1 > t0 && h > 1.0e-4)\n"
	"\t{\n"
	"\t\tfloat h2 = h * h;\n"
	"\t\tfloat f0 = nw * (t0 / (2.0 * h2 * (h2 + t0 * t0)) + atan(t0 / h) / (2.0 * h2 * h)) - nu / (2.0 * (h2 + t0 * t0));\n"
	"\t\tfloat f1 = nw * (t1 / (2.0 * h2 * (h2 + t1 * t1)) + atan(t1 / h) / (2.0 * h2 * h)) - nu / (2.0 * (h2 + t1 * t1));\n"
	"\t\tline = 2.0 * radius * h / 3.14159265358979 * (f1 - f0);\n"
	"\t}\n"
	"\tfloat3 to_a = p - a, to_b = p - b;\n"
	"\tfloat to_a_length = length(to_a), to_b_length = length(to_b);\n"
	"\tfloat weight_a = to_a_length > 0.0 ? 0.5 * (1.0 - dot(axis, to_a) / to_a_length) : 0.5;\n"
	"\tfloat weight_b = to_b_length > 0.0 ? 0.5 * (1.0 + dot(axis, to_b) / to_b_length) : 0.5;\n"
	"\tfloat occlusion = line + weight_a * world_proxy_sphere(p, n, a, radius) +\n"
	"\t\tweight_b * world_proxy_sphere(p, n, b, radius);\n"
	"\tfloat closest_occlusion = world_proxy_sphere(p, n, a + clamp(s0, 0.0, l) * axis, radius);\n"
	"\tocclusion = occlusion > closest_occlusion ? occlusion : closest_occlusion;\n"
	"\treturn clamp(occlusion, 0.0, 1.0);\n"
	"}\n"
	"static inline float world_proxy_box_edge(float3 n, float3 a, float3 b)\n"
	"{\n"
	"\tfloat3 edge_normal = cross(a, b);\n"
	"\tfloat sine_squared = dot(edge_normal, edge_normal);\n"
	"\tif (!(sine_squared > 1.0e-14 * dot(a, a) * dot(b, b)))\n"
	"\t\treturn 0.0;\n"
	"\tfloat sine = sqrt(sine_squared);\n"
	"\treturn atan2(sine, dot(a, b)) * dot(n, edge_normal) / sine;\n"
	"}\n"
	"static inline float world_proxy_box(float3 p, float3 n, float3 centre, float3 half_x, float3 half_y, float extent_z)\n"
	"{\n"
	"\tfloat3 extents = float3(length(half_x), length(half_y), extent_z);\n"
	"\tif (extents.x <= 0.0 || extents.y <= 0.0 || extents.z <= 0.0)\n"
	"\t\treturn 0.0;\n"
	"\tfloat3 axes[3];\n"
	"\taxes[0] = half_x / extents.x;\n"
	"\taxes[1] = half_y / extents.y;\n"
	"\taxes[2] = cross(axes[0], axes[1]);\n"
	"\tfloat3 offset = p - centre, local, local_normal;\n"
	"\tfor (int axis = 0; axis < 3; axis++)\n"
	"\t{\n"
	"\t\tlocal[axis] = dot(offset, axes[axis]);\n"
	"\t\tlocal_normal[axis] = dot(n, axes[axis]);\n"
	"\t}\n"
	"\tif (abs(local.x) <= extents.x && abs(local.y) <= extents.y && abs(local.z) <= extents.z)\n"
	"\t\treturn 1.0;\n"
	"\tfloat occlusion = 0.0;\n"
	"\tif (dot(offset, offset) >= " WORLD_AO_TEXT(XGPU_WORLD_AO_FAR) " * " WORLD_AO_TEXT(XGPU_WORLD_AO_FAR) " * dot(extents, extents))\n"
	"\t{\n"
	"\t\tfor (int axis = 0; axis < 3; axis++)\n"
	"\t\t{\n"
	"\t\t\tint u = (axis + 1) % 3, v = (axis + 2) % 3;\n"
	"\t\t\tfor (int side = -1; side <= 1; side += 2)\n"
	"\t\t\t{\n"
	"\t\t\t\tfloat out = float(side) * local[axis] - extents[axis];\n"
	"\t\t\t\tif (out <= 0.0)\n"
	"\t\t\t\t\tcontinue;\n"
	"\t\t\t\tfloat3 r;\n"
	"\t\t\t\tr[axis] = float(side) * extents[axis] - local[axis];\n"
	"\t\t\t\tr[u] = -local[u];\n"
	"\t\t\t\tr[v] = -local[v];\n"
	"\t\t\t\tfloat r_squared = dot(r, r), facing = dot(local_normal, r);\n"
	"\t\t\t\tif (facing > 0.0)\n"
	"\t\t\t\t\tocclusion += 4.0 * extents[u] * extents[v] * out * facing / (3.14159265358979 * r_squared * r_squared);\n"
	"\t\t\t}\n"
	"\t\t}\n"
	"\t\treturn clamp(occlusion, 0.0, 1.0);\n"
	"\t}\n"
	"\tfor (int axis = 0; axis < 3; axis++)\n"
	"\t{\n"
	"\t\tfor (int side = -1; side <= 1; side += 2)\n"
	"\t\t{\n"
	"\t\t\tint u = (axis + 1) % 3, v = (axis + 2) % 3;\n"
	"\t\t\tfloat3 corners[4], exit_point, entry_point;\n"
	"\t\t\tfloat heights[4], face = 0.0;\n"
	"\t\t\tbool crossed = false;\n"
	"\t\t\tif (float(side) * local[axis] <= extents[axis])\n"
	"\t\t\t\tcontinue;\n"
	"\t\t\tfor (int corner = 0; corner < 4; corner++)\n"
	"\t\t\t{\n"
	"\t\t\t\tfloat su = (corner == 1 || corner == 2) ? 1.0 : -1.0, sv = corner >= 2 ? 1.0 : -1.0;\n"
	"\t\t\t\tcorners[corner][axis] = float(side) * extents[axis] - local[axis];\n"
	"\t\t\t\tcorners[corner][u] = su * extents[u] - local[u];\n"
	"\t\t\t\tcorners[corner][v] = sv * extents[v] - local[v];\n"
	"\t\t\t\theights[corner] = dot(local_normal, corners[corner]);\n"
	"\t\t\t}\n"
	"\t\t\tfor (int corner = 0; corner < 4; corner++)\n"
	"\t\t\t{\n"
	"\t\t\t\tint next = (corner + 1) % 4, across = (corner == 0 || corner == 2) ? v : u;\n"
	"\t\t\t\tfloat across_side = (corner == 0 || corner == 3) ? -1.0 : 1.0;\n"
	"\t\t\t\tbool above = heights[corner] >= 0.0, next_above = heights[next] >= 0.0;\n"
	"\t\t\t\tfloat3 crossing = corners[corner] + heights[corner] / (heights[corner] - heights[next]) * (corners[next] - corners[corner]);\n"
	"\t\t\t\tif (above && !next_above)\n"
	"\t\t\t\t{\n"
	"\t\t\t\t\texit_point = crossing;\n"
	"\t\t\t\t\tcrossed = true;\n"
	"\t\t\t\t}\n"
	"\t\t\t\tif (!above && next_above)\n"
	"\t\t\t\t\tentry_point = crossing;\n"
	"\t\t\t\tif ((above || next_above) && across_side * local[across] <= extents[across])\n"
	"\t\t\t\t\tface += world_proxy_box_edge(local_normal, above ? corners[corner] : crossing, next_above ? corners[next] : crossing);\n"
	"\t\t\t}\n"
	"\t\t\tif (crossed)\n"
	"\t\t\t\tface += world_proxy_box_edge(local_normal, exit_point, entry_point);\n"
	"\t\t\tocclusion += float(side) * face;\n"
	"\t\t}\n"
	"\t}\n"
	"\treturn clamp(abs(occlusion) / (2.0 * 3.14159265358979), 0.0, 1.0);\n"
	"}\n"
	/* world_proxy_box_fade: the box's bound on its occlusion made a fade */
	"static inline float world_proxy_box_fade(float3 p, float3 n, float3 centre, float3 half_x, float3 half_y, float extent_z)\n"
	"{\n"
	"\tfloat3 extents = float3(length(half_x), length(half_y), extent_z);\n"
	"\tif (extents.x <= 0.0 || extents.y <= 0.0 || extents.z <= 0.0)\n"
	"\t\treturn 0.0;\n"
	"\tfloat3 axes[3];\n"
	"\taxes[0] = half_x / extents.x;\n"
	"\taxes[1] = half_y / extents.y;\n"
	"\taxes[2] = cross(axes[0], axes[1]);\n"
	"\tfloat3 offset = p - centre, outside;\n"
	"\tfloat distance_squared = 0.0, height = 0.0;\n"
	"\tfor (int axis = 0; axis < 3; axis++)\n"
	"\t{\n"
	"\t\tfloat local = dot(offset, axes[axis]), local_normal = dot(n, axes[axis]);\n"
	"\t\toutside[axis] = abs(local) - extents[axis];\n"
	"\t\toutside[axis] = outside[axis] > 0.0 ? outside[axis] : 0.0;\n"
	"\t\tdistance_squared += outside[axis] * outside[axis];\n"
	"\t\theight += extents[axis] * abs(local_normal) - local_normal * local;\n"
	"\t}\n"
	"\tif (distance_squared <= 0.0)\n"
	"\t\treturn 1.0;\n"
	"\tif (height <= 0.0)\n"
	"\t\treturn 0.0;\n"
	"\tfloat faces = extents.y * extents.z * outside.x + extents.x * extents.z * outside.y + extents.x * extents.y * outside.z;\n"
	"\tfloat distance = sqrt(distance_squared);\n"
	"\tfloat bound = " WORLD_AO_TEXT(XGPU_WORLD_AO_LEAST) " * 4.0 / 3.14159265358979 * faces * (height < distance ? height : distance) /\n"
	"\t\t(distance_squared * distance_squared);\n"
	"\treturn clamp(bound - 1.0, 0.0, 1.0);\n"
	"}\n";

static void world_ao_struct(struct xgpu_text *text)
{
	xgpu_text_append(text,
		"struct xgpu_world_ao\n"
		"{\n"
		"\tfloat4 field[2];\n"
		"\tfloat4 parameters[2];\n"
		"\tfloat4 proxies[%d];\n"
		"\tfloat4 grid[2];\n"
		"\tuint4 cells[%d];\n"
		"};\n", 3 * XGPU_WORLD_AO_SHAPES, XGPU_WORLD_AO_GRID_CELLS / 2);
}

/* found once a pixel: a surface's pixel's occlusion read from the pass's
texels (world_ao_shading.c's world_ao_upsample): at a quarter of the
pixels (the block's parameters[1].w, 2), the four about it by their
bilinear weights and their closeness (their distances' and normals' to its
own), or where none is close the closest of the 4x4 about it; at each
pixel (1), its own */
static void world_ao_upsample(struct xgpu_text *text)
{
	xgpu_text_append(text,
		"\tfloat occlusion;\n"
		"\t{\n"
		"\t\tfloat scale = world_ao.parameters[1].w;\n"
		"\t\tif (scale <= 1.0)\n"
		"\t\t\tocclusion = world_ao_pixels.read(uint2(in.frag_position.xy)).r;\n"
		"\t\telse\n"
		"\t\t{\n"
		"\t\t\tfloat3 n = in.xWorldNormal.xyz * rsqrt(max(dot(in.xWorldNormal.xyz, in.xWorldNormal.xyz), 1.0e-24));\n"
		"\t\t\tfloat3 ray = in.xWorldPosition - world_ao.grid[0].xyz;\n"
		"\t\t\tfloat distance = length(ray);\n"
		"\t\t\tfloat facing = max(abs(dot(n, ray)) / max(distance, 1.0e-12), %.6f);\n"
		"\t\t\tfloat2 at = in.frag_position.xy / scale - 0.5, base = floor(at), f = at - base;\n"
		"\t\t\tint2 last = int2(world_ao_pixels.get_width(), world_ao_pixels.get_height()) - 1;\n"
		"\t\t\tfloat sum = 0.0, weights = 0.0;\n"
		"\t\t\tfor (int j = 0; j < 2; j++)\n"
		"\t\t\t\tfor (int i = 0; i < 2; i++)\n"
		"\t\t\t\t{\n"
		"\t\t\t\t\tfloat4 texel = world_ao_pixels.read(uint2(clamp(int2(base) + int2(i, j), int2(0), last)));\n"
		"\t\t\t\t\tfloat weight = (i != 0 ? f.x : 1.0 - f.x) * (j != 0 ? f.y : 1.0 - f.y) *\n"
		"\t\t\t\t\t\tworld_ao_closeness(texel, distance, facing, n);\n"
		"\t\t\t\t\tsum += weight * texel.x;\n"
		"\t\t\t\t\tweights += weight;\n"
		"\t\t\t\t}\n"
		"\t\t\tif (weights > %.6g)\n"
		"\t\t\t\tocclusion = sum / weights;\n"
		"\t\t\telse\n"
		"\t\t\t{\n"
		"\t\t\t\tfloat best = -1.0;\n"
		"\t\t\t\tocclusion = 1.0;\n"
		"\t\t\t\tfor (int j = -1; j < 3; j++)\n"
		"\t\t\t\t\tfor (int i = -1; i < 3; i++)\n"
		"\t\t\t\t\t{\n"
		"\t\t\t\t\t\tfloat4 texel = world_ao_pixels.read(uint2(clamp(int2(base) + int2(i, j), int2(0), last)));\n"
		"\t\t\t\t\t\tfloat closeness = world_ao_closeness(texel, distance, facing, n);\n"
		"\t\t\t\t\t\tif (closeness > best)\n"
		"\t\t\t\t\t\t{\n"
		"\t\t\t\t\t\t\tbest = closeness;\n"
		"\t\t\t\t\t\t\tocclusion = texel.x;\n"
		"\t\t\t\t\t\t}\n"
		"\t\t\t\t\t}\n"
		"\t\t\t}\n"
		"\t\t}\n"
		"\t}\n", XGPU_WORLD_AO_UPSAMPLE_FACING, XGPU_WORLD_AO_UPSAMPLE_LEAST);
}

/* (its function: a texel's closeness to the pixel, world_ao_shading.c's
world_ao_upsample_closeness) */
static void world_ao_closeness(struct xgpu_text *text)
{
	xgpu_text_append(text,
		"static inline float world_ao_closeness(float4 texel, float distance, float facing, float3 n)\n"
		"{\n"
		"\tif (!(texel.y > 0.0) || !(distance > 0.0))\n"
		"\t\treturn 0.0;\n"
		"\tfloat z = 1.0 - abs(texel.z) - abs(texel.w), t = max(-z, 0.0);\n"
		"\tfloat3 texel_n = normalize(float3(texel.zw + select(float2(t), float2(-t), texel.zw >= 0.0), z));\n"
		"\tfloat relative = abs(texel.y / distance - 1.0) * facing * %.6f;\n"
		"\tfloat along = max(dot(n, texel_n), 0.0);\n"
		"\talong *= along;\n"
		"\talong *= along;\n"
		"\talong *= along;\n"
		"\treturn along / (1.0 + relative * relative);\n"
		"}\n", 1.0 / XGPU_WORLD_AO_UPSAMPLE_DEPTH);
}

static void world_ao_functions(struct xgpu_text *text, int light)
{
	world_ao_struct(text);
	/* (the field's: never the lightmap pass's) */
	if (light != XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
		xgpu_text_append(text,
		/* the field's distance at a point (world_sdf_sample): its cell in the
		table, all outside or all inside the range, or a brick of the atlas
		whose texel is filtered (xgpu.h) */
		"static inline float world_sdf_distance(constant xgpu_world_ao &w, texture3d<float> world_sdf,\n"
		"\ttexture3d<float> world_sdf_table, float3 point)\n"
		"{\n"
		"\tconstexpr sampler field_sampler(filter::linear, address::clamp_to_edge, coord::normalized);\n"
		"\tfloat range = %d.0 / w.field[0].w;\n"
		"\tfloat3 c = clamp((point - w.field[0].xyz) * w.field[0].w, float3(0.0), w.field[1].xyz * %d.0);\n"
		"\tfloat3 cell = min(floor(c * %.6f), w.field[1].xyz - 1.0);\n"
		"\tfloat4 entry = floor(world_sdf_table.read(uint3(cell)) * 255.0 + 0.5);\n"
		"\tif (entry.w != %d.0)\n"
		"\t\treturn entry.w == %d.0 ? -range : range;\n"
		"\tfloat3 at = entry.xyz * %d.0 + (c - cell * %d.0) + 0.5;\n"
		"\treturn world_sdf.sample(field_sampler, at / float3(%d.0, %d.0, w.field[1].w * %d.0), level(0.0)).r * range;\n"
		"}\n"
		/* A_sdf(p, n): the cone of taps along the normal */
		"static inline float world_sdf_occlusion(constant xgpu_world_ao &w, texture3d<float> world_sdf,\n"
		"\ttexture3d<float> world_sdf_table, float3 p, float3 n, float own)\n"
		"{\n"
		"\tfloat first = max(1.0 / w.field[0].w, %.6f);\n"
		"\tfloat occlusion = 0.0;\n"
		"\tfor (int tap = 0; tap < %d; tap++)\n"
		"\t{\n"
		"\t\tfloat h = first * float(1 << tap);\n"
		"\t\tfloat d = world_sdf_distance(w, world_sdf, world_sdf_table, p + n * h) - own;\n"
		"\t\tocclusion += (1.0 / float(2 << tap)) * saturate((h - d) / h);\n"
		"\t}\n"
		"\treturn 1.0 - saturate(%.6f * occlusion);\n"
		"}\n"
		/* ... and where cards (a two-sided model shader's: xgpu.h's
		XGPU_WORLD_AO_INPUT_CARDS) that of the most open of the sky's
		direction (up) and the card's two faces, the first of them where
		alike; n the direction found (world_ao_shading.c's
		world_ao_cards_occlusion) */
		"static inline float world_sdf_cards_occlusion(constant xgpu_world_ao &w, texture3d<float> world_sdf,\n"
		"\ttexture3d<float> world_sdf_table, float3 p, thread float3 &n, float own, bool cards)\n"
		"{\n"
		"\tfloat field = world_sdf_occlusion(w, world_sdf, world_sdf_table, p, n, own);\n"
		"\tif (!cards)\n"
		"\t\treturn field;\n"
		"\tfloat3 up = float3(0.0, 0.0, 1.0), direction = n;\n"
		"\tfloat sky = world_sdf_occlusion(w, world_sdf, world_sdf_table, p, up, own);\n"
		"\tfloat back = world_sdf_occlusion(w, world_sdf, world_sdf_table, p, -n, own);\n"
		"\tif (!(field > sky))\n"
		"\t{\n"
		"\t\tfield = sky;\n"
		"\t\tdirection = up;\n"
		"\t}\n"
		"\tif (back > field)\n"
		"\t{\n"
		"\t\tfield = back;\n"
		"\t\tdirection = -n;\n"
		"\t}\n"
		"\tn = direction;\n"
		"\treturn field;\n"
		"}\n", XGPU_WORLD_SDF_RANGE_VOXELS, XGPU_WORLD_SDF_BRICK, 1.0 / XGPU_WORLD_SDF_BRICK, XGPU_WORLD_SDF_CELL_BRICK,
		XGPU_WORLD_SDF_CELL_INSIDE, XGPU_WORLD_SDF_BRICK_SPAN, XGPU_WORLD_SDF_BRICK,
		XGPU_WORLD_SDF_ATLAS * XGPU_WORLD_SDF_BRICK_SPAN, XGPU_WORLD_SDF_ATLAS * XGPU_WORLD_SDF_BRICK_SPAN,
		XGPU_WORLD_SDF_BRICK_SPAN, XGPU_WORLD_AO_STEP, XGPU_WORLD_AO_TAPS, XGPU_WORLD_AO_K);
	xgpu_text_append(text, "%s", world_proxies_shapes);
	xgpu_text_append(text,
		/* a vehicle's occlusion field (its shape and the next two:
		xgpu_stream.h's XGPU_WORLD_AO_PROXY_FIELD), its occlusion faded out
		over the last of its reach (world_ao_shading.c's
		world_ao_field_proxy_occlusion) */
		"static inline float world_proxy_field(constant xgpu_world_ao &w, texture3d<float> objects, int shape, float3 p,\n"
		"\tfloat3 n, float4 v0, float4 v1, float4 v2)\n"
		"{\n"
		"\tconstexpr sampler objects_sampler(filter::linear, address::clamp_to_edge, coord::normalized);\n"
		"\tfloat4 b0 = w.proxies[shape * 3 + 3], b1 = w.proxies[shape * 3 + 4], b2 = w.proxies[shape * 3 + 5];\n"
		"\tfloat4 c0 = w.proxies[shape * 3 + 6], c1 = w.proxies[shape * 3 + 7];\n"
		"\tfloat reach = v2.w, away = length(p - v0.xyz);\n"
		"\tif (away >= reach)\n"
		"\t\treturn 0.0;\n"
		"\tfloat4 c2 = w.proxies[shape * 3 + 8];\n"
		"\tfloat3 g = float3(dot(v1.xyz, p), dot(b0.xyz, p), dot(b1.xyz, p)) + v2.xyz;\n"
		"\tfloat3 local = float3(dot(v1.xyz, n), dot(b0.xyz, n), dot(b1.xyz, n));\n"
		"\tlocal *= rsqrt(max(dot(local, local), 1.0e-24));\n"
		/* (its geometry's box's bound, in its voxels: its occlusion is under it) */
		"\tfloat3 extents = c2.xyz, offset = g - b2.xyz * 0.5;\n"
		"\tfloat3 beyond = max(abs(offset) - extents, float3(0.0));\n"
		"\tfloat distance_squared = dot(beyond, beyond);\n"
		"\tfloat height = dot(extents, abs(local)) - dot(local, offset);\n"
		"\tfloat box_fade = 1.0;\n"
		"\tif (distance_squared > 0.0)\n"
		"\t{\n"
		"\t\tif (height <= 0.0)\n"
		"\t\t\treturn 0.0;\n"
		"\t\tfloat distance = sqrt(distance_squared);\n"
		"\t\tfloat faces = dot(beyond, float3(extents.y * extents.z, extents.x * extents.z, extents.x * extents.y));\n"
		"\t\tbox_fade = clamp(%.6f * faces * (height < distance ? height : distance) / (distance_squared * distance_squared)\n"
		"\t\t\t- 1.0, 0.0, 1.0);\n"
		"\t}\n"
		"\tfloat fade = saturate((reach - away) / (reach * %.6f)) * box_fade;\n"
		"\tif (fade <= 0.0)\n"
		"\t\treturn 0.0;\n"
		"\tfloat3 q = clamp(g, float3(0.0), b2.xyz);\n"
		"\tfloat outside = length(g - q), from = length(q - b2.xyz * 0.5);\n"
		"\tfloat scale = (from + 1.0e-3) / (from + outside + 1.0e-3);\n"
		"\tscale *= scale;\n"
		"\tfloat4 tv = objects.sample(objects_sampler, (q + c0.xyz) * c1.xyz, level(0.0));\n"
		"\treturn saturate(scale * (tv.x + dot(local, tv.yzw))) * fade;\n"
		"}\n", XGPU_WORLD_AO_LEAST * 4.0f / XGPU_WORLD_AO_PI, XGPU_WORLD_AO_REACH_FADE);
	xgpu_text_append(text,
		/* each kind's factor where it can shade p (world_ao.c's
		world_ao_proxy_factor), 1 where it cannot: within its reach of its
		centre (a capsule's segment's middle), not wholly below p's tangent
		plane (its centre's height under the plane past the radius that holds
		it), faded out over the last of its reach (a box by its bound). Each
		kind has its own, so that a walk runs only the kinds of the shapes it
		meets (world_proxies_occlusion) */
		"static inline float world_proxy_sphere_reach(float4 v2)\n"
		"{\n"
		"\treturn 16.0 * v2.x;\n"
		"}\n"
		"static inline float world_proxy_capsule_reach(float4 v0, float4 v1, float4 v2)\n"
		"{\n"
		"\tfloat l = length(v1.xyz - v0.xyz), radius = v2.x;\n"
		"\treturn 0.5 * l + 16.0 * sqrt(radius * radius + 2.0 * radius * l / 3.14159265358979);\n"
		"}\n"
		"static inline float world_proxy_box_reach(float4 v1, float4 v2)\n"
		"{\n"
		"\tfloat x = length(v1.xyz), y = length(v2.xyz), z = v2.w;\n"
		"\tfloat areas = sqrt((y * z) * (y * z) + (x * z) * (x * z) + (x * y) * (x * y));\n"
		"\treturn sqrt(x * x + y * y + z * z) + sqrt(" WORLD_AO_TEXT(XGPU_WORLD_AO_LEAST) " * 4.0 / 3.14159265358979 * areas);\n"
		"}\n"
		"static inline float world_proxy_sphere_factor(float3 p, float3 n, float4 v0, float4 v2)\n"
		"{\n"
		"\tfloat3 centre = v0.xyz;\n"
		"\tfloat reach = world_proxy_sphere_reach(v2), away = length(p - centre);\n"
		"\tif (away >= reach || dot(n, centre - p) < -v2.x)\n"
		"\t\treturn 1.0;\n"
		"\tfloat fade = saturate((reach - away) / (reach * %.6f));\n"
		"\treturn 1.0 - world_proxy_sphere(p, n, centre, v2.x) * fade;\n"
		"}\n"
		"static inline float world_proxy_capsule_factor(float3 p, float3 n, float4 v0, float4 v1, float4 v2)\n"
		"{\n"
		"\tfloat3 centre = (v0.xyz + v1.xyz) * 0.5;\n"
		"\tfloat reach = world_proxy_capsule_reach(v0, v1, v2), away = length(p - centre);\n"
		"\tif (away >= reach || dot(n, centre - p) < -(0.5 * length(v1.xyz - v0.xyz) + v2.x))\n"
		"\t\treturn 1.0;\n"
		"\tfloat fade = saturate((reach - away) / (reach * %.6f));\n"
		"\treturn 1.0 - world_proxy_capsule(p, n, v0.xyz, v1.xyz, v2.x) * fade;\n"
		"}\n"
		"static inline float world_proxy_box_factor(float3 p, float3 n, float4 v0, float4 v1, float4 v2)\n"
		"{\n"
		"\tfloat3 centre = v0.xyz;\n"
		"\tfloat reach = world_proxy_box_reach(v1, v2), away = length(p - centre);\n"
		"\tif (away >= reach || dot(n, centre - p) < -sqrt(dot(v1.xyz, v1.xyz) + dot(v2, v2)))\n"
		"\t\treturn 1.0;\n"
		"\tfloat fade = world_proxy_box_fade(p, n, v0.xyz, v1.xyz, v2.xyz, v2.w);\n"
		"\tif (fade <= 0.0)\n"
		"\t\treturn 1.0;\n"
		"\treturn 1.0 - world_proxy_box(p, n, v0.xyz, v1.xyz, v2.xyz, v2.w) * fade;\n"
		"}\n", XGPU_WORLD_AO_REACH_FADE, XGPU_WORLD_AO_REACH_FADE);
	xgpu_text_append(text,
		/* A_prox(p, n): the shapes of the pixel's cell of the grid (those
		whose reach touches it: world_ao_grid) below the draw's limit (the
		dynamic objects' first: world_ao_read), lowest first, of the draw's
		object's others within their reach, each faded out over the last of
		it (a box by its bound).

		Where the GPU has the SIMD-groups' reductions (the block's
		parameters[1].z, set by the backend: xgpu_metal_world_ao.c), a
		SIMD-group walks the shapes of all of its pixels' cells together
		(simd_or), lowest first, each pixel taking only its own cell's, as
		the SPIR-V emitter's subgroups do (xgpu_spirv.c's
		world_proxies_occlusion): each pixel's product is the same, of the
		same shapes in the same order, but each shape's index, so its place
		and its kind, is the group's. Its loads are then of one place, and
		only the kinds of the shapes it meets are run. Without them each
		pixel walks its own cell's. The spheres, capsules and boxes below
		analytic_from are left out (the pass's: the distance-field scene's
		rays find them, world_ao_traced); occlusion fields never */
		"static inline float world_proxies_occlusion(constant xgpu_world_ao &w, texture3d<float> objects, float3 p, float3 n,\n"
		"\tfloat owner, int limit, int analytic_from)\n"
		"{\n"
		"\tfloat product = 1.0;\n"
		"\tbool simd = w.parameters[1].z != 0.0;\n"
		"\tfloat3 offset = p - w.grid[0].xyz;\n"
		"\tfloat3 at = sign(offset) * log2(1.0 + abs(offset) * w.grid[1].w) * w.grid[1].xyz + float3(%d.0, %d.0, %d.0);\n"
		"\tint cell = (clamp(int(floor(at.z)), 0, %d) * %d + clamp(int(floor(at.y)), 0, %d)) * %d +\n"
		"\t\tclamp(int(floor(at.x)), 0, %d);\n"
		"\tuint4 pair = w.cells[cell >> 1];\n"
		"\tuint words[2] = { (cell & 1) != 0 ? pair.z : pair.x, (cell & 1) != 0 ? pair.w : pair.y };\n"
		"\tfor (int word = 0; word < 2; word++)\n"
		"\t{\n"
		"\t\tint allowed = clamp(limit - word * 32, 0, 32);\n"
		"\t\tuint keep = allowed < 32 ? (1u << uint(clamp(allowed, 0, 31))) - 1u : 0xFFFFFFFFu;\n"
		"\t\tuint own = words[word] & keep, bits = own;\n"
		"\t\tif (simd)\n"
		"\t\t\tbits = simd_broadcast_first(simd_or(own));\n"
		"\t\tfor (; bits != 0u; bits &= bits - 1u)\n"
		"\t\t{\n"
		"\t\t\tuint lowest = ctz(bits);\n"
		"\t\t\tif ((own & (1u << lowest)) == 0u)\n"
		"\t\t\t\tcontinue;\n"
		"\t\t\tint shape = word * 32 + int(lowest);\n"
		"\t\t\tfloat4 v0 = w.proxies[shape * 3], v1 = w.proxies[shape * 3 + 1], v2 = w.proxies[shape * 3 + 2];\n"
		"\t\t\tif (v1.w == owner)\n"
		"\t\t\t\tcontinue;\n"
		"\t\t\tif (v0.w == %d.0)\n"
		"\t\t\t\tproduct *= 1.0 - world_proxy_field(w, objects, shape, p, n, v0, v1, v2);\n"
		"\t\t\telse if (shape < analytic_from)\n"
		"\t\t\t\tcontinue;\n"
		"\t\t\telse if (v0.w == %d.0)\n"
		"\t\t\t\tproduct *= world_proxy_capsule_factor(p, n, v0, v1, v2);\n"
		"\t\t\telse if (v0.w == %d.0)\n"
		"\t\t\t\tproduct *= world_proxy_sphere_factor(p, n, v0, v2);\n"
		"\t\t\telse if (v0.w == %d.0)\n"
		"\t\t\t\tproduct *= world_proxy_box_factor(p, n, v0, v1, v2);\n"
		"\t\t}\n"
		"\t}\n"
		"\treturn product;\n"
		"}\n",
		XGPU_WORLD_AO_GRID_X / 2, XGPU_WORLD_AO_GRID_Y / 2, XGPU_WORLD_AO_GRID_Z / 2,
		XGPU_WORLD_AO_GRID_Z - 1, XGPU_WORLD_AO_GRID_Y, XGPU_WORLD_AO_GRID_Y - 1, XGPU_WORLD_AO_GRID_X,
		XGPU_WORLD_AO_GRID_X - 1, XGPU_WORLD_AO_FIELD, XGPU_WORLD_AO_CAPSULE, XGPU_WORLD_AO_SPHERE, XGPU_WORLD_AO_BOX);
}

/* ---------- the distance-field scene's ray step

world_trace: port/linux/game/world_cascades.c's world_trace_ray operation
for operation (tools/test_world_trace.py runs this text as C++ against it):
from origin along the unit direction to at most t_max, through the
cascades (cascades[k]: the first voxel's centre and the voxel, 0 not
ready; the texture's at XGPU_MSL_TEXTURE_WORLD_CASCADES), hitting what mask
allows (XGPU_WORLD_TRACE_*), each distance only once the ray is outside it.
The hardware's ray queries (#602) would implement the same signature
(docs/world-ao.md) */
static const char world_trace_functions[] =
	"struct world_trace_hit\n"
	"{\n"
	"\tbool hit;\n"
	"\tfloat t;\n"
	"\tint kind;\n"
	"};\n"
	/* the cascade a point is in: the finest ready whose window holds it
	with its filter's neighbours (-1 none) */
	"static inline int world_cascade_find(constant float4 *cascades, float3 p, int first)\n"
	"{\n"
	"\tint found = -1;\n"
	"\tfor (int cascade = " WORLD_AO_TEXT(XGPU_WORLD_CASCADES) " - 1; cascade >= first; cascade--)\n"
	"\t{\n"
	"\t\tfloat4 c = cascades[cascade];\n"
	"\t\tif (c.w <= 0.0)\n"
	"\t\t\tcontinue;\n"
	"\t\tfloat3 q = (p - c.xyz) / c.w;\n"
	"\t\tfloat least = min(min(q.x, q.y), q.z), most = max(max(q.x, q.y), q.z);\n"
	"\t\tif (least >= 0.0 && most <= " WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SIDE) ".0 - 1.0)\n"
	"\t\t\tfound = cascade;\n"
	"\t}\n"
	"\treturn found;\n"
	"}\n"
	/* its two distances there (world units): the voxels' centres'
	coordinate modulo the side, a texel in (the guard past the last) */
	"static inline float4 world_cascade_sample(texture3d<float> world_cascades, float4 c, int cascade, float3 p)\n"
	"{\n"
	"\tconstexpr sampler cascades_sampler(filter::linear, address::clamp_to_edge, coord::normalized);\n"
	"\tfloat3 at = p / c.w - 0.5;\n"
	"\tfloat3 place = at - " WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SIDE) ".0 * floor(at / " WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SIDE) ".0);\n"
	"\tfloat3 uvw = float3((place.x + 0.5) / " WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SPAN) ".0, (place.y + 0.5) / "
		WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SPAN) ".0,\n"
	"\t\t(place.z + 0.5 + " WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SPAN) ".0 * float(cascade)) / ("
		WORLD_AO_TEXT(XGPU_WORLD_CASCADE_SPAN) ".0 * " WORLD_AO_TEXT(XGPU_WORLD_CASCADES) ".0));\n"
	"\treturn world_cascades.sample(cascades_sampler, uvw, level(0.0)) * (" WORLD_AO_TEXT(XGPU_WORLD_CASCADE_RANGE_VOXELS)
		".0 * c.w);\n"
	"}\n"
	"static inline world_trace_hit world_trace(texture3d<float> world_cascades, constant float4 *cascades, float3 origin,\n"
	"\tfloat3 direction, float t_max, int mask)\n"
	"{\n"
	"\tworld_trace_hit result;\n"
	"\tresult.hit = false;\n"
	"\tresult.t = t_max;\n"
	"\tresult.kind = 0;\n"
	"\tbool active_structure = false, active_objects = false;\n"
	"\tfloat t = 0.0;\n"
	"\tfor (int step = 0; step < " WORLD_AO_TEXT(XGPU_WORLD_TRACE_STEPS) "; step++)\n"
	"\t{\n"
	"\t\tfloat3 p = origin + direction * t;\n"
	"\t\tint cascade = world_cascade_find(cascades, p, 0);\n"
	"\t\tif (cascade < 0)\n"
	"\t\t\tbreak;\n"
	"\t\tfloat4 c = cascades[cascade];\n"
	"\t\tfloat voxel = c.w;\n"
	"\t\tfloat4 d = world_cascade_sample(world_cascades, c, cascade, p);\n"
	"\t\tfloat nearest = " WORLD_AO_TEXT(XGPU_WORLD_CASCADE_RANGE_VOXELS) ".0 * voxel;\n"
	"\t\tfloat inside = nearest;\n"
	"\t\tif ((mask & " WORLD_AO_TEXT(XGPU_WORLD_TRACE_STRUCTURE) ") != 0)\n"
	"\t\t{\n"
	"\t\t\tif (d.x >= 0.0)\n"
	"\t\t\t\tactive_structure = true;\n"
	"\t\t\tif (active_structure)\n"
	"\t\t\t\tnearest = min(nearest, d.x);\n"
	"\t\t\telse\n"
	"\t\t\t\tinside = min(inside, -d.x);\n"
	"\t\t}\n"
	"\t\tif ((mask & " WORLD_AO_TEXT(XGPU_WORLD_TRACE_OBJECTS) ") != 0)\n"
	"\t\t{\n"
	"\t\t\tif (d.y >= 0.0)\n"
	"\t\t\t\tactive_objects = true;\n"
	"\t\t\tif (active_objects)\n"
	"\t\t\t\tnearest = min(nearest, d.y);\n"
	"\t\t\telse\n"
	"\t\t\t\tinside = min(inside, -d.y);\n"
	"\t\t}\n"
	"\t\tif (nearest < " WORLD_AO_TEXT(XGPU_WORLD_TRACE_HIT) " * voxel)\n"
	"\t\t{\n"
	"\t\t\tresult.hit = true;\n"
	"\t\t\tresult.t = t;\n"
	"\t\t\tresult.kind = (mask & " WORLD_AO_TEXT(XGPU_WORLD_TRACE_STRUCTURE) ") != 0 && active_structure && d.x <= nearest ? "
		WORLD_AO_TEXT(XGPU_WORLD_TRACE_STRUCTURE) " : " WORLD_AO_TEXT(XGPU_WORLD_TRACE_OBJECTS) ";\n"
	"\t\t\tbreak;\n"
	"\t\t}\n"
	"\t\tt += max(min(nearest, inside), " WORLD_AO_TEXT(XGPU_WORLD_TRACE_LEAST_STEP) " * voxel);\n"
	"\t\tif (t >= t_max)\n"
	"\t\t\tbreak;\n"
	"\t}\n"
	"\treturn result;\n"
	"}\n";

void xgpu_msl_world_trace(struct xgpu_text *text)
{
	xgpu_text_append(text, "%s", world_trace_functions);
}

char *xgpu_msl_world_trace_test(void)
{
	struct xgpu_text text = { 0 };

	xgpu_text_append(&text, "#include <metal_stdlib>\nusing namespace metal;\n");
	xgpu_msl_world_trace(&text);
	xgpu_text_append(&text,
		"struct world_trace_in\n"
		"{\n"
		"\tfloat4 position [[position]];\n"
		"\tfloat3 origin [[user(origin)]];\n"
		"\tfloat3 direction [[user(direction)]];\n"
		"\tfloat4 parameters [[user(parameters), flat]];\n"
		"};\n"
		"fragment float4 xgpu_world_trace_test(world_trace_in in [[stage_in]],\n"
		"\ttexture3d<float> world_cascades [[texture(%d)]], constant float4 *cascades [[buffer(0)]])\n"
		"{\n"
		"\tworld_trace_hit hit = world_trace(world_cascades, cascades, in.origin, in.direction, in.parameters.x,\n"
		"\t\tint(in.parameters.y));\n"
		"\treturn float4(hit.hit ? 1.0 : 0.0, hit.t, float(hit.kind), 0.0);\n"
		"}\n", XGPU_MSL_TEXTURE_WORLD_CASCADES);
	return text.buffer;
}

/* the occlusion at the pixel, for the light it multiplies
(XGPU_AMBIENT_OCCLUSION_LIGHT_*): a model's, the field's relative to its
reference faded in, times the others' proxies; the lightmap pass's, the
proxies alone; then the floor and the strength (xgpu.h) */
/* whether a lightmap pass's draw reads the static scenery's pages (their
bytes the coverage's format, read as 1, 1, 1, the byte: their alpha): its
stage 2 is its lightmap, a 2D texture at projected coordinates (else it
walks every shape: world_ao_read) */
static BOOL world_ao_paged(const struct xgpu_pixel_program *program, int light)
{
	return light == XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP && program->stages[2].mode == XGPU_TEXTURE_PROJECT2D &&
		program->stages[2].sampler == _xgpu_sampler_2d;
}

static void world_ao_read(struct xgpu_text *text, const struct xgpu_pixel_program *program, int light)
{
	/* (the shapes walked: the dynamic objects', parameters[1].y of them;
	every one in the lightmap pass where its page has no layer (u.world_ao.y
	< 0), or it reads no lightmap: world_ao_shading.c's
	world_ao_proxies_walked) */
	xgpu_text_append(text,
		"\tfloat occlusion;\n"
		"\t{\n"
		"\t\tfloat3 world_n = in.xWorldNormal.xyz * rsqrt(max(dot(in.xWorldNormal.xyz, in.xWorldNormal.xyz), 1.0e-24));\n"
		"\t\tint limit = int(world_ao.parameters[1].y);\n");
	/* (a model's field: where its reference is negative, a static scenery
	placement's own draw, its taps from the pixel's own depth in the field,
	relative to none; a two-sided shader's cards, its world_ao.w not 0, about
	their most open direction, the shapes too) */
	if (light != XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
		xgpu_text_append(text,
			"\t\tbool baked = u.world_ao.y < 0.0;\n"
			"\t\tfloat inside = min(world_sdf_distance(world_ao, world_sdf, world_sdf_table, in.xWorldPosition), 0.0);\n"
			"\t\tfloat field = world_sdf_cards_occlusion(world_ao, world_sdf, world_sdf_table, in.xWorldPosition, world_n,\n"
			"\t\t\tbaked ? inside : 0.0, u.world_ao.w != 0.0);\n");
	if (world_ao_paged(program, light))
		xgpu_text_append(text,
			"\t\tfloat scenery = 1.0;\n"
			"\t\tif (u.world_ao.y < 0.0)\n"
			"\t\t\tlimit = %d;\n"
			"\t\telse\n"
			"\t\t{\n"
			"\t\t\tconstexpr sampler page_sampler(filter::linear, address::clamp_to_edge, coord::normalized);\n"
			"\t\t\tfloat2 page = fast::divide(in.xT2.xy, float2(in.xT2.w != 0.0 ? in.xT2.w : 1.0)) * u.texture_scale[2].xy;\n"
			"\t\t\tscenery = world_ao_scenery.sample(page_sampler, page, uint(u.world_ao.y), level(0.0)).a;\n"
			"\t\t}\n", XGPU_WORLD_AO_SHAPES);
	else if (light == XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
		xgpu_text_append(text, "\t\tlimit = %d;\n", XGPU_WORLD_AO_SHAPES);
	xgpu_text_append(text,
		"\t\tfloat proxies = world_proxies_occlusion(world_ao, world_ao_objects, in.xWorldPosition, world_n, u.world_ao.x,\n"
		"\t\t\tlimit, 0);\n");
	if (world_ao_paged(program, light))
		xgpu_text_append(text, "\t\tproxies *= scenery;\n");
	if (light == XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
		xgpu_text_append(text, "\t\tocclusion = mix(1.0, max(proxies, %.6f), world_ao.parameters[0].y);\n",
			XGPU_WORLD_AO_FLOOR);
	else
		xgpu_text_append(text,
			"\t\tfield = mix(field, saturate(field / max(baked ? 1.0 : u.world_ao.y, %.6f)), world_ao.parameters[1].x);\n"
			"\t\tocclusion = mix(1.0, max(mix(1.0, field, world_ao.parameters[0].z) * proxies, %.6f), world_ao.parameters[0].x);\n",
			XGPU_WORLD_AO_REFERENCE_LEAST, XGPU_WORLD_AO_FLOOR);
	xgpu_text_append(text, "\t}\n");
}

/* found once a pixel: a pre-pass draw's inputs to its pixel's occlusion
(xgpu.h's XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS; world_ao_shading.c's
world_ao_inputs_pack): its normal octahedral, its owner, its reference
(a model's) or its page's texel (the lightmap pass's: 1 without one), and
whether it walks every shape, as world_ao_read finds them */
static void world_ao_inputs(struct xgpu_text *text, const struct xgpu_pixel_program *program, int light)
{
	xgpu_text_append(text,
		"\tfloat occlusion = 1.0;\n"
		"\tuint4 world_ao_inputs;\n"
		"\t{\n"
		"\t\tfloat3 world_n = in.xWorldNormal.xyz * rsqrt(max(dot(in.xWorldNormal.xyz, in.xWorldNormal.xyz), 1.0e-24));\n"
		"\t\tfloat value = u.world_ao.y;\n"
		"\t\tuint flags = (uint(int(u.world_ao.x) + 2) & 0xFFu) | %uu;\n"
		/* (a two-sided model shader's cards: found about their most open
		direction) */
		"\t\tif (u.world_ao.w != 0.0)\n"
		"\t\t\tflags |= %uu;\n", XGPU_WORLD_AO_INPUT_PRESENT, XGPU_WORLD_AO_INPUT_CARDS);
	if (world_ao_paged(program, light))
		xgpu_text_append(text,
			"\t\tflags |= %uu;\n"
			"\t\tif (u.world_ao.y < 0.0)\n"
			"\t\t{\n"
			"\t\t\tflags |= %uu;\n"
			"\t\t\tvalue = 1.0;\n"
			"\t\t}\n"
			"\t\telse\n"
			"\t\t{\n"
			"\t\t\tconstexpr sampler page_sampler(filter::linear, address::clamp_to_edge, coord::normalized);\n"
			"\t\t\tfloat2 page = fast::divide(in.xT2.xy, float2(in.xT2.w != 0.0 ? in.xT2.w : 1.0)) * u.texture_scale[2].xy;\n"
			"\t\t\tvalue = world_ao_scenery.sample(page_sampler, page, uint(u.world_ao.y), level(0.0)).a;\n"
			"\t\t}\n", XGPU_WORLD_AO_INPUT_LIGHTMAP, XGPU_WORLD_AO_INPUT_ALL_SHAPES);
	else if (light == XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
		xgpu_text_append(text,
			"\t\tflags |= %uu;\n"
			"\t\tvalue = 1.0;\n", XGPU_WORLD_AO_INPUT_LIGHTMAP | XGPU_WORLD_AO_INPUT_ALL_SHAPES);
	xgpu_text_append(text,
		"\t\tfloat2 folded = world_n.xy / max(abs(world_n.x) + abs(world_n.y) + abs(world_n.z), 1.0e-24);\n"
		"\t\tif (world_n.z < 0.0)\n"
		"\t\t\tfolded = (1.0 - abs(folded.yx)) * select(float2(-1.0), float2(1.0), folded >= 0.0);\n"
		"\t\tworld_ao_inputs = uint4(pack_float_to_snorm2x16(folded), flags | (uint(as_type<ushort>(half(value))) << 16),\n"
		"\t\t\tas_type<uint>(length(in.xWorldPosition - world_ao.grid[0].xyz)), 0u);\n"
		"\t}\n");
}

char *xgpu_msl_pixel_shader(const struct xgpu_pixel_program *program, const struct xgpu_msl_options *options)
{
	struct xgpu_text text = { 0 };
	struct pixel_usage usage;
	BOOL world_position, environment = program->dynamic_lights == XGPU_DYNAMIC_LIGHTS_ENVIRONMENT;
	BOOL count_samples = program->count_samples && options->count_samples;
	BOOL sample_mask = FALSE;
	struct xgpu_text functions = { 0 };
	unsigned long functions_at;
	const char *comparison = program->alpha_test ? comparison_operator(program->alpha_test) : "";
	/* a z-sprite (xgpu_shader.h): its depth, only ever nearer than its
	card's (depth(greater), depth reversed: the depth test before the shader
	keeps what it can) */
	BOOL replaces_depth = xgpu_pixel_program_depth_stage(program) != 0;
	/* (display.hdr: a screen effect's pass, unclamped above; a
	self-illuminated surface's) */
	BOOL unclamped = XGPU_HDR_IS_UNCLAMPED(program->hdr);
	BOOL self_illuminated = XGPU_HDR_IS_SELF_ILLUMINATED(program->hdr) && !program->source_blend;
	/* (display.ambient_occlusion "world": the light the occlusion found in
	the world multiplies, at the pixel's position: xgpu.h; none without
	XGPU_AMBIENT_OCCLUSION_WORLD) */
	int occlusion_light = (program->ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD) ?
		XGPU_AMBIENT_OCCLUSION_LIGHT_OF(program->ambient_occlusion) : XGPU_AMBIENT_OCCLUSION_LIGHT_NONE;
	BOOL world = occlusion_light != XGPU_AMBIENT_OCCLUSION_LIGHT_NONE;
	/* (found once a pixel: read from its texture, or the pre-pass's inputs
	written in the colour's place: xgpu.h) */
	BOOL world_inputs = world && (program->ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS);
	/* (an occluder's twin in the pre-pass: no inputs, where its alpha test
	keeps it) */
	BOOL occluder = !world && (program->ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS);
	BOOL world_sampled = world && !world_inputs && (program->ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED);
	BOOL world_inline = world && !world_inputs && !world_sampled;
	int stage;

	pixel_usage(program, &usage);
	world_position = (program->per_pixel_lighting && (program->per_pixel_lighting == 2 ||
		program->dynamic_lights == XGPU_DYNAMIC_LIGHTS_MODEL)) || environment || world;
	if (comparison && *comparison && program->alpha_test_samples && comparison[0] != '=' && comparison[0] != '!')
		sample_mask = TRUE;
	xgpu_text_append(&text, uniforms_declaration, XGPU_MODEL_LIGHT_COUNT, environment ?
		(world ? "\tfloat4 world_ao;\n\tfloat4 dynamic_light_tint;\n" : "\tfloat4 dynamic_light_spare;\n\tfloat4 dynamic_light_tint;\n") :
		world ? "\tfloat4 world_ao;\n" : "");
	if (world_inline)
		world_ao_functions(&text, occlusion_light);
	else if (world_sampled || world_inputs)
		world_ao_struct(&text);
	if (world_sampled)
		world_ao_closeness(&text);
	xgpu_text_append(&text,
		"struct xgpu_fragment_in\n"
		"{\n"
		"\tfloat4 xD0 [[user(xD0)]];\n"
		"\tfloat4 xD1 [[user(xD1)]];\n"
		"\tfloat4 xT0 [[user(xT0)]];\n"
		"\tfloat4 xT1 [[user(xT1)]];\n"
		"\tfloat4 xT2 [[user(xT2)]];\n"
		"\tfloat4 xT3 [[user(xT3)]];\n"
		"\tfloat xFog [[user(xFog)]];\n");
	if (replaces_depth || world_sampled)
		xgpu_text_append(&text, "\tfloat4 frag_position [[position]];\n");
	if (occlusion_light == XGPU_AMBIENT_OCCLUSION_LIGHT_VERTEX)
		xgpu_text_append(&text, "\tfloat3 xLightAmbient [[user(xLightAmbient)]];\n\tfloat3 xLightPoint [[user(xLightPoint)]];\n");
	if (program->per_pixel_lighting || environment || world)
		xgpu_text_append(&text, "\tfloat4 xWorldNormal [[user(xWorldNormal)]];\n");
	if (world_position)
		xgpu_text_append(&text, "\tfloat3 xWorldPosition [[user(xWorldPosition)]];\n");
	if (environment)
		xgpu_text_append(&text, "\tfloat3 xWorldBinormal [[user(xWorldBinormal)]];\n\tfloat3 xWorldTangent [[user(xWorldTangent)]];\n");
	xgpu_text_append(&text, "};\nstruct xgpu_fragment_out\n{\n\t%s color [[color(0)]];\n",
		world_inputs || occluder ? "uint4" : "float4");
	if (sample_mask)
		xgpu_text_append(&text, "\tuint sample_mask [[sample_mask]];\n");
	if (replaces_depth)
		xgpu_text_append(&text, "\tfloat depth [[depth(greater)]];\n");
	xgpu_text_append(&text, "};\n");
	/* (the functions the shader calls: put in before it at the end) */
	if (program->dynamic_lights)
		dynamic_lights_functions(&text, &functions);
	if (program->per_pixel_lighting)
		model_lighting(&functions, program->per_pixel_lighting == 2, program->dynamic_lights == XGPU_DYNAMIC_LIGHTS_MODEL,
			occlusion_light == XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL);
	xgpu_text_append(&functions, "%s%s", pixel_functions, border_function);
	functions_at = text.length;

	/* samples that pass the depth and stencil tests, as the NV2A's
	occlusion counter, where the backend counts in the shader */
	xgpu_text_append(&text, "%sfragment xgpu_fragment_out xgpu_fragment(xgpu_fragment_in in [[stage_in]],\n"
		"\tconstant xgpu_uniforms &u [[buffer(%d)]]", count_samples ? "[[early_fragment_tests]]\n" : "",
		XGPU_MSL_BUFFER_UNIFORMS);
	for (stage = 0; stage < 4; stage++)
	{
		if (stage_samples(program->stages[stage].mode))
			xgpu_text_append(&text, ",\n\t%s tex%d [[texture(%d)]], sampler tex%d_sampler [[sampler(%d)]]",
				texture_declaration(program->stages[stage].sampler), stage, stage, stage, stage);
	}
	if (program->dynamic_lights)
		xgpu_text_append(&text, ",\n\tconstant xgpu_dynamic_lights &dynamic_lights [[buffer(%d)]], "
			"const device uint4 *dynamic_light_cells [[buffer(%d)]], "
			"const device uint4 *dynamic_light_world [[buffer(%d)]]", XGPU_MSL_BUFFER_DYNAMIC_LIGHTS,
			XGPU_MSL_BUFFER_DYNAMIC_LIGHT_CELLS, XGPU_MSL_BUFFER_DYNAMIC_LIGHT_WORLD);
	if (count_samples)
		xgpu_text_append(&text, ",\n\tdevice atomic_uint *visible_samples [[buffer(%d)]]", XGPU_MSL_BUFFER_SAMPLES);

	if (world_sampled)
		xgpu_text_append(&text, ",\n\ttexture2d<float> world_ao_pixels [[texture(%d)]], constant xgpu_world_ao &world_ao "
			"[[buffer(%d)]]", XGPU_MSL_TEXTURE_WORLD_AO_PIXELS, XGPU_MSL_BUFFER_WORLD_AO);
	else if (world_inputs)
	{
		xgpu_text_append(&text, ",\n\tconstant xgpu_world_ao &world_ao [[buffer(%d)]]", XGPU_MSL_BUFFER_WORLD_AO);
		if (world_ao_paged(program, occlusion_light))
			xgpu_text_append(&text, ", texture2d_array<float> world_ao_scenery [[texture(%d)]]",
				XGPU_MSL_TEXTURE_WORLD_AO_SCENERY);
	}
	if (world_inline)
	{
		xgpu_text_append(&text, ",\n\tconstant xgpu_world_ao &world_ao [[buffer(%d)]], texture3d<float> world_ao_objects "
			"[[texture(%d)]]", XGPU_MSL_BUFFER_WORLD_AO, XGPU_MSL_TEXTURE_WORLD_AO_OBJECTS);
		if (occlusion_light != XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
			xgpu_text_append(&text, ", texture3d<float> world_sdf [[texture(%d)]], texture3d<float> world_sdf_table "
				"[[texture(%d)]]", XGPU_MSL_TEXTURE_WORLD_SDF, XGPU_MSL_TEXTURE_WORLD_SDF_TABLE);
		else if (world_ao_paged(program, occlusion_light))
			xgpu_text_append(&text, ", texture2d_array<float> world_ao_scenery [[texture(%d)]]",
				XGPU_MSL_TEXTURE_WORLD_AO_SCENERY);
	}
	/* (display.hdr: a draw blended here reads the pixel it draws over:
	Apple's GPUs' programmable blending) */
	if (XGPU_HDR_SHADER_BLENDS(program->hdr))
		xgpu_text_append(&text, ",\n\tfloat4 xgpu_destination [[color(0)]]");
	xgpu_text_append(&text, ")\n{\n\txgpu_fragment_out output;\n");

	if (register_read(&usage, XGPU_REGISTER_V0))
		xgpu_text_append(&text, "\tfloat4 v0 = in.xD0;\n");
	if (register_read(&usage, XGPU_REGISTER_V1))
		xgpu_text_append(&text, "\tfloat4 v1 = in.xD1;\n");
	for (stage = 0; stage < 4; stage++)
	{
		if (register_read(&usage, XGPU_REGISTER_T0 + stage))
			xgpu_text_append(&text, "\tfloat4 t%d = float4(0.0);\n", stage);
	}
	for (stage = 0; stage < 4; stage++)
	{
		if (usage.dots[stage])
			xgpu_text_append(&text, "\tfloat dot%d = 0.0;\n", stage);
	}
	if (replaces_depth)
		xgpu_text_append(&text, "\tfloat replaced_depth = 1.0;\n");
	/* display.ambient_occlusion "world": the occlusion at the pixel, which
	multiplies the ambient light alone (xgpu.h) */
	if (world_inline)
		world_ao_read(&text, program, occlusion_light);
	else if (world_sampled)
		world_ao_upsample(&text);
	else if (world_inputs)
		world_ao_inputs(&text, program, occlusion_light);
	if (register_read(&usage, XGPU_REGISTER_V0) && program->per_pixel_lighting &&
		program->dynamic_lights == XGPU_DYNAMIC_LIGHTS_MODEL)
	{
		xgpu_text_append(&text,
			"\t{\n"
			"\t\tfloat3 n = in.xWorldNormal.xyz * rsqrt(max(dot(in.xWorldNormal.xyz, in.xWorldNormal.xyz), 1.0e-24));\n"
			"\t\tv0.rgb = dynamic_light_add(dynamic_lights, model_lighting(u, in.xWorldNormal%s),\n"
			"\t\t\tdynamic_light_sum(dynamic_lights, dynamic_light_cells, dynamic_light_world, in.xWorldPosition, n, n, false,\n"
			"\t\t\t\tu.model_lights[%d]));\n"
			"\t}\n", occlusion_light == XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL ? ", occlusion" : "",
			XGPU_MODEL_LIGHT_OWN_LIGHTS);
	}
	else if (register_read(&usage, XGPU_REGISTER_V0) && program->per_pixel_lighting)
	{
		xgpu_text_append(&text, "\tv0.rgb = model_lighting(u, in.xWorldNormal%s%s);\n",
			program->per_pixel_lighting == 2 ? ", in.xWorldPosition" : "",
			occlusion_light == XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL ? ", occlusion" : "");
	}
	else if (register_read(&usage, XGPU_REGISTER_V0) && occlusion_light == XGPU_AMBIENT_OCCLUSION_LIGHT_VERTEX)
	{
		/* (lit for each vertex: its ambient light by the occlusion, its
		point lights' added, clamped as its program clamps the sum) */
		xgpu_text_append(&text, "\tv0.rgb = saturate(in.xLightAmbient * occlusion + in.xLightPoint);\n");
	}

	for (stage = 0; stage < 4; stage++)
		texture_stage(&text, program, stage, &usage, options);
	/* (the bump map's texel, which the combiners may overwrite: the enhanced
	dynamic lights read it after them) */
	if (environment && program->stages[0].sampler == _xgpu_sampler_2d)
		xgpu_text_append(&text, "\tfloat3 dynamic_light_bump = t0.rgb;\n");

	/* the fog register: rgb is the fog color, alpha the fog factor */
	if (register_read(&usage, XGPU_REGISTER_FOG))
	{
		if (!program->fog)
			xgpu_text_append(&text, "\tfloat fog_factor = 1.0;\n");
		else if (program->fog_mode == D3DFOG_EXP)
			xgpu_text_append(&text, "\tfloat fog_factor = exp(-u.fog_parameters.z * in.xFog);\n");
		else if (program->fog_mode == D3DFOG_EXP2)
			xgpu_text_append(&text, "\tfloat fog_factor = exp(-(u.fog_parameters.z * in.xFog) * (u.fog_parameters.z * in.xFog));\n");
		else if (program->fog_mode == D3DFOG_LINEAR)
			xgpu_text_append(&text, "\tfloat fog_factor = fast::divide(u.fog_parameters.y - in.xFog, "
				"max(u.fog_parameters.y - u.fog_parameters.x, 1.0e-6));\n");
		else
			xgpu_text_append(&text, "\tfloat fog_factor = in.xFog;\n");
		xgpu_text_append(&text, "\tfloat4 fog = float4(u.fog_color.rgb, saturate(fog_factor));\n");
	}
	if (register_read(&usage, XGPU_REGISTER_R0))
		xgpu_text_append(&text, "\tfloat4 r0 = float4(0.0, 0.0, 0.0, t0.a);\n");
	if (register_read(&usage, XGPU_REGISTER_R1))
		xgpu_text_append(&text, "\tfloat4 r1 = float4(0.0);\n");

	/* (display.hdr: a self-illuminated surface's pixel without its
	self-illumination, the registers as they were after) */
	if (self_illuminated)
	{
		static const unsigned long saved[] =
		{
			XGPU_REGISTER_V0, XGPU_REGISTER_V1, XGPU_REGISTER_T0, XGPU_REGISTER_T1, XGPU_REGISTER_T2, XGPU_REGISTER_T3,
			XGPU_REGISTER_R0, XGPU_REGISTER_R1,
		};
		unsigned int index;

		xgpu_text_append(&text, "\tfloat3 self_illumination_base;\n\t{\n");
		for (index = 0; index < sizeof(saved) / sizeof(saved[0]); index++)
		{
			if (register_read(&usage, saved[index]))
				xgpu_text_append(&text, "\tfloat4 saved_%s = %s;\n", destination_name(saved[index], &usage),
					destination_name(saved[index], &usage));
		}
		combiners(&text, program, &usage, unclamped, XGPU_HDR_SELF_ILLUMINATION_STAGES(program->hdr));
		xgpu_text_append(&text, "\tself_illumination_base = saturate(result.rgb);\n");
		for (index = 0; index < sizeof(saved) / sizeof(saved[0]); index++)
		{
			if (register_read(&usage, saved[index]))
				xgpu_text_append(&text, "\t%s = saved_%s;\n", destination_name(saved[index], &usage),
					destination_name(saved[index], &usage));
		}
		xgpu_text_append(&text, "\t}\n");
	}
	combiners(&text, program, &usage, unclamped, 0);
	/* (... and the light its self-illumination adds past white, toward its
	ceiling where it is bright, as a glow's: xgpu_hdr_self_illuminate) */
	if (self_illuminated)
		xgpu_text_append(&text,
			"\tfloat3 self_illumination = float3(0.0);\n"
			"\t{\n"
			"\t\tfloat3 lit = saturate(result.rgb), added = max(lit - self_illumination_base, float3(0.0));\n"
			"\t\tfloat added_most = max(added.r, max(added.g, added.b)), most = max(lit.r, max(lit.g, lit.b));\n"
			"\t\tif (added_most > 0.0)\n"
			"\t\t\tself_illumination = added * min(%.6f * smoothstep(%.6f, 1.0, most) * most / added_most, %.6f);\n"
			"\t}\n", XGPU_HDR_SELF_ILLUMINATION_VALUE - 1.0f, XGPU_HDR_SELF_ILLUMINATION_KNEE, XGPU_HDR_EXPAND_MOST);
	/* (display.ambient_occlusion: a lightmap pass's light, by the
	occlusion, before the enhanced dynamic lights add theirs) */
	if (occlusion_light == XGPU_AMBIENT_OCCLUSION_LIGHT_LIGHTMAP)
		xgpu_text_append(&text, "\tresult.rgb *= occlusion;\n");

	if (environment)
	{
		/* the lightmap pass's light, before the textures multiply it
		(xgpu_spirv.c) */
		xgpu_text_append(&text,
			"\t{\n"
			"\t\tfloat3 surface_normal = normalize(in.xWorldNormal.xyz);\n"
			"\t\tfloat3 bump = %s;\n"
			"\t\tfloat3 n = normalize(in.xWorldTangent * bump.x + in.xWorldBinormal * bump.y + surface_normal * max(bump.z, 0.05));\n"
			"\t\tresult.rgb = dynamic_light_add%s(dynamic_lights, result.rgb,\n"
			"\t\t\tdynamic_light_sum(dynamic_lights, dynamic_light_cells, dynamic_light_world, in.xWorldPosition, n, surface_normal, true,\n"
			"\t\t\t\tfloat4(-1.0)) *\n"
			"\t\t\tu.dynamic_light_tint.rgb);\n"
			"\t}\n",
			program->stages[0].sampler == _xgpu_sampler_2d ? "dynamic_light_bump * 2.0 - 1.0" : "float3(0.0, 0.0, 1.0)",
			(program->hdr & XGPU_HDR_LIGHTS) ? "_hdr" : "");
	}
	if (program->coverage_alpha)
		xgpu_text_append(&text, "\tresult.a = mix(1.0, result.a, t0.g);\n");
	if (program->alpha_test)
	{
		if (!comparison)
		{
			xgpu_text_append(&text, "\tdiscard_fragment();\n");
		}
		else if (sample_mask)
		{
			/* multisampled: the samples covered as alpha passes the reference
			over the pixel (xgpu_spirv.c) */
			xgpu_text_append(&text,
				"\tfloat test_alpha = saturate(result.a) * 255.0;\n"
				"\tfloat test_coverage = saturate(%s(test_alpha - u.alpha_reference) / max(fwidth(test_alpha), 1.0) + 0.5);\n"
				"\tint test_samples = int(test_coverage * %d.0 + 0.5);\n"
				"\tif (test_samples == 0) discard_fragment();\n"
				"\toutput.sample_mask = (1u << uint(test_samples)) - 1u;\n",
				comparison[0] == '<' ? "-" : "", (int)program->alpha_test_samples);
		}
		else if (*comparison)
		{
			xgpu_text_append(&text, "\tif (!(floor(saturate(result.a) * 255.0 + 0.5) %s u.alpha_reference)) discard_fragment();\n",
				comparison);
		}
	}
	if (options->debug_expression && *options->debug_expression)
		xgpu_text_append(&text, "\tresult = float4(float3(%s), 1.0);\n", options->debug_expression);
	if (options->debug_texture0 && register_read(&usage, XGPU_REGISTER_T0))
		xgpu_text_append(&text, "\tresult = float4(t0.rgb, 1.0);\n");
	if (options->debug_flat)
		xgpu_text_append(&text, "\tresult = in.xD0.a > 0.0 ? float4(in.xD0.rgb, 1.0) : float4(1.0, 0.0, 1.0, 1.0);\n");
	/* (debug.world_ao_view: the occlusion found in the world, in grey) */
	if (world && (program->ambient_occlusion & XGPU_AMBIENT_OCCLUSION_WORLD_VIEW))
		xgpu_text_append(&text, "\tresult = float4(float3(occlusion), 1.0);\n");
	if (count_samples)
		xgpu_text_append(&text, "\tatomic_fetch_add_explicit(visible_samples, 1u, memory_order_relaxed);\n");
	/* (the pre-pass's twin: its inputs in its colour's place, where its
	alpha test keeps it) */
	if (world_inputs || occluder)
	{
		xgpu_text_append(&text, "\t(void)result;\n");
		if (occluder)
			xgpu_text_append(&text, "\tuint4 world_ao_inputs = uint4(0u);\n");
		if (replaces_depth)
			xgpu_text_append(&text, "\toutput.depth = max(1.0 - replaced_depth, in.frag_position.z);\n");
		xgpu_text_append(&text, "\toutput.color = world_ao_inputs;\n\treturn output;\n}\n");
		functions_insert(&text, functions_at, functions.buffer);
		free(functions.buffer);
		return text.buffer;
	}
	/* (no farther than the card: depth(greater)'s promise, on reversed
	depth: xgpu_backend.h) */
	if (replaces_depth)
		xgpu_text_append(&text, "\toutput.depth = max(1.0 - replaced_depth, in.frag_position.z);\n");
	if (program->source_blend)
	{
		static const char *const factors[] =
		{
			"u.blend_constant", "(1.0 - u.blend_constant)", "float4(u.blend_constant.a)", "float4(1.0 - u.blend_constant.a)"
		};

		xgpu_text_append(&text, "\toutput.color = saturate(result) * %s;\n", factors[(program->source_blend - 1) & 3]);
	}
	else if (program->hdr & XGPU_HDR_LIGHTS)
	{
		/* (display.hdr: the dynamic lights' light past white, to their white
		in the display's gamma: xgpu_hdr.h) */
		xgpu_text_append(&text, "\toutput.color = float4(clamp(result.rgb, 0.0, %.6f)%s, saturate(result.a));\n",
			XGPU_HDR_LIGHTS_WHITE_VALUE, self_illuminated ? " + self_illumination" : "");
	}
	else if (unclamped)
	{
		/* (display.hdr: a screen effect's pass, the view's light past white
		kept) */
		xgpu_text_append(&text, "\toutput.color = float4(max(result.rgb, 0.0), saturate(result.a));\n");
	}
	else if (self_illuminated)
	{
		xgpu_text_append(&text, "\toutput.color = float4(saturate(result.rgb) + self_illumination, saturate(result.a));\n");
	}
	else if (program->hdr)
	{
		xgpu_text_append(&text, "\tfloat4 source = saturate(result);\n");
		hdr_output(&text, program->hdr);
	}
	else
	{
		xgpu_text_append(&text, "\toutput.color = saturate(result);\n");
	}
	/* (finite: the NV2A wrote no colour that is not a number, nor an
	infinite one; by its bits, which Metal's fast arithmetic keeps) */
	xgpu_text_append(&text, "\toutput.color = nv2a_finite(output.color);\n");
	xgpu_text_append(&text, "\treturn output;\n}\n");
	functions_insert(&text, functions_at, functions.buffer);
	free(functions.buffer);
	return text.buffer;
}

/* the glow about each plasma bolt's light (xgpu_msl.h), the SPIR-V
emitter's (xgpu_spirv.c) for the screen's target, operation for
operation: the light's quad facing the camera,
projected as the game's vertex programs project (the screen conversion
undone, as the emitter's vertex shaders undo it: Metal's clip space is the
game's), and in the pixel shader a wide soft glow and a small hot core,
fading out as it nears what is behind it and gone behind what is before it */
char *xgpu_msl_halo_shader(BOOL pixel)
{
	struct xgpu_text text = { 0 };

	xgpu_text_append(&text,
		"#include <metal_stdlib>\n"
		"using namespace metal;\n"
		"struct xgpu_halo_uniforms\n"
		"{\n"
		"\tfloat4 c[6];\n"
		"\tfloat4 camera_axes[2];\n"
		"\tfloat4 viewport_scale;\n"
		"\tfloat4 viewport_offset;\n"
		"\tfloat4 depth_parameters;\n"
		"\tfloat screen_offset, unused0, unused1, unused2;\n"
		"};\n"
		"struct xgpu_halo_in\n"
		"{\n"
		"\tfloat4 middle [[attribute(%d)]];\n"
		"\tfloat4 color [[attribute(%d)]];\n"
		"\tfloat2 corner [[attribute(%d)]];\n"
		"};\n"
		"struct xgpu_halo_out\n"
		"{\n"
		"\tfloat4 position [[position]];\n"
		"\tfloat2 corner;\n"
		"\tfloat3 color;\n"
		"\tfloat depth;\n"
		"};\n",
		XGPU_MSL_HALO_MIDDLE, XGPU_MSL_HALO_COLOR, XGPU_MSL_HALO_CORNER);
	if (!pixel)
		xgpu_text_append(&text,
			"vertex xgpu_halo_out xgpu_halo_vertex(xgpu_halo_in in [[stage_in]],\n"
			"\tconstant xgpu_halo_uniforms &u [[buffer(%d)]])\n"
			"{\n"
			"\txgpu_halo_out out;\n"
			"\tout.corner = in.corner;\n"
			"\tout.color = in.color.rgb;\n"
			"\tfloat3 world = in.middle.xyz + (u.camera_axes[0].xyz * in.corner.x + u.camera_axes[1].xyz * in.corner.y) *\n"
			"\t\tin.middle.w;\n"
			"\tfloat4 clip = float4(dot(world, u.c[0].xyz) + u.c[0].w, dot(world, u.c[1].xyz) + u.c[1].w,\n"
			"\t\tdot(world, u.c[2].xyz) + u.c[2].w, dot(world, u.c[3].xyz) + u.c[3].w);\n"
			"\tout.depth = clip.w;\n"
			"\tfloat3 scale = float3(u.viewport_scale.x != 0.0 ? u.viewport_scale.x : 1.0,\n"
			"\t\tu.viewport_scale.y != 0.0 ? u.viewport_scale.y : 1.0, u.viewport_scale.z != 0.0 ? u.viewport_scale.z : 1.0);\n"
			"\tout.position = float4((clip.xyz * u.c[4].xyz + (u.c[5].xyz + float3(0.5 + u.screen_offset, 0.5, 0.0) -\n"
			"\t\tu.viewport_offset.xyz) * clip.w) / scale, clip.w);\n"
			"\treturn out;\n"
			"}\n", XGPU_MSL_BUFFER_UNIFORMS);
	else
		xgpu_text_append(&text,
			"fragment float4 xgpu_halo_fragment(xgpu_halo_out in [[stage_in]], constant xgpu_halo_uniforms &u [[buffer(%d)]],\n"
			"\tdepth2d<float> scene_depth [[texture(0)]])\n"
			"{\n"
			"\tfloat r2 = dot(in.corner, in.corner);\n"
			"\tif (r2 >= 1.0)\n"
			"\t\tdiscard_fragment();\n"
			"\tfloat scene = 1.0 - scene_depth.read(uint2(in.position.xy));\n"
			"\tfloat away = scene - u.depth_parameters.x;\n"
			"\tfloat distance = away * u.depth_parameters.y > 0.0 ? u.depth_parameters.y / away : 1.0e9;\n"
			"\tfloat fade = scene >= 1.0 ? 1.0 : saturate((distance - in.depth) / u.depth_parameters.z);\n"
			"\tfloat glow = (exp(-3.0 * r2) - 0.0498) * 0.6 + exp(-24.0 * r2) * 0.8;\n"
			"\treturn float4(in.color * (glow * fade * fade), 0.0);\n"
			"}\n", XGPU_MSL_BUFFER_UNIFORMS);
	return text.buffer;
}

/* ---------- display.ambient_occlusion "world" found once a pixel

The passes after the pre-pass (xgpu_msl.h's xgpu_msl_world_ao_pass;
xgpu_spirv.c's world_ao_pass_*, operation for operation;
world_ao_shading.c's world_ao_pass_position, world_ao_upsample,
world_ao_inputs_unpack and world_ao_pass_occlusion, their CPU reference). */

char *xgpu_msl_world_ao_pass(BOOL pixel)
{
	struct xgpu_text text = { 0 };

	xgpu_text_append(&text, "#include <metal_stdlib>\nusing namespace metal;\n");
	if (!pixel)
	{
		xgpu_text_append(&text,
			"vertex float4 xgpu_world_ao_vertex(uint vertex_id [[vertex_id]])\n"
			"{\n"
			"\tfloat2 corner = float2(float((vertex_id << 1) & 2u), float(vertex_id & 2u));\n"
			"\treturn float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
			"}\n");
		return text.buffer;
	}
	world_ao_functions(&text, XGPU_AMBIENT_OCCLUSION_LIGHT_MODEL);
	xgpu_msl_world_trace(&text);
	xgpu_text_append(&text,
		"struct xgpu_world_ao_pass\n"
		"{\n"
		"\tfloat4 camera[4];\n"
		"\tint4 corners;\n"
		"\tfloat4 cascades[%d];\n"
		"\tfloat4 previous[4];\n"
		"\tint4 previous_corners;\n"
		"\tfloat4 history;\n"
		"};\n", XGPU_WORLD_CASCADES);
	xgpu_text_append(&text,
		/* a pixel's world position: its distance from the eye along the ray
		through its centre */
		"static inline float3 world_ao_pass_position(constant xgpu_world_ao_pass &pass, constant xgpu_world_ao &world_ao,\n"
		"\tfloat2 at, float distance)\n"
		"{\n"
		"\tfloat2 screen = 2.0 * (at - float2(pass.corners.xy)) / float2(pass.corners.zw - pass.corners.xy) - 1.0;\n"
		"\treturn world_ao.grid[0].xyz + distance * normalize(pass.camera[1].xyz + screen.x * pass.camera[2].xyz +\n"
		"\t\tscreen.y * pass.camera[3].xyz);\n"
		"}\n");
	xgpu_text_append(&text,
		/* the occlusion at a point from a pixel's inputs: its static term (a
		model's field relative to its reference; the lightmap pass's page's
		texel, from the pre-pass) times the shapes of its cell (the dynamic
		objects', or every one), then the floor and the strength: one call a
		pixel (world_ao_shading.c's world_ao_pass_occlusion) */
		"static inline float3 world_ao_pass_unfold(float2 folded)\n"
		"{\n"
		"\tfloat z = 1.0 - abs(folded.x) - abs(folded.y), t = max(-z, 0.0);\n"
		"\tfolded += select(float2(t), float2(-t), folded >= 0.0);\n"
		"\treturn normalize(float3(folded, z));\n"
		"}\n"
		/* the share of a point's cosine-weighted hemisphere the dynamic
		objects block, found by a few rays through the distance-field
		scene's objects (world_ao_shading.c's world_ao_traced): each
		cosine-weighted about the normal (a frame of it, Duff's; the rays
		stratified, turned for each pixel and frame), from a little off the
		surface, to the AO's reach; a hit counts as 1 - (t / reach)^2 */
		"static inline float world_ao_traced(texture3d<float> world_cascades, constant float4 *cascades, float3 p,\n"
		"\tfloat3 n, uint2 pixel, float frame)\n"
		"{\n"
		"\tfloat s = n.z >= 0.0 ? 1.0 : -1.0;\n"
		"\tfloat a = -1.0 / (s + n.z), b = n.x * n.y * a;\n"
		"\tfloat3 x = float3(1.0 + s * n.x * n.x * a, s * b, -s * n.x), y = float3(b, s + n.y * n.y * a, -n.y);\n"
		"\tfloat turn = fract(52.9829189 * fract(0.06711056 * float(pixel.x) + 0.00583715 * float(pixel.y)) +\n"
		"\t\t0.618034 * frame);\n"
		"\tfloat twist = fract(52.9829189 * fract(0.00583715 * float(pixel.x) + 0.06711056 * float(pixel.y)) +\n"
		"\t\t0.7548777 * frame);\n"
		"\tfloat occlusion = 0.0;\n"
		/* (none where no object is within reach: the objects' distance
		there, in a cascade whose range holds the reach) */
		"\tint probe = world_cascade_find(cascades, p, %d);\n"
		"\tif (probe < 0 || world_cascade_sample(world_cascades, cascades[probe], probe, p).y >= %.6f)\n"
		"\t\treturn 0.0;\n"
		"\tfor (int ray = 0; ray < %d; ray++)\n"
		"\t{\n"
		"\t\tfloat u = (float(ray) + turn) / %d.0, angle = 6.28318531 * fract(twist + 0.618034 * float(ray));\n"
		"\t\tfloat radius = sqrt(u);\n"
		"\t\tfloat3 d = x * (radius * cos(angle)) + y * (radius * sin(angle)) + n * sqrt(max(1.0 - u, 0.0));\n"
		"\t\tworld_trace_hit hit = world_trace(world_cascades, cascades, p + n * %.6f, d, %.6f, %d);\n"
		"\t\tif (hit.hit)\n"
		"\t\t{\n"
		"\t\t\tfloat f = hit.t / %.6f;\n"
		"\t\t\tocclusion += 1.0 - f * f;\n"
		"\t\t}\n"
		"\t}\n"
		"\treturn occlusion / %d.0;\n"
		"}\n"
		"static inline float world_ao_evaluate(constant xgpu_world_ao &world_ao, texture3d<float> world_sdf,\n"
		"\ttexture3d<float> world_sdf_table, texture3d<float> world_ao_objects, texture3d<float> world_cascades,\n"
		"\tconstant xgpu_world_ao_pass &pass, float3 p, uint2 words, uint2 pixel)\n"
		"{\n"
		"\tfloat3 n = world_ao_pass_unfold(unpack_snorm2x16_to_float(words.x));\n"
		"\tfloat owner = float(int(words.y & 0xFFu)) - 2.0;\n"
		"\tfloat value = float(as_type<half>(ushort(words.y >> 16)));\n"
		"\tint limit = (words.y & %uu) != 0u ? %d : int(world_ao.parameters[1].y);\n"
		"\tbool lightmap = (words.y & %uu) != 0u, baked = value < 0.0;\n"
		/* (a model's field: from the pixel's own depth in it where its
		reference is negative, a static scenery placement's own draw; a
		two-sided shader's cards about their most open direction, the shapes
		and rays too) */
		"\tfloat field = 1.0;\n"
		"\tif (!lightmap)\n"
		"\t{\n"
		"\t\tfloat inside = min(world_sdf_distance(world_ao, world_sdf, world_sdf_table, p), 0.0);\n"
		"\t\tfield = world_sdf_cards_occlusion(world_ao, world_sdf, world_sdf_table, p, n, baked ? inside : 0.0,\n"
		"\t\t\t(words.y & %uu) != 0u);\n"
		"\t}\n"
		/* (the dynamic objects' shapes by the rays, their occlusion fields
		and the static scenery's shapes walked) */
		"\tbool traced = pass.history.w != 0.0;\n"
		"\tfloat proxies = world_proxies_occlusion(world_ao, world_ao_objects, p, n, owner, limit,\n"
		"\t\ttraced ? int(world_ao.parameters[1].y) : 0);\n"
		"\tif (traced)\n"
		"\t\tproxies *= 1.0 - world_ao_traced(world_cascades, pass.cascades, p, n, pixel, pass.history.y);\n"
		"\tif (lightmap)\n"
		"\t\treturn mix(1.0, max(proxies * value, %.6f), world_ao.parameters[0].y);\n"
		"\tfield = mix(field, saturate(field / max(baked ? 1.0 : value, %.6f)), world_ao.parameters[1].x);\n"
		"\treturn mix(1.0, max(mix(1.0, field, world_ao.parameters[0].z) * proxies, %.6f), world_ao.parameters[0].x);\n"
		"}\n",
		XGPU_WORLD_AO_RAY_CASCADE, XGPU_WORLD_AO_RAY_REACH,
		XGPU_WORLD_AO_RAYS, XGPU_WORLD_AO_RAYS, XGPU_WORLD_AO_RAY_BIAS, XGPU_WORLD_AO_RAY_REACH, XGPU_WORLD_TRACE_OBJECTS,
		XGPU_WORLD_AO_RAY_REACH, XGPU_WORLD_AO_RAYS,
		XGPU_WORLD_AO_INPUT_ALL_SHAPES, XGPU_WORLD_AO_SHAPES, XGPU_WORLD_AO_INPUT_LIGHTMAP, XGPU_WORLD_AO_INPUT_CARDS,
		XGPU_WORLD_AO_FLOOR, XGPU_WORLD_AO_REFERENCE_LEAST, XGPU_WORLD_AO_FLOOR);
	xgpu_text_append(&text,
		"fragment float4 xgpu_world_ao_pass(float4 position [[position]], constant xgpu_world_ao_pass &pass [[buffer(%d)]],\n"
		"\tconstant xgpu_world_ao &world_ao [[buffer(%d)]], texture2d<uint> inputs [[texture(0)]],\n"
		"\ttexture3d<float> world_sdf [[texture(%d)]], texture3d<float> world_sdf_table [[texture(%d)]],\n"
		"\ttexture3d<float> world_ao_objects [[texture(%d)]], texture2d<float> history [[texture(%d)]],\n"
		"\ttexture3d<float> world_cascades [[texture(%d)]])\n"
		"{\n"
		/* (its block's nearest pixel drawn: found at a quarter of the
		pixels, each 2x2 block's, pass.camera[3].w 2; else each pixel's) */
		"\tint scale = max(int(pass.camera[3].w), 1);\n"
		"\tuint2 pixel = uint2(0u), words = uint2(0u);\n"
		"\tfloat nearest = INFINITY;\n"
		"\tfor (int j = 0; j < scale; j++)\n"
		"\t\tfor (int i = 0; i < scale; i++)\n"
		"\t\t{\n"
		"\t\t\tint2 at = int2(position.xy) * scale + int2(i, j);\n"
		"\t\t\tif (any(at < pass.corners.xy) || any(at >= pass.corners.zw))\n"
		"\t\t\t\tcontinue;\n"
		"\t\t\tuint4 read = inputs.read(uint2(at));\n"
		"\t\t\tfloat d = as_type<float>(read.z);\n"
		"\t\t\tif ((read.y & %uu) != 0u && d < nearest)\n"
		"\t\t\t{\n"
		"\t\t\t\tnearest = d;\n"
		"\t\t\t\tpixel = uint2(at);\n"
		"\t\t\t\twords = read.xy;\n"
		"\t\t\t}\n"
		"\t\t}\n"
		"\tif (nearest == INFINITY)\n"
		"\t\treturn float4(1.0, 0.0, 0.0, 0.0);\n"
		"\tfloat3 p = world_ao_pass_position(pass, world_ao, float2(pixel) + 0.5, nearest);\n"
		"\tfloat2 packed = unpack_snorm2x16_to_float(words.x);\n"
		"\tfloat occlusion = world_ao_evaluate(world_ao, world_sdf, world_sdf_table, world_ao_objects, world_cascades, pass,\n"
		"\t\tp, words, pixel);\n"
		/* the previous frame's occlusion at the same point, where it is the
		same surface (its distance from that frame's eye within a share of
		it, its normal near), blended with this frame's (temporal
		accumulation: the rays' noise averaged over frames) */
		"\tif (pass.history.x != 0.0)\n"
		"\t{\n"
		"\t\tfloat3 v = p - pass.previous[0].xyz, f = pass.previous[1].xyz, r = pass.previous[2].xyz, u = pass.previous[3].xyz;\n"
		"\t\tfloat along = dot(v, f) / dot(f, f);\n"
		"\t\tif (along > 0.0)\n"
		"\t\t{\n"
		"\t\t\tfloat2 screen = float2(dot(v, r) / dot(r, r), dot(v, u) / dot(u, u)) / along;\n"
		"\t\t\tfloat2 at = float2(pass.previous_corners.xy) + (screen + 1.0) * 0.5 *\n"
		"\t\t\t\tfloat2(pass.previous_corners.zw - pass.previous_corners.xy);\n"
		"\t\t\tint2 texel = int2(floor(at / float(scale)));\n"
		"\t\t\tif (all(at >= float2(pass.previous_corners.xy)) && all(at < float2(pass.previous_corners.zw)) &&\n"
		"\t\t\t\ttexel.x < int(history.get_width()) && texel.y < int(history.get_height()))\n"
		"\t\t\t{\n"
		"\t\t\t\tfloat4 old = history.read(uint2(texel));\n"
		"\t\t\t\tfloat distance = length(v);\n"
		"\t\t\t\tif (old.y > 0.0 && abs(old.y - distance) <= %.6f * distance &&\n"
		"\t\t\t\t\tdot(world_ao_pass_unfold(old.zw), world_ao_pass_unfold(packed)) >= %.6f)\n"
		"\t\t\t\t\tocclusion = mix(old.x, occlusion, pass.history.z);\n"
		"\t\t\t}\n"
		"\t\t}\n"
		"\t}\n"
		"\treturn float4(occlusion, nearest, packed);\n"
		"}\n",
		XGPU_MSL_BUFFER_UNIFORMS, XGPU_MSL_BUFFER_WORLD_AO, XGPU_MSL_TEXTURE_WORLD_SDF, XGPU_MSL_TEXTURE_WORLD_SDF_TABLE,
		XGPU_MSL_TEXTURE_WORLD_AO_OBJECTS, XGPU_MSL_WORLD_AO_PASS_HISTORY, XGPU_MSL_WORLD_AO_PASS_CASCADES,
		XGPU_WORLD_AO_INPUT_PRESENT, XGPU_WORLD_AO_HISTORY_DISTANCE, XGPU_WORLD_AO_HISTORY_NORMAL);
	return text.buffer;
}
