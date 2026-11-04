/*
CHECK.C

Reversed depth's checks (port/linux/src/xgpu_backend.h, "Reversed depth";
tools/test_reversed_depth.py):

  check programs <capture dir>   the game's vertex programs (vsNNN.bin, as
                                 msl_corpus reads them): which have the
                                 projection's rows (xgpu_vertex_program_depth_rows),
                                 and that the Metal emitter's shader of each
                                 computes its depth from them
  check precision                a light 1.5 mm in front of its wall, seen
                                 from 100 m through the sniper rifle's zoom,
                                 in the world's coordinates (Blood Gulch's
                                 bases are 100 m and more from its origin):
                                 the points of the light and of the wall
                                 behind it each pixel's ray meets, their depth
                                 as the emitters write it, the game's (z / w)
                                 and reversed (from the rows); how many of the
                                 light's points pass the depth test
  check precision <far> <distance> <gap>
                                 the same with another far plane
                                 (display.draw_distance's, port/linux/game/
                                 far_clip.c), the wall that far away and the
                                 light that much in front of it (world units)

Built by tools/test_reversed_depth.py with the shader decoders and the
Metal emitter, as tools/msl_corpus is, the arithmetic of the shaders done
in float, uncontracted (as the backends compile them: precise, invariant).
*/

#include "xgpu.h"
#include "xgpu_shader.h"
#include "xgpu_msl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...) { (void)format; }

static int programs(const char *dir)
{
	int number, rows = 0, screen = 0, failures = 0;

	for (number = 1; number <= 999; number++)
	{
		char path[1024];
		static unsigned char data[16384];
		static struct xgpu_vertex_program program;
		struct xgpu_msl_options options;
		unsigned long z, w;
		FILE *file;
		long size;
		char *source;

		snprintf(path, sizeof(path), "%s/vs%03d.bin", dir, number);
		if (!(file = fopen(path, "rb")))
			break;
		size = (long)fread(data, 1, sizeof(data), file);
		fclose(file);
		nv2a_vertex_program_decode((const DWORD *)(data + 4), (unsigned long)(size - 4) / 16, &program);
		memset(&options, 0, sizeof(options));
		source = xgpu_msl_vertex_shader(&program, 0, 0, NULL, &options);
		if (xgpu_vertex_program_depth_rows(&program, &z, &w))
		{
			rows++;
			if (!source || !strstr(source, "depth_row = depth_w_row - depth_z_row"))
			{
				printf("vs%03d: its depth is not computed from its rows\n", number);
				failures++;
			}
		}
		else
		{
			screen++;
			printf("vs%03d: no rows (projects %d)\n", number, program.projects);
		}
		if (!source || !strstr(source, "position.z = position.w - position.z;"))
		{
			printf("vs%03d: its depth is not reversed\n", number);
			failures++;
		}
		free(source);
	}
	printf("programs %d with rows %d without %d\n", rows + screen, rows, screen);
	return failures ? 1 : 0;
}

/* the shaders' dot product with a row: dot(p.xyz, row.xyz) + row.w (DPH) */
static float dph(const float p[3], const float row[4])
{
	volatile float sum = p[0] * row[0];

	sum = sum + p[1] * row[1];
	sum = sum + p[2] * row[2];
	return sum + row[3];
}

static int precision(double far, double distance, double gap)
{
	/* the game's projection (rasterizer_set_frustum_z: Direct3D's, depth 0
	to 1, its near and far planes) of a camera at (0, 0, 2) looking along
	(1, 1, 0) / sqrt 2, and a wall facing it 100 m away, about (120, -50)
	from the origin; the sniper rifle's zoom changes x and y alone */
	const double near = 0.0625;
	const double camera[3] = { 49.3, -120.7, 2.0 }, forward[3] = { 0.70710678, 0.70710678, 0.0 };
	double alpha = far / (far - near), beta = -far * near / (far - near);
	float z_row[4], w_row[4];
	int axis, sample, game = 0, reversed = 0, samples = 4000;

	for (axis = 0; axis < 3; axis++)
	{
		w_row[axis] = (float)forward[axis];
		z_row[axis] = (float)(forward[axis] * alpha);
	}
	w_row[3] = (float)-(forward[0] * camera[0] + forward[1] * camera[1] + forward[2] * camera[2]);
	z_row[3] = (float)(-(forward[0] * camera[0] + forward[1] * camera[1] + forward[2] * camera[2]) * alpha + beta);
	srand(1);
	for (sample = 0; sample < samples; sample++)
	{
		/* a point of the wall (a metre's square of it, at the distance along
		forward), and the light's in front of it on the same ray */
		double across = (double)rand() / RAND_MAX - 0.5, up = (double)rand() / RAND_MAX - 0.5;
		double wall[3], light[3], scale = (distance - gap) / distance;
		float wall_point[3], light_point[3], z, w, row[4];
		volatile float game_wall, game_light, reversed_wall, reversed_light;

		wall[0] = camera[0] + forward[0] * distance - forward[1] * across;
		wall[1] = camera[1] + forward[1] * distance + forward[0] * across;
		wall[2] = camera[2] + up;
		for (axis = 0; axis < 3; axis++)
		{
			light[axis] = camera[axis] + (wall[axis] - camera[axis]) * scale;
			wall_point[axis] = (float)wall[axis];
			light_point[axis] = (float)light[axis];
		}
		/* the game's: z / w */
		z = dph(wall_point, z_row);
		w = dph(wall_point, w_row);
		game_wall = z / w;
		z = dph(light_point, z_row);
		w = dph(light_point, w_row);
		game_light = z / w;
		game += game_light <= game_wall;
		/* reversed: one dot product with the rows' difference, over w */
		for (axis = 0; axis < 4; axis++)
			row[axis] = w_row[axis] - z_row[axis];
		reversed_wall = dph(wall_point, row) / dph(wall_point, w_row);
		reversed_light = dph(light_point, row) / dph(light_point, w_row);
		reversed += reversed_light >= reversed_wall;
	}
	printf("game %d of %d\nreversed %d of %d\n", game, samples, reversed, samples);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "programs"))
		return programs(argv[2]);
	if (argc == 2 && !strcmp(argv[1], "precision"))
		return precision(1024.0, 100.0, 0.0015);
	if (argc == 5 && !strcmp(argv[1], "precision"))
		return precision(atof(argv[2]), atof(argv[3]), atof(argv[4]));
	fprintf(stderr, "usage: check programs <capture dir> | check precision [<far> <distance> <gap>]\n");
	return 2;
}
