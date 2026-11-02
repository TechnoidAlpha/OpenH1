/* Device identity, for the remapping editor's labels and capture. It does
 * not change input mappings. */
#include "controller_buttons.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* Updated by the game's input polling, read by menus/HUD on the same thread. */
static struct controller_device controller_devices[4];

void controller_device_update(short port, void *gamepad)
{
    if (port >= 0 && port < 4) controller_device_read(gamepad, &controller_devices[port]);
}

void controller_device_get(short port, struct controller_device *device)
{
    memset(device, 0, sizeof(*device));
    if (port >= 0 && port < 4) *device = controller_devices[port];
}

static int controller_family_from_type(SDL_GamepadType type)
{
    switch (type)
    {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return CONTROLLER_XBOX;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return CONTROLLER_PLAYSTATION;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return CONTROLLER_SWITCH;
    default: return CONTROLLER_UNKNOWN;
    }
}

void controller_device_read(void *handle, struct controller_device *device)
{
    SDL_Gamepad *gamepad = handle;
    memset(device, 0, sizeof(*device));
    if (!gamepad) return;
    device->type = SDL_GetGamepadType(gamepad);
    device->family = controller_family_from_type((SDL_GamepadType)device->type);
#ifndef HALO_GUEST
    {
        char const *name = SDL_GetGamepadName(gamepad);
        device->instance = SDL_GetGamepadID(gamepad);
        device->vendor = SDL_GetGamepadVendor(gamepad);
        device->product = SDL_GetGamepadProduct(gamepad);
        snprintf(device->name, sizeof(device->name), "%s", name ? name : "Unknown controller");
    }
#else
    /* The guest's SDL bridge exposes the family but not a device name
     * (port/android/guest/runtime/guest_sdl.c): the host's handle, which
     * stays the same while the pad is open, tells one pad from another. */
    device->instance = (unsigned int)(unsigned long)gamepad;
    snprintf(device->name, sizeof(device->name), "%s",
        device->family == CONTROLLER_XBOX ? "Xbox controller" :
        device->family == CONTROLLER_PLAYSTATION ? "PlayStation controller" :
        device->family == CONTROLLER_SWITCH ? "Switch controller" : "Unknown controller");
#endif
}
