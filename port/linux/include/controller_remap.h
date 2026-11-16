#ifndef CONTROLLER_REMAP_H
#define CONTROLLER_REMAP_H

#include <stddef.h>

#define CONTROLLER_REMAP_GAME_ACTIONS 10
#define CONTROLLER_REMAP_BUTTONS 19
/* The back paddles' physical indices, after the pad's sixteen: the left and
 * right (an Elite's P3 and P1, the Steam Deck's L4 and R4), then the second
 * left and right (P4 and P2, L5 and R5). They are not the Xbox pad's, so
 * they can only press a custom layout's actions. */
#define CONTROLLER_REMAP_PADDLE 16
#define CONTROLLER_REMAP_PHYSICAL 20
#define CONTROLLER_REMAP_TEXT 512
#define CONTROLLER_REMAP_UNBOUND 255
enum controller_remap_action
{
    PAD_JUMP, PAD_GRENADE_TYPE, PAD_USE, PAD_WEAPON, PAD_MELEE,
    PAD_FLASHLIGHT, PAD_GRENADE, PAD_FIRE, PAD_CROUCH, PAD_ZOOM,
    PAD_SCOREBOARD, PAD_SCREENSHOT, PAD_FORWARD, PAD_BACKWARD, PAD_LEFT, PAD_RIGHT,
    PAD_CHAT, PAD_TEAM_CHAT, PAD_VEHICLE_CHAT, PAD_TALK,
    PAD_CAMERA_FASTER, PAD_CAMERA_SLOWER, PAD_CAMERA_SWITCH, PAD_CAMERA_UP, PAD_CAMERA_DOWN,
    PAD_CAMERA_CYCLE,
    /* (after the debug camera's, so that a v3 layout is a v4 one's start:
     * it is in the gameplay group, CONTROLLER_REMAP_DEBUG_ACTION) */
    PAD_PING,
    CONTROLLER_REMAP_ACTIONS
};
/* The debug camera's actions, a group of their own: their buttons may be
 * the gameplay's. */
#define CONTROLLER_REMAP_DEBUG_ACTION(action) ((action) >= PAD_CAMERA_FASTER && (action) <= PAD_CAMERA_CYCLE)
#define CONTROLLER_REMAP_DEBUG_MASK ((1u << (PAD_CAMERA_CYCLE + 1)) - (1u << PAD_CAMERA_FASTER))
#define CONTROLLER_REMAP_GAMEPLAY_MASK (((1u << CONTROLLER_REMAP_ACTIONS) - 1) & ~CONTROLLER_REMAP_DEBUG_MASK)

/* Game control indices: jump, grenade type, use/reload, weapon, melee,
 * flashlight, grenade, fire, crouch, zoom. Start and stick axes stay stock. */
extern unsigned char const controller_remap_controls[CONTROLLER_REMAP_GAME_ACTIONS];
char const *controller_remap_setting(short port);
/* the layout text a port's player plays with: their profile's own
 * (input.controller), else the port's setting's */
char const *controller_remap_text(short port);
int controller_remap_parse(char const *text, unsigned char *buttons);
void controller_remap_format(unsigned char const *buttons, char *text, unsigned int size);
int controller_remap_read(short port, unsigned char *buttons);
void controller_remap_defaults(unsigned char *buttons);
void controller_remap_complete(unsigned char *buttons);
int controller_remap_conflict(unsigned char const *buttons, int action, int button);
void controller_remap_assign(unsigned char *buttons, int action, int button);
char const *controller_remap_button_name(int button, int family);
int controller_remap_cycle(int button, int direction);

/* Game-side access to the existing profile and menu context. No profile writes. */
void controller_remap_profile(short port, unsigned char *buttons);
int controller_remap_playing(short port);
void controller_remap_reset(void);
void controller_remap_input_reset(void);
short controller_remap_local_player(short port);
int controller_remap_context(short port);
int controller_remap_camera(short local_player, int available, int controlling,
    unsigned int *held, unsigned int *pressed);
unsigned int controller_remap_held(short port);
unsigned int controller_remap_pressed(short port, unsigned int mask);
int controller_remap_connected(short port);
int controller_remap_camera_cycle(short port, int fast);
int controller_remap_talk(void);
void controller_remap_movement(short port, float *forward, float *strafe);
struct game_input_preferences;
void controller_remap_prompt(short port, struct game_input_preferences *preferences);
/* What controller_remap_prompt gives a game control on a back paddle: this
 * plus the paddle (0 to 3), past the HUD's button icons, which have none for
 * them; controller_remap_prompt_text names it. */
#define CONTROLLER_REMAP_PROMPT_PADDLE 250
/* The words a prompt shows for a button controller_remap_prompt gave: UNBOUND
 * for none, a back paddle's name on the port's controller ("L4", "P1"); NULL
 * for a button that has its icon. */
wchar_t const *controller_remap_prompt_text(short port, int button);

/* Raw physical input only, before keyboard/touch merging. Returns true while
 * capture consumes this port. The caller also suppresses its merged menu input. */
struct _XINPUT_GAMEPAD;
int controller_remap_input(short port, struct _XINPUT_GAMEPAD *pad, int connected);
/* The port's back paddles held, for its next controller_remap_input: bit 0
 * the left paddle, 1 the right, 2 the second left, 3 the second right. */
void controller_remap_paddles(short port, unsigned int paddles);
void controller_remap_merge(struct _XINPUT_GAMEPAD const *source, struct _XINPUT_GAMEPAD *target);
/* The physical buttons OpenH1's overlays read of a port's pad while they
 * are up (port/linux/src/xinput_sdl.c's remap_reserved): bits of the game's
 * button indices, which a custom layout passes through untranslated. */
#define CONTROLLER_REMAP_RESERVE_X (1u << 2)
#define CONTROLLER_REMAP_RESERVE_DPAD_UP (1u << 8)
#define CONTROLLER_REMAP_RESERVE_DPAD_DOWN (1u << 9)
#define CONTROLLER_REMAP_RESERVE_DPAD_LEFT (1u << 10)
#define CONTROLLER_REMAP_RESERVE_DPAD_RIGHT (1u << 11)
#define CONTROLLER_REMAP_RESERVE_ALL 0xFFFFu
void controller_remap_reserve(short port, unsigned int buttons);
void controller_remap_capture_begin(short port);
int controller_remap_capture_poll(int *button);
void controller_remap_capture_end(void);

#endif
