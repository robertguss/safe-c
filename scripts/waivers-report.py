#!/usr/bin/env python3
"""Print every active waiver; reject invalid or expired entries."""
import sys

from tools import ToolError
from waivers import load_waivers, report_waivers

try:
    report_waivers(load_waivers())
except ToolError as exc:
    print(exc, file=sys.stderr)
    sys.exit(1)
