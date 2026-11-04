"""A light let go of with no transition (the Scorpion's cannon's) is at its transition's end, not faded by 0/0, and the
rasterizer draws no light whose place, reach or colour is not a number, and an artificially
bright object on black is brightened to grey, not by 0/0 (source/objects/object_lights.c,
source/rasterizer/rasterizer_lights.c)."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED,build,function,inline,mutated,read,run,structure

CONTROLS={
    'zero-over-zero':(('object_lights','return duration > 0.0f ? MAX(elapsed, 0.0f) / duration : 1.0f;',
                       'return MAX(elapsed, 0.0f) / duration;'),'no-transition'),
    'unguarded':(('submit','return NONE;',';'),'not-a-number'),
    'brighten-by-zero':(('object_lights','if (!(maximum > 0.0f))','if (FALSE)'),'brighten-black'),
}

def generated(control=None):
    source=read('source/objects/object_lights.c')
    lights=function(source,'light_port_transition_fraction')+'\n'+function(source,'brighten_real_rgb_color')
    header=read('source/rasterizer/rasterizer_lights.h')
    submit=inline(read('source/math/real_math.h'),'valid_real')+'\n'+structure(header,'rasterizer_light_submit_parameters')+'\n'+ \
        structure(header,'rasterizer_lights')
    body=function(read('source/rasterizer/rasterizer_lights.c'),'rasterizer_light_submit')
    if control:
        where,before,after=CONTROLS[control][0]
        if where=='object_lights':lights=mutated(lights,before,after)
        else:body=mutated(body,before,after)
    return (('under_test.inc',submit+'\n'+lights),('under_test_submit.inc',body))

@pytest.mark.parametrize('case',['no-transition','transition','brighten-black','submitted','not-a-number'])
def test_light(case):
    status,output=run(build('light_not_a_number',generated()),case)
    assert status==0,output

@pytest.mark.parametrize('control',sorted(CONTROLS))
def test_negative_control(control):
    status,output=run(build('light_not_a_number',generated(control)),CONTROLS[control][1])
    assert status==CHECK_FAILED,output
