"""MCC child anchors must reach every weapon HUD element's draw call."""


from tools.harness import build, function, read, run


def test_anchor_selection_and_map_transitions():
    source = function(read("port/linux/game/mcc_hud_draw.c"), "mcc_hud_child_placement")
    status, output = run(build("mcc_hud_child_anchor", (("under_test.inc", source),)), "anchors")
    assert status == 0, output
