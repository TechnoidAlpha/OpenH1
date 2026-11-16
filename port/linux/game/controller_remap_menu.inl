/* Settings > Controls > Controller > Button Remapping (tools/port_settings.py's
 * BUTTON_REMAPPING page): each player's layout, their profile's own
 * (input.controller: player_settings.c) or their port's (input.controller_p1
 * to p4, a player's without a profile, and the older versions'),
 * a group of actions at a time. The page's rows are the player's
 * (op_remap_player), the group's (op_remap_group) and an action's each
 * (op_pad_1 to op_pad_<REMAP_ROWS>), whose names and buttons this file
 * writes each frame (pad_editor_update, from the page's "port settings
 * page"). A on an action takes the next button pressed on that player's
 * controller, left and right step through the buttons, X (or Delete while
 * one is taken) leaves the action unbound; a button another action has is
 * replaced only once the player says so, leaving that one unbound. The
 * page's OK writes every player's layout ("port settings keep"), its Cancel
 * forgets them, Defaults puts the shown player's back to the profile's
 * preset. The profile is never changed. */
static wchar_t const *const pad_action_labels[CONTROLLER_REMAP_ACTIONS] = {
    L"JUMP", L"SWITCH GRENADE", L"USE / RELOAD", L"SWITCH WEAPON", L"MELEE",
    L"FLASHLIGHT", L"THROW GRENADE", L"FIRE", L"CROUCH", L"ZOOM",
    L"SCOREBOARD", L"SCREENSHOT", L"MOVE FORWARD", L"MOVE BACKWARD", L"STRAFE LEFT", L"STRAFE RIGHT",
    L"CHAT", L"TEAM CHAT", L"VEHICLE CHAT", L"PUSH TO TALK",
    L"CAMERA SPEED UP", L"CAMERA SPEED DOWN", L"CAMERA / PLAYER SWITCH", L"CAMERA RAISE", L"CAMERA LOWER", L"CYCLE CAMERA",
    L"PING"
};
static short const pad_action_groups[CONTROLLER_REMAP_ACTIONS] = {0,1,1,1,1,2,1,1,0,2,2,2,0,0,0,0,3,3,3,3,4,4,4,4,4,4,3};
static char const *const pad_group_names[] = { "MOVEMENT", "COMBAT", "GENERAL", "COMMUNICATION", "DEBUG CAMERA" };
#define PAD_GROUPS ((short)NUMBEROF(pad_group_names))
static struct
{
    unsigned char buttons[4][CONTROLLER_REMAP_ACTIONS];
    boolean custom[4];
    short capturing, port, proposed, conflict, button, group;
    /* the player and the group shown */
    short shown_port, shown_group;
    /* the page the layouts were read for (a page opened reads them again) */
    long screen_tag, screen_time;
} pad_editor = { {{0}}, {0}, NONE, 0, NONE, NONE, 0, 0, 0, 0, NONE, 0 };

static boolean pad_editor_page(struct widget_instance *screen)
{
    return screen && named(screen, "op_remap_player", 0) != NULL;
}

static void pad_editor_begin(struct widget_instance *screen)
{
    short i;
    controller_remap_capture_end();
    pad_editor.capturing = pad_editor.proposed = NONE;
    for (i = 0; i < 4; i++)
    {
        controller_remap_profile(i, pad_editor.buttons[i]);
        pad_editor.custom[i] = (boolean)controller_remap_read(i, pad_editor.buttons[i]);
    }
    pad_editor.shown_port = controller_of(screen);
    if (pad_editor.shown_port < 0 || pad_editor.shown_port >= 4) pad_editor.shown_port = 0;
    pad_editor.shown_group = 0;
    pad_editor.screen_tag = screen->definition_tag_index;
    pad_editor.screen_time = screen->creation_time;
}

/* the action on an action's row, of the group shown; NONE */
static short pad_editor_action(struct widget_instance *widget)
{
    short wanted, action;
    for (; widget && (!widget->name || strncmp(widget->name, "op_pad_", 7)); widget = widget->parent);
    if (!widget) return NONE;
    wanted = (short)(atoi(widget->name + 7) - 1);
    for (action = 0; action < CONTROLLER_REMAP_ACTIONS && wanted >= 0; action++)
        if (pad_action_groups[action] == pad_editor.shown_group && !wanted--)
            return action;
    return NONE;
}

/* the row (op_...) a widget is in */
static char const *pad_editor_row(struct widget_instance *widget)
{
    for (; widget && (!widget->name || strncmp(widget->name, "op_", 3)); widget = widget->parent);
    return widget ? widget->name : "";
}

/* A replacement is pending until the player accepts it. There is no swap. */
static void pad_editor_propose(short port, short action, int button)
{
    pad_editor.port = port;
    pad_editor.group = pad_action_groups[action];
    pad_editor.button = (short)button;
    pad_editor.conflict = (short)controller_remap_conflict(pad_editor.buttons[port], action, button);
    pad_editor.proposed = pad_editor.conflict == NONE ? NONE : action;
    if (pad_editor.proposed == NONE)
    {
        controller_remap_assign(pad_editor.buttons[port], action, button);
        pad_editor.custom[port] = TRUE;
    }
}

/* "port controller bind", "left", "right" and "clear", on the page's rows */
static boolean pad_editor_event(struct widget_instance *widget, char const *name)
{
    char const *row = pad_editor_row(widget);
    short port = pad_editor.shown_port, action;
    int direction = !strcmp(name, "port controller left") ? -1 : 1;

    if (pad_editor.capturing != NONE)
        return TRUE;
    if (pad_editor.proposed != NONE)
    {
        /* (A replaces; anything else leaves both as they were) */
        if (!strcmp(name, "port controller bind"))
        {
            controller_remap_assign(pad_editor.buttons[pad_editor.port], pad_editor.proposed, pad_editor.button);
            pad_editor.custom[pad_editor.port] = TRUE;
        }
        pad_editor.proposed = NONE;
        return TRUE;
    }
    if (!strcmp(row, "op_remap_player") || !strcmp(row, "op_remap_group"))
    {
        short *shown = !strcmp(row, "op_remap_player") ? &pad_editor.shown_port : &pad_editor.shown_group;
        short count = !strcmp(row, "op_remap_player") ? 4 : PAD_GROUPS;
        if (!strcmp(name, "port controller clear")) return TRUE;
        *shown = (short)((*shown + direction + count) % count);
        return TRUE;
    }
    if ((action = pad_editor_action(widget)) == NONE)
        return TRUE;
    if (!strcmp(name, "port controller clear"))
        pad_editor_propose(port, action, CONTROLLER_REMAP_UNBOUND);
    else if (!strcmp(name, "port controller bind"))
    {
        pad_editor.capturing = action;
        pad_editor.port = port;
        controller_remap_capture_begin(port);
    }
    else
    {
        /* (past the back paddles the player's controller has not) */
        struct controller_device device;
        int button = pad_editor.buttons[port][action];
        controller_device_get(port, &device);
        do button = controller_remap_cycle(button, direction);
        while (button != CONTROLLER_REMAP_UNBOUND && button >= CONTROLLER_REMAP_PADDLE &&
            !(device.paddles & (1u << (button - CONTROLLER_REMAP_PADDLE))));
        pad_editor_propose(port, action, button);
    }
    return TRUE;
}

/* the page's OK ("port settings keep"): every player's layout written, a
player's with a profile into their profile (input.controller), another's
as the port's (input.controller_p<n>) */
static boolean pad_editor_save(struct widget_instance *screen)
{
    boolean result = TRUE;
    short i;
    if (!pad_editor_page(screen)) return TRUE;
    controller_remap_capture_end();
    pad_editor.capturing = pad_editor.proposed = NONE;
    for (i = 0; i < 4; i++)
    {
        char text[CONTROLLER_REMAP_TEXT] = {0};
        char folder[1024];
        if (pad_editor.custom[i]) controller_remap_format(pad_editor.buttons[i], text, sizeof(text));
        if (!strcmp(text, controller_remap_text(i))) continue;
        if (player_settings_profile_folder(player_ui_get_active_player_profile_index(i), folder, sizeof(folder)))
        {
            if (!config_profile_write(folder, "input.controller", text)) result = FALSE;
        }
        else if (!config_write(controller_remap_setting(i), text)) result = FALSE;
    }
    return result;
}

/* the page's Cancel, B and Escape ("port settings page cancel"): a pending
 * replacement or a button being taken is dropped, the page kept (FALSE);
 * else the page is left, its changes forgotten */
static boolean pad_editor_cancel(struct widget_instance *screen)
{
    if (!pad_editor_page(screen)) return TRUE;
    if (pad_editor.proposed != NONE || pad_editor.capturing != NONE)
    {
        pad_editor.proposed = pad_editor.capturing = NONE;
        controller_remap_capture_end();
        return FALSE;
    }
    controller_remap_capture_end();
    return TRUE;
}

/* the page's Defaults ("port settings defaults"): the shown player's layout
 * the profile's preset again */
static void pad_editor_defaults(struct widget_instance *screen)
{
    if (!pad_editor_page(screen)) return;
    pad_editor.proposed = NONE;
    controller_remap_profile(pad_editor.shown_port, pad_editor.buttons[pad_editor.shown_port]);
    pad_editor.custom[pad_editor.shown_port] = FALSE;
}

static void pad_editor_text_set(struct widget_instance *text_box, char const *text)
{
    wchar_t wide[ROW_TEXT_LENGTH];
    short i;
    for (i = 0; text[i] && i < ROW_TEXT_LENGTH - 1; i++) wide[i] = (wchar_t)(unsigned char)text[i];
    wide[i] = 0;
    text_set(text_box, wide);
}

/* each frame of the page's list: a button taken, the rows' names and values */
static void pad_editor_update(struct widget_instance *list)
{
    struct widget_instance *screen = screen_of(list), *row;
    struct controller_device device;
    short port;
    char text[ROW_TEXT_LENGTH];

    if (!pad_editor_page(screen)) return;
    if (screen->definition_tag_index != pad_editor.screen_tag || screen->creation_time != pad_editor.screen_time)
        pad_editor_begin(screen);
    port = pad_editor.shown_port;
    if (pad_editor.proposed != NONE && (port != pad_editor.port || pad_editor.shown_group != pad_editor.group))
        pad_editor.proposed = NONE;
    if (pad_editor.capturing != NONE)
    {
        int button, result = controller_remap_capture_poll(&button);
        if (result)
        {
            if (result == 1 || result == 2)
                pad_editor_propose(pad_editor.port, pad_editor.capturing, result == 2 ? CONTROLLER_REMAP_UNBOUND : button);
            pad_editor.capturing = NONE;
        }
    }
    controller_device_get(port, &device);
    snprintf(text, sizeof(text), "P%d - %s", port + 1, pad_editor.custom[port] ? "CUSTOM" : "PROFILE'S");
    pad_editor_text_set(named(list, "remap_player_value", 0), text);
    pad_editor_text_set(named(list, "remap_group_value", 0), pad_group_names[pad_editor.shown_group]);
    for (row = list->child; row; row = row->next)
    {
        short action;
        char label_name[32], value_name[32];
        int index;

        if (!row->name || strncmp(row->name, "op_pad_", 7)) continue;
        action = pad_editor_action(row);
        row->visible = action != NONE;
        if (action == NONE) continue;
        index = atoi(row->name + 7);
        snprintf(label_name, sizeof(label_name), "pad_%d_label", index);
        snprintf(value_name, sizeof(value_name), "pad_%d_value", index);
        text_set(named(row, label_name, 0), pad_action_labels[action]);
        pad_editor_text_set(named(row, value_name, 0),
            pad_editor.capturing == action && pad_editor.port == port ? "PRESS A BUTTON" :
            pad_editor.buttons[port][action] == CONTROLLER_REMAP_UNBOUND ? "UNBOUND" :
            controller_remap_button_name(pad_editor.buttons[port][action], device.family));
    }
}

/* the panel's name, value and words for a row of the page: an action's
 * (its name and button are this file's, not a list's), and while a button is
 * taken or a replacement asked; the player's row names the controller (the
 * others': FALSE) */
static boolean pad_editor_panel(struct widget_instance *row, wchar_t *name, wchar_t *value,
    wchar_t const **help)
{
    static wchar_t words[512];
    struct controller_device device;
    struct widget_instance *child;
    char text[512];
    short i;

    if (!row->name || (strncmp(row->name, "op_pad_", 7) && strncmp(row->name, "op_remap_", 9)))
        return FALSE;
    /* (the row's texts: its label, then its value) */
    for (child = row->child, i = 0; child; child = child->next, i++)
    {
        wchar_t *to = i == 0 ? name : value;
        if (i > 1) break;
        if (child->type != 1 || !child->parameters.text_box.text) continue;
        if (i == 0 && strncmp(row->name, "op_pad_", 7)) continue;
        ustrncpy(to, child->parameters.text_box.text, ROW_TEXT_LENGTH - 1);
        to[ROW_TEXT_LENGTH - 1] = 0;
    }
    controller_device_get(pad_editor.port, &device);
    if (pad_editor.proposed != NONE)
    {
        char conflict[64];
        for (i = 0; pad_action_labels[pad_editor.conflict][i] && i < 63; i++)
            conflict[i] = (char)pad_action_labels[pad_editor.conflict][i];
        conflict[i] = 0;
        snprintf(text, sizeof(text), "%s already uses %s. Press A or Enter to put it here, leaving %s unbound; "
            "B or Escape keeps both as they were.", conflict,
            controller_remap_button_name(pad_editor.button, device.family), conflict);
    }
    else if (pad_editor.capturing != NONE)
        snprintf(text, sizeof(text), "Let go, then press the button for it on player %d's controller.|Start or "
            "Escape cancels; Delete leaves it unbound.", pad_editor.port + 1);
    else if (!strcmp(row->name, "op_remap_player"))
    {
        controller_device_get(pad_editor.shown_port, &device);
        snprintf(text, sizeof(text), "Whose buttons to edit. Each player's layout is kept apart, and named for "
            "the controller they play with: %s.|OK saves every player's.", device.name[0] ? device.name :
            "none connected");
    }
    else if (!strncmp(row->name, "op_pad_", 7))
        snprintf(text, sizeof(text), "Press A or Enter, then the button for it. Left and right step through the "
            "buttons; X leaves it without one.|In menus the buttons are the stock ones.");
    else
        return TRUE;
    for (i = 0; text[i] && i < (short)NUMBEROF(words) - 1; i++) words[i] = (wchar_t)(unsigned char)text[i];
    words[i] = 0;
    *help = words;
    return TRUE;
}
