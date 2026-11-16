/* The records shipped with the game (port/assets/prepared): the form the
code makes records in now (render_prepared.c's PREPARED_VERSION, the
journal's form: d3d8_device.c's halo_gpu_journal_form), printed for
test_prepared_shipped.py to compare with the records' manifest. */
#include "harness.h"

#include <stdint.h>

#include "form.inc"

#define JOURNAL_STATE_WORDS (sizeof(struct journal_state) / sizeof(uint32_t))

int main(void)
{
	printf("%d %u\n", PREPARED_VERSION, (unsigned int)(JOURNAL_FORM << 16 | (unsigned int)JOURNAL_STATE_WORDS));
	return 0;
}
