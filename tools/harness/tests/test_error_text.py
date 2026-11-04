"""The port's own error messages (#523): their written line breaks made spaces before the dialog wraps them to its
own width (ui_widget.c's ui_widget_port_text_unbreak), a blank line kept; the wrap (ui_widget_port_text_wrap)
measures a line's last word before a break too, which it skipped, so that a written line ran past the dialog; the
dialog's text box unbreaks before it wraps."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

# a fault, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the breaks kept as written (the lines ran past a narrower dialog)
    "breaks-kept": (("\t\t\ttext[to++] = breaks > 1 ? L'\\r' : L' ';", "\t\t\ttext[to++] = L'\\r';"), "unbreak"),
    # a paragraph made one line with the next
    "paragraph-lost": (("\t\t\tif (breaks > 1)\n\t\t\t\ttext[to++] = L'\\r';\n", ""), "unbreak"),
    # a line's last word before a break not measured, as before
    "last-word-unmeasured": (("\t\tif (line_end && index == line_start)", "\t\tif (line_end)"), "wrap"),
}


def generated(fault=None):
    source = read("source/interface/ui_widget.c")
    code = function(source, "ui_widget_port_text_unbreak") + "\n\n" + function(source, "ui_widget_port_text_wrap")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][0])
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", ["unbreak", "wrap"])
def test_case(case):
    status, output = run(build("error_text", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("error_text", generated(control)), NEGATIVE_CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_dialog_unbreaks_before_wrapping():
    renderer = function(read("source/interface/ui_widget.c"), "widget_instance_render_text_box")
    assert "\t\tui_widget_port_text_unbreak(*text);\n\t\tustrncpy(wrapped, *text," in renderer
