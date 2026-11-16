"""The menus' pictures' finisher (port/linux/src/menu_pictures.c): each kept frame's picture made on a thread of its own,
in the order the frames were kept, while the pass goes on, and the main menu waiting for the last
(menu_pictures_render_finished). Negative controls: a wait that does not wait, and a queue that takes the newest first."""
from pathlib import Path
import subprocess

from test_load_trace import build


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "port/linux/src/menu_pictures.c"


def probe(text):
    code = text[text.index("/* The kept frames' pictures are made on a thread of their own"):
                text.index("int menu_pictures_render_kept(")]
    return r'''
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
struct picture_finish_job { long pose; unsigned char *bgra; struct picture_finish_job *next; };
/* the poses made, in order, and on which thread */
static long made[64];
static int made_count, on_caller;
static pthread_t caller;
static void picture_finish(struct picture_finish_job *job)
{
    struct timespec delay = {0, 5000000};
    /* (slower than the pass hands them over: the queue fills) */
    nanosleep(&delay, NULL);
    if (pthread_equal(pthread_self(), caller))
        on_caller++;
    made[made_count++] = job->pose;
}
''' + code + r'''
int main(void)
{
    long pose;

    caller = pthread_self();
    for (pose = 0; pose < 20; pose++)
    {
        struct picture_finish_job *job = calloc(1, sizeof(*job));

        job->pose = pose;
        job->bgra = malloc(16);
        picture_finish_queue(job);
    }
    /* (handed over at once: none made yet on the caller's thread) */
    assert(on_caller == 0);
    menu_pictures_render_finished();
    /* every picture made once the wait returns, in the order kept */
    assert(made_count == 20);
    for (pose = 0; pose < 20; pose++)
        assert(made[pose] == pose);
    /* (a wait with nothing queued returns) */
    menu_pictures_render_finished();
    return 0;
}
'''


def test_pictures_finished_in_order_before_the_main_menu(tmp_path):
    subprocess.run([str(build(tmp_path, probe(SOURCE.read_text())))], check=True, timeout=30)


def test_pictures_finisher_negative_controls(tmp_path):
    text = SOURCE.read_text()
    wait = "\twhile (picture_finisher.pending > 0)\n\t\tpthread_cond_wait(&picture_finisher.changed, &picture_finisher.lock);\n"
    append = "\tif (picture_finisher.last)\n\t\tpicture_finisher.last->next = job;\n\telse\n\t\tpicture_finisher.first = job;\n\tpicture_finisher.last = job;\n"
    newest_first = "\tjob->next = picture_finisher.first;\n\tpicture_finisher.first = job;\n\tif (!picture_finisher.last)\n\t\tpicture_finisher.last = job;\n"
    assert wait in text and append in text
    for name, broken in (("no-wait", text.replace(wait, "")), ("newest-first", text.replace(append, newest_first))):
        folder = tmp_path / name
        folder.mkdir()
        result = subprocess.run([str(build(folder, probe(broken)))], timeout=30, capture_output=True)
        assert result.returncode != 0, name
