/*
GUEST_QSORT.C

qsort and qsort_r, which the guest takes in place of musl's
(tools/android_build.py MUSL_EXCLUDE): musl's smoothsort step for step, so
that equal elements end in the same order as they did, but faster where the
game sorts each frame (the particles drawn, the transparent geometry): qsort
calls the comparison directly, not through a second function, and elements
of 4 or 8 bytes move as words, not through memcpy.

From musl 1.2.5's src/stdlib/qsort.c and qsort_nr.c, under the MIT licence:

Copyright (C) 2011 by Valentin Ochs

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

(Minor changes by Rich Felker for integration in musl, 2011-04-27.)
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int (*sort_compare_r)(const void *, const void *, void *);
typedef int (*sort_compare)(const void *, const void *);

/* the comparison: qsort's (plain, a constant in each of the two sorts
below, so that the other call is compiled away) or qsort_r's */
struct sort
{
	size_t width;
	sort_compare compare;
	sort_compare_r compare_r;
	void *arg;
};

#define SORT_INLINE static inline __attribute__((always_inline))

SORT_INLINE int compare(const struct sort *sort, int plain, const void *left, const void *right)
{
	return plain ? sort->compare(left, right) : sort->compare_r(left, right, sort->arg);
}

static int ntz(size_t value)
{
	return __builtin_ctzl(value);
}

SORT_INLINE int pntz(size_t p[2])
{
	int r = ntz(p[0] - 1);

	if (r != 0 || (r = 8 * sizeof(size_t) + ntz(p[1])) != 8 * sizeof(size_t))
		return r;
	return 0;
}

/* ar[0] takes ar[1]'s element, ar[1] ar[2]'s ... and ar[n - 1] ar[0]'s */
SORT_INLINE void cycle(size_t width, unsigned char *ar[], int n)
{
	unsigned char tmp[256];
	size_t l;
	int i;

	if (n < 2)
		return;
	if (width == 8)
	{
		uint64_t first, next;

		__builtin_memcpy(&first, ar[0], 8);
		for (i = 0; i < n - 1; i++)
		{
			__builtin_memcpy(&next, ar[i + 1], 8);
			__builtin_memcpy(ar[i], &next, 8);
		}
		__builtin_memcpy(ar[n - 1], &first, 8);
		return;
	}
	if (width == 4)
	{
		uint32_t first, next;

		__builtin_memcpy(&first, ar[0], 4);
		for (i = 0; i < n - 1; i++)
		{
			__builtin_memcpy(&next, ar[i + 1], 4);
			__builtin_memcpy(ar[i], &next, 4);
		}
		__builtin_memcpy(ar[n - 1], &first, 4);
		return;
	}
	ar[n] = tmp;
	while (width)
	{
		l = sizeof(tmp) < width ? sizeof(tmp) : width;
		memcpy(ar[n], ar[0], l);
		for (i = 0; i < n; i++)
		{
			memcpy(ar[i], ar[i + 1], l);
			ar[i] += l;
		}
		width -= l;
	}
}

/* shl() and shr() need n > 0 */
SORT_INLINE void shl(size_t p[2], int n)
{
	if (n >= 8 * sizeof(size_t))
	{
		n -= 8 * sizeof(size_t);
		p[1] = p[0];
		p[0] = 0;
	}
	p[1] <<= n;
	p[1] |= p[0] >> (sizeof(size_t) * 8 - n);
	p[0] <<= n;
}

SORT_INLINE void shr(size_t p[2], int n)
{
	if (n >= 8 * sizeof(size_t))
	{
		n -= 8 * sizeof(size_t);
		p[0] = p[1];
		p[1] = 0;
	}
	p[0] >>= n;
	p[0] |= p[1] << (sizeof(size_t) * 8 - n);
	p[1] >>= n;
}

SORT_INLINE void sift(const struct sort *sort, int plain, unsigned char *head, int pshift, size_t lp[])
{
	size_t width = sort->width;
	unsigned char *rt, *lf;
	unsigned char *ar[14 * sizeof(size_t) + 1];
	int i = 1;

	ar[0] = head;
	while (pshift > 1)
	{
		rt = head - width;
		lf = head - width - lp[pshift - 2];

		if (compare(sort, plain, ar[0], lf) >= 0 && compare(sort, plain, ar[0], rt) >= 0)
			break;
		if (compare(sort, plain, lf, rt) >= 0)
		{
			ar[i++] = lf;
			head = lf;
			pshift -= 1;
		}
		else
		{
			ar[i++] = rt;
			head = rt;
			pshift -= 2;
		}
	}
	cycle(width, ar, i);
}

SORT_INLINE void trinkle(const struct sort *sort, int plain, unsigned char *head, size_t pp[2], int pshift,
	int trusty, size_t lp[])
{
	size_t width = sort->width;
	unsigned char *stepson, *rt, *lf;
	size_t p[2];
	unsigned char *ar[14 * sizeof(size_t) + 1];
	int i = 1;
	int trail;

	p[0] = pp[0];
	p[1] = pp[1];

	ar[0] = head;
	while (p[0] != 1 || p[1] != 0)
	{
		stepson = head - lp[pshift];
		if (compare(sort, plain, stepson, ar[0]) <= 0)
			break;
		if (!trusty && pshift > 1)
		{
			rt = head - width;
			lf = head - width - lp[pshift - 2];
			if (compare(sort, plain, rt, stepson) >= 0 || compare(sort, plain, lf, stepson) >= 0)
				break;
		}

		ar[i++] = stepson;
		head = stepson;
		trail = pntz(p);
		shr(p, trail);
		pshift += trail;
		trusty = 0;
	}
	if (!trusty)
	{
		cycle(width, ar, i);
		sift(sort, plain, head, pshift, lp);
	}
}

SORT_INLINE void smoothsort(const struct sort *sort, int plain, void *base, size_t nel)
{
	size_t width = sort->width;
	size_t lp[12 * sizeof(size_t)];
	size_t i, size = width * nel;
	unsigned char *head, *high;
	size_t p[2] = {1, 0};
	int pshift = 1;
	int trail;

	if (!size)
		return;

	head = base;
	high = head + size - width;

	/* Precompute Leonardo numbers, scaled by element width */
	for (lp[0] = lp[1] = width, i = 2; (lp[i] = lp[i - 2] + lp[i - 1] + width) < size; i++)
		;

	while (head < high)
	{
		if ((p[0] & 3) == 3)
		{
			sift(sort, plain, head, pshift, lp);
			shr(p, 2);
			pshift += 2;
		}
		else
		{
			if (lp[pshift - 1] >= (size_t)(high - head))
				trinkle(sort, plain, head, p, pshift, 0, lp);
			else
				sift(sort, plain, head, pshift, lp);

			if (pshift == 1)
			{
				shl(p, 1);
				pshift = 0;
			}
			else
			{
				shl(p, pshift - 1);
				pshift = 1;
			}
		}

		p[0] |= 1;
		head += width;
	}

	trinkle(sort, plain, head, p, pshift, 0, lp);

	while (pshift != 1 || p[0] != 1 || p[1] != 0)
	{
		if (pshift <= 1)
		{
			trail = pntz(p);
			shr(p, trail);
			pshift += trail;
		}
		else
		{
			shl(p, 2);
			pshift -= 2;
			p[0] ^= 7;
			shr(p, 1);
			trinkle(sort, plain, head - lp[pshift] - width, p, pshift + 1, 1, lp);
			shl(p, 1);
			p[0] |= 1;
			trinkle(sort, plain, head - width, p, pshift, 1, lp);
		}
		head -= width;
	}
}

void qsort(void *base, size_t nel, size_t width, sort_compare compare_function)
{
	struct sort sort;

	sort.width = width;
	sort.compare = compare_function;
	sort.compare_r = NULL;
	sort.arg = NULL;
	smoothsort(&sort, 1, base, nel);
}

void qsort_r(void *base, size_t nel, size_t width, sort_compare_r compare_function, void *arg)
{
	struct sort sort;

	sort.width = width;
	sort.compare = NULL;
	sort.compare_r = compare_function;
	sort.arg = arg;
	smoothsort(&sort, 0, base, nel);
}

void __qsort_r(void *base, size_t nel, size_t width, sort_compare_r compare_function, void *arg)
{
	qsort_r(base, nel, width, compare_function, arg);
}
