"""The port's own error messages (#523): their written line breaks made spaces before the dialog wraps them to its
own width (ui_widget.c's ui_widget_port_text_unbreak), a blank line kept; the dialog's text box does so before it
wraps."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

NEGATIVE_CONTROLS = {
    # the breaks kept as written (the lines ran past a narrower dialog)
    "breaks-kept": ("\t\t\ttext[to++] = breaks > 1 ? L'\\r' : L' ';", "\t\t\ttext[to++] = L'\\r';"),
    # a paragraph made one line with the next
    "paragraph-lost": ("\t\t\tif (breaks > 1)\n\t\t\t\ttext[to++] = L'\\r';\n", ""),
}


def generated(fault=None):
    code = function(read("source/interface/ui_widget.c"), "ui_widget_port_text_unbreak")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault])
    return (("under_test.inc", code + "\n"),)


def test_unbreak():
    status, output = run(build("error_text", generated()), "unbreak")
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("error_text", generated(control)), "unbreak")
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_dialog_unbreaks_before_wrapping():
    renderer = function(read("source/interface/ui_widget.c"), "widget_instance_render_text_box")
    assert "\t\tui_widget_port_text_unbreak(*text);\n\t\tustrncpy(wrapped, *text," in renderer
