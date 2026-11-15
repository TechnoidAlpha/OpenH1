#ifndef CONTROLLER_BUTTONS_H
#define CONTROLLER_BUTTONS_H

enum controller_family
{
    CONTROLLER_UNKNOWN,
    CONTROLLER_XBOX,
    CONTROLLER_PLAYSTATION,
    CONTROLLER_SWITCH,
    /* the Steam Deck's own controls (A B X Y, L1 R1, L2 R2, View, L4 L5 R4 R5) */
    CONTROLLER_STEAM_DECK
};

struct controller_device
{
    int family;
    int type;
    unsigned int instance;
    unsigned short vendor, product;
    /* its back paddles (controller_remap_paddles' bits) */
    unsigned int paddles;
    char name[128];
};

/* Uses the same port assignment as XInput, including keyboard/pad split screen.
 * A disconnected port returns a cleared identity; no saved mapping is changed. */
void controller_device_get(short port, struct controller_device *device);
void controller_device_read(void *gamepad, struct controller_device *device);
void controller_device_update(short port, void *gamepad);
/* the HUD's button styles (display.hud_button_style and its families'
 * display.hud_button_style_xbox, _ps and _switch): a style's index by its
 * name, its controller family, a family's setting, and a port's style, its
 * controller's family's where input.auto_button_detection says (OpenCE's
 * button glyphs: port/linux/game/controller_glyphs.c) */
int controller_button_style_index(char const *name);
int controller_button_style_family(int style);
char const *controller_button_family_setting(int family);
int controller_button_style_for_port(short port);

#endif
