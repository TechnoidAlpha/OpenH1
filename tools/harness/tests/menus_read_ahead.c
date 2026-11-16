/* The PC menus' files are read on a thread of their own from the start
(port/linux/src/menu_files.c): the first halo_menus_load waits for that
read and has what it read, the files are read once, and with no read
ahead the load reads them itself. */
#include "harness.h"
#include <pthread.h>
#include <unistd.h>

struct halo_menus { long widget_count; char const *root; };

#include "state.inc"

static int reads;
/* the reading, slow, as Expat's 24 ms over the files are */
static void menus_files_read(void)
{
	menus_read = 1;
	reads++;
	usleep(50000);
	menus.widget_count = 2544;
	menus.root = "main_menu";
	menus_succeeded = 1;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";
	struct halo_menus const *loaded;

	if (!strcmp(test, "ahead"))
	{
		halo_menus_read_ahead();
		halo_menus_read_ahead();
		loaded = halo_menus_load();
		CHECK(loaded && loaded->widget_count == 2544, "the load did not wait for the read (%p)", (void *)loaded);
		CHECK(reads == 1, "read %d times", reads);
		return 0;
	}
	if (!strcmp(test, "again"))
	{
		halo_menus_read_ahead();
		loaded = halo_menus_load();
		halo_menus_read_ahead();
		CHECK(halo_menus_load() == loaded && loaded, "a later load has another result");
		CHECK(reads == 1, "read %d times", reads);
		return 0;
	}
	if (!strcmp(test, "none"))
	{
		loaded = halo_menus_load();
		CHECK(loaded && loaded->widget_count == 2544, "the load did not read the files");
		CHECK(reads == 1, "read %d times", reads);
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
