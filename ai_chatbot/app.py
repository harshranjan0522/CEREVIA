"""Entry point for the CEREVIA companion service.

Kept at its original path so `python ai_chatbot/app.py` still works; the server
itself lives in server.py.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from server import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
