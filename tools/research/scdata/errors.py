"""Common base of the converter's own errors, so the CLI reports every rejected input the
same way (one line, exit status 1) instead of a traceback."""
from __future__ import annotations


class ConverterError(Exception):
    """Input the converter rejects: malformed or outside the supported corpus."""
