"""Exercise pending menu edits and both production glyph paths without maps."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]


def run_c(tmp_path, code):
    compiler = shutil.which('clang')
    if not compiler:
        pytest.skip('clang required')
    source = tmp_path / 'style.c'
    binary = tmp_path / 'style.exe'
    source.write_text(code)
    target = ['--target=i686-pc-windows-msvc'] if sys.platform == 'win32' else ['-m32']
    result = subprocess.run([compiler, *target, '-std=gnu89', '-O1', str(source), '-o', str(binary)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    subprocess.run([str(binary)], check=True, timeout=10)


def test_dark_gems_label_keeps_existing_saved_value():
    import port_settings
    output = '\n'.join('\n'.join(lines) for lines in port_settings.settings_files().values())
    assert 'DARK GEMS' in output and 'MENU GEMS' not in output
    assert 'menu_gems' in output


def test_control_visible_bounds_and_animation_slots(tmp_path):
    np = pytest.importorskip("numpy")
    Image = pytest.importorskip("PIL.Image")
    import button_assets
    cases=[]
    for slot,e in enumerate(button_assets.CONTROL_BUTTONS,6):
        a=np.asarray(Image.open(button_assets.ASSETS/(e['name']+'.png')))[...,3]
        ys,xs=np.where(a>=128)
        cases.append([slot,int(xs.min()),int(ys.min()),int(xs.max())+1,int(ys.max())+1])
    array=','.join('{'+','.join(map(str,c))+'}' for c in cases)
    header=(ROOT/'port/linux/game/hud_button_layout.h').read_text()
    run_c(tmp_path, '#include <assert.h>\ntypedef float real;\n'+header+
          '\nstatic int cases[][5]={'+array+'};\n'+r'''
static void near(real a,real b) {assert(a-b<0.0001f && b-a<0.0001f);}
int main(void) {
    int i,j;real scales[]={1,.75f,20.0f/26,15.0f/26};
    for(i=0;i<8;i++) for(j=0;j<4;j++) {
        int *c=cases[i];real scale=scales[j],x=104,y=80;
        real w=(i<2?44:32)*scale,h=(i<2?33:34)*scale;
        real cw=i<2?400:288,ch=i<2?280:320;
        hud_button_control_bounds((short)c[0],&x,&y,&w,&h);
        near(x+c[1]*w/cw,104);near(y+c[2]*h/ch,80);
        near((c[3]-c[1])*w/cw,(i<2?44:32)*scale);
        near((c[4]-c[2])*h/ch,(i<2?33:34)*scale);
    }
    for(i=0;i<30;i++) {
        assert(hud_button_control_slot(14,i)==8+2*(i%3));
        assert(hud_button_control_slot(15,i)==9+2*(i%3));
        assert(hud_button_control_slot(16,i)==8);
        assert(hud_button_control_slot(17,i)==9);
    }
    for(i=8;i<14;i++) assert(hud_button_control_slot((short)i,0)==-1);
    return 0;
}
''')


def test_texture_capacity_covers_all_embedded_button_assets():
    import re
    import embed_assets
    source=(ROOT/'port/linux/src/hud_hires.c').read_text()
    capacity=int(re.search(r'#define MAXIMUM_TEXTURES (\d+)',source)[1])
    entries=list(embed_assets.textures())
    assert len(entries)<=capacity
    assert any(asset['name']=='thumbstick-full_right__0' for _,asset,_ in entries)
