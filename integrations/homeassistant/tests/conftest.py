"""pytest configuration for the ePaper Clock integration unit tests.

These tests exercise the PURE message-selection logic and must run WITHOUT a Home
Assistant installation. The component code lives in
``custom_components/epaper_clock/``; we add that directory to sys.path so
``import message_selection`` works, then collect ONLY the tests here (the tests are
outside the package tree so pytest never imports the component __init__.py).
"""

import os
import sys

_COMPONENT_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "custom_components", "epaper_clock")
)
if _COMPONENT_DIR not in sys.path:
    sys.path.insert(0, _COMPONENT_DIR)
