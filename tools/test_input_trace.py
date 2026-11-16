"""The keys and mouse buttons held as SDL holds them (sdl_platform.c: input_held_reconcile), and the input trace
(port/linux/src/input_trace.c) that records each event, reading and release, compiled from the sources."""
import shutil
import subprocess

import pytest

from harness import ABI_FLAGS, function, mutated
from test_controller_remap import ROOT, source

WORLD = r'''
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HALO_GUEST 1
#define SDL_SCANCODE_COUNT 512
#define SDL_SCANCODE_W 26
#define SDL_SCANCODE_RETURN 40
#define SDL_BUTTON_MASK(button) (1u << ((button) - 1))
#define PLATFORM_MOUSE_BUTTON_COUNT 8
#define INPUT_MOUSE SDL_SCANCODE_COUNT
typedef int SDL_Scancode;
typedef unsigned long long Uint64;
static unsigned long long now_ns = 5000000000ull;
unsigned long long input_trace_now_ns(void) {return now_ns;}
static Uint64 SDL_GetTicks(void) {return now_ns / 1000000ull;}
static const char *SDL_GetScancodeName(SDL_Scancode scancode) {return scancode == 26 ? "W" : "Return";}
void halo_input_name(int input, char *name, size_t size)
{snprintf(name, size, "%s", input == 26 ? "W" : input == 40 ? "Return" : input == INPUT_MOUSE + 1 ? "Mouse Left" : "");}
const char *halo_keyboard_action_setting(int action) {return action == 0 ? "move_forward" : "other";}
static char logged[1 << 20];
void platform_log(const char *format, ...)
{
 va_list arguments; size_t used = strlen(logged);
 va_start(arguments, format); vsnprintf(logged + used, sizeof(logged) - used, format, arguments); va_end(arguments);
 strcat(logged, "\n");
}
static struct {unsigned char keys[SDL_SCANCODE_COUNT]; unsigned char mouse_buttons[PLATFORM_MOUSE_BUTTON_COUNT];}
 input_state;
static int test_enter_held;
static unsigned int test_buttons_held;
static unsigned char sdl_keys[SDL_SCANCODE_COUNT];
static unsigned int sdl_buttons;
unsigned int host_sdl_input_held(unsigned char *keys, int count) {memcpy(keys, sdl_keys, count); return sdl_buttons;}
'''

CASES = r'''
static char *trace_now(void) {return input_trace_text(30, NULL);}
int main(int argc, char **argv)
{
 const char *name = argv[1];
 char *text;
 if (!strcmp(name, "lost_release")) {
  /* W's down came, its up was lost on the way: SDL has it up */
  input_trace_key(now_ns - 3000000, 26, 1, 0, 13);
  input_state.keys[26] = 1;
  input_held_reconcile();
  assert(!input_state.keys[26]);
  assert(strstr(logged, "input: W held by its events, but SDL has it up; let go of"));
  text = trace_now();
  assert(strstr(text, "key W down (code 13) [sent 3 ms before]"));
  assert(strstr(text, "let go of W, which its events left held: SDL has it up"));
  /* (and the trace's last seconds went to the log with it) */
  assert(strstr(logged, "input trace: a key or button its events left held was let go of"));
 } else if (!strcmp(name, "held")) {
  input_state.keys[26] = 1; sdl_keys[26] = 1;
  input_state.mouse_buttons[1] = 1; sdl_buttons = SDL_BUTTON_MASK(1);
  input_held_reconcile();
  assert(input_state.keys[26] && input_state.mouse_buttons[1] && !logged[0]);
 } else if (!strcmp(name, "never_pressed")) {
  /* SDL holding a key the events have not brought yet holds nothing */
  sdl_keys[26] = 1; sdl_buttons = SDL_BUTTON_MASK(1);
  input_held_reconcile();
  assert(!input_state.keys[26] && !input_state.mouse_buttons[1] && !logged[0]);
 } else if (!strcmp(name, "system")) {
  /* SDL holds it, the system has had it up: SDL missed its release */
  input_state.keys[26] = 1; sdl_keys[26] = 3;
  input_held_reconcile();
  assert(!input_state.keys[26]);
  assert(strstr(logged, "but the system has it up"));
  assert(strstr(trace_now(), "the system has it up (SDL missed its release)"));
 } else if (!strcmp(name, "mouse")) {
  input_state.mouse_buttons[1] = 1; sdl_buttons = 0;
  input_held_reconcile();
  assert(!input_state.mouse_buttons[1] && strstr(logged, "mouse button 1 held by its events, but SDL has it up"));
  input_state.mouse_buttons[3] = 1; sdl_buttons = SDL_BUTTON_MASK(3) | SDL_BUTTON_MASK(3) << 16;
  input_held_reconcile();
  assert(!input_state.mouse_buttons[3] && strstr(logged, "mouse button 3 held by its events, but the system"));
 } else if (!strcmp(name, "scripted_return")) {
  input_state.keys[40] = 1; test_enter_held = 1;
  input_held_reconcile();
  assert(input_state.keys[40] && !logged[0]);
 } else if (!strcmp(name, "scripted_button")) {
  /* the scripted mouse's button stays until it lets it go */
  input_state.mouse_buttons[1] = 1; test_buttons_held = 1u << 1;
  input_held_reconcile();
  assert(input_state.mouse_buttons[1] && !logged[0]);
  input_state.mouse_buttons[1] = 0;
  input_held_reconcile();
  assert(!test_buttons_held);
 } else if (!strcmp(name, "readings")) {
  unsigned char keys[SDL_SCANCODE_COUNT] = {0}, latched[SDL_SCANCODE_COUNT] = {0};
  unsigned char buttons[PLATFORM_MOUSE_BUTTON_COUNT] = {0}, none[PLATFORM_MOUSE_BUTTON_COUNT] = {0};
  char *at;
  int count = 0;
  keys[26] = 1;
  input_trace_reading(keys, SDL_SCANCODE_COUNT, buttons, PLATFORM_MOUSE_BUTTON_COUNT, latched, none,
   INPUT_TRACE_READING_FOCUSED);
  now_ns += 16000000;
  input_trace_reading(keys, SDL_SCANCODE_COUNT, buttons, PLATFORM_MOUSE_BUTTON_COUNT, latched, none,
   INPUT_TRACE_READING_FOCUSED);
  /* a tap between two readings: latched, and that reading written */
  now_ns += 16000000;
  latched[40] = 1;
  input_trace_reading(keys, SDL_SCANCODE_COUNT, buttons, PLATFORM_MOUSE_BUTTON_COUNT, latched, none,
   INPUT_TRACE_READING_FOCUSED);
  input_trace_pad(0, 0, 0, 32767, 0, 0, 0, 0, 1, 0.0f, 0.0f);
  input_trace_pad(0, 0, 0, 32767, 0, 0, 0, 0, 1, 0.0f, 0.0f);
  input_trace_player(0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
  input_trace_player(0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
  text = trace_now();
  for (at = text; (at = strstr(at, "read: keys W, buttons -, game, focused")); at++) count++;
  assert(count == 2);
  assert(strstr(text, "latched (pressed and let go of since the last reading): 1 key, Return"));
  assert(strstr(text, "pad: buttons -, triggers 0 0, actions move_forward"));
  assert(strstr(text, "pad: left stick 0 32767, right stick 0 0"));
  count = 0;
  for (at = text; (at = strstr(at, "player 0: forward 1.000, strafe 0.000")); at++) count++;
  assert(count == 1);
 } else if (!strcmp(name, "window")) {
  /* only the last seconds asked for, and every one over the ring's size kept */
  int index;
  input_trace_window(now_ns, INPUT_TRACE_FOCUS_LOST);
  now_ns += 40000000000ull;
  for (index = 0; index < 20000; index++) input_trace_mouse_button(now_ns, 1, index & 1, 0);
  input_trace_window(now_ns, INPUT_TRACE_OCCLUDED);
  text = trace_now();
  assert(!strstr(text, "focus lost") && strstr(text, "window: occluded"));
  assert(strstr(text, "mouse 1 down") && strstr(text, "20002 records in all"));
 } else return 2;
 puts("ok");
 return 0;
}
'''


def program(reconcile=None):
    platform = (ROOT / 'port/linux/src/sdl_platform.c').read_text()
    reconcile = reconcile or function(platform, 'input_held_reconcile')
    return (WORLD + source('port/linux/src/input_trace.h') + source('port/linux/src/input_trace.c') + reconcile +
            CASES)


@pytest.mark.parametrize('case', ['lost_release', 'held', 'never_pressed', 'system', 'mouse', 'scripted_return',
                                  'scripted_button', 'readings', 'window'])
def test_input_held_and_trace(tmp_path, case):
    result = _run(tmp_path, program(), case)
    assert result.returncode == 0 and result.stdout == 'ok\n', result.stdout + result.stderr


def _run(tmp_path, code, case):
    """compiles the code as the game's C (gnu89, 32-bit where the machine has a 32-bit runtime) and runs a case"""
    compiler = shutil.which('clang')
    if not compiler:
        pytest.skip('clang required')
    path = tmp_path / 'input.c'
    binary = tmp_path / 'input.exe'
    path.write_text(code)
    result = subprocess.run([compiler, *ABI_FLAGS, '-std=gnu89', '-O1', str(path), '-o', str(binary), '-lpthread'],
                            capture_output=True, text=True)
    if result.returncode and 'cannot find' in result.stderr:
        pytest.skip('a 32-bit C library required (gcc-multilib)')
    assert result.returncode == 0, result.stderr
    return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)


def test_negative_control_events_alone_keep_a_lost_release(tmp_path):
    reconcile = function((ROOT / 'port/linux/src/sdl_platform.c').read_text(), 'input_held_reconcile')
    broken = mutated(reconcile, '\t\tinput_state.keys[scancode] = 0;\n', '')
    assert _run(tmp_path, program(broken), 'lost_release').returncode != 0
