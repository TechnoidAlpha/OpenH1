"""MCC packages' navigation (Settings > Mods) and the sharing screens' focus on each platform."""
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import pytest
from mcc_test_support import compile_tool
ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "port/linux/game"


MENUS = ROOT / "port/assets/menus/ce"
SETTINGS = "main_menu/settings_select/player_setup/player_profile_edit/"


def _menus():
    """the generated menus' widgets and string lists, by name"""
    widgets, strings = {}, {}
    for file in MENUS.glob("*.xml"):
        root = ET.parse(file).getroot()
        for widget in root.findall("widget"):
            widgets[widget.get("name")] = widget
        for listing in root.findall("strings"):
            strings[listing.get("name")] = [s.get("text") for s in listing.findall("string")]
    return widgets, strings


def _label(widgets, strings, name):
    widget = widgets[name]
    return strings[widget.get("string_list")][int(widget.get("string_index"))]


@pytest.mark.parametrize("platform", ["desktop", "android"])
def test_settings_mods_tab_reaches_mcc_packages(platform):
    """Settings > Mods > MASTER CHIEF COLLECTION: the MCC PACKAGES row opens
    the packages' settings, and they reach MCC DOWNLOADS and its REMOVE"""
    widgets, strings = _menus()
    # (the settings' last tab is MODS, which "port settings tab" opens)
    assert strings["main_menu/settings/tab_names"][-1] == "MODS"
    assert _label(widgets, strings, SETTINGS + "video_settings/tab_mods_settings") == "MODS"
    visible = lambda node: platform in node.get("platform", "desktop android").split()
    pending = [SETTINGS + "mods_settings/mods_settings_screen"]
    reached = set()
    while pending:
        name = pending.pop()
        if name in reached or name not in widgets or not visible(widgets[name]):
            continue
        reached.add(name)
        for node in widgets[name]:
            if visible(node):
                target = node.get("widget") if node.tag == "child" else node.get("open")
                if target:
                    pending.append(target)
    row = SETTINGS + "mods_settings/op_mcc_packages"
    assert row in reached
    labels = strings[SETTINGS + "mods_settings/labels"]
    assert labels.index("MASTER CHIEF COLLECTION") < labels.index("MCC PACKAGES ›")
    packages = SETTINGS + "network_setup/mcc_packages/mcc_packages_screen"
    assert packages in reached
    settings = {widgets[name].get("setting") for name in reached}
    assert {"game.mcc_campaigns", "game.mcc_collections"} <= settings
    # (Steam's Workshop is the desktop's alone)
    assert ("game.workshop_maps" in settings) == (platform == "desktop")
    assert SETTINGS + "network_setup/mcc_downloads/screen" in reached
    assert SETTINGS + "network_setup/mcc_downloads/remove" in reached


@pytest.mark.parametrize("platform", ["desktop", "android"])
@pytest.mark.parametrize("screen", ["share", "downloads"])
def test_prompt_focus_tracks_visible_actions(tmp_path, platform, screen):
    from harness import function

    source = (GAME / "menu_functions.c").read_text()
    start = source.index(f'else if (!strcmp(name,"port mcc {screen} update")) {{')
    end = source.index('\n    else if' if screen == 'downloads' else '\n\telse if', start)
    body = source[start:end].replace('else if', 'if', 1)
    driver = r'''
#include <assert.h>
#include <string.h>
typedef int boolean;
#define FALSE 0
#define NONE -1
struct widget_instance {
    struct widget_instance *child,*next,*focused_child;
    int visible;
    struct {struct {int selected_index;} list;} parameters;
};
static struct widget_instance children[6];
static int download_on,workshop_on;
static int mcc_share_game_can_download(void) {return download_on;}
static int mcc_share_game_has_workshop(void) {return workshop_on;}
#define mcc_share_menu_can_remove() download_on
#define mcc_share_menu_confirming() 0
#define text_set(widget,text) ((void)0)
static struct widget_instance *named(struct widget_instance *w,char const *name,int unused) {
    (void)w;(void)unused;
    return children+(!strcmp(name,"download")?1:!strcmp(name,"workshop")?2:3);
}
''' + function(source, "focus_row") + '\nstatic void update(struct widget_instance *widget) {char const *name="port mcc share update";\n' + body + r'''
}
int main(void) {
    struct widget_instance screen;
    int d,w,i,expected;
    memset(&screen,0,sizeof(screen));screen.child=children;
    for(i=0;i<6;i++){children[i].visible=1;children[i].next=i<5?children+i+1:0;}
    for(d=0;d<2;d++)for(w=0;w<2;w++) {
        download_on=d;workshop_on=w;
        screen.focused_child=children+1;screen.parameters.list.selected_index=1;
        expected=d?1:w?2:4;
#ifdef HALO_ANDROID
        if(!d&&w)expected=3;
#endif
        update(&screen);
        assert(screen.focused_child==children+expected);
        assert(screen.parameters.list.selected_index==expected&&screen.focused_child->visible);
        if(w) {
            screen.focused_child=children+3;screen.parameters.list.selected_index=3;
            update(&screen);assert(screen.focused_child==children+3);
            workshop_on=0;update(&screen);assert(screen.focused_child==children+(d?1:4));
        }
    }
    return 0;
}
'''
    if screen == "downloads":
        driver = driver.replace('name="port mcc share update"', 'name="port mcc downloads update"')
        start = driver.index('    for(d=0;d<2;d++)')
        driver = driver[:start] + r'''
    (void)d;(void)w;(void)expected;(void)workshop_on;
    (void)mcc_share_game_can_download;(void)mcc_share_game_has_workshop;
    for(i=0;i<3;i++) {
        download_on=i==0;
        screen.focused_child=i==2?0:children+3;screen.parameters.list.selected_index=3;
        update(&screen);
        assert(screen.focused_child==children+(i==0?3:4));
        assert(screen.parameters.list.selected_index==(i==0?3:4));
        assert(screen.focused_child->visible);
    }
    return 0;
}
'''
    prefix = "" if platform == "desktop" else "#define HALO_" + platform.upper() + " 1\n"
    path = tmp_path / "prompt.c"
    path.write_text(prefix + driver)
    exe = compile_tool(tmp_path, [path])
    subprocess.run([str(exe)], check=True, capture_output=True)
