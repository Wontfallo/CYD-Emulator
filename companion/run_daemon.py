from __future__ import annotations

import os
import sys
import uvicorn

if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    uvicorn.run("backend.app:app", host="127.0.0.1", port=8765, log_level="info")

