from __future__ import annotations

import argparse
import threading
import time
import uvicorn


def run_server(host: str, port: int) -> None:
    uvicorn.run("backend.app:app", host=host, port=port, log_level="warning")


def ensure_std_streams() -> None:
    """Give the process a stdout/stderr when it has none.

    Started with pythonw.exe (launch_hydrodeck.bat, a shortcut, a double-click) there is no
    console and sys.stdout / sys.stderr are None. uvicorn's log setup calls sys.stdout.isatty()
    and dies on that, so the window would open with no server behind it. Send both to a log
    file instead.
    """
    import sys
    from pathlib import Path

    if sys.stdout is not None and sys.stderr is not None:
        return
    log_path = Path(__file__).resolve().parent / "data" / "studio.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log = open(log_path, "a", buffering=1, encoding="utf-8", errors="replace")
    if sys.stdout is None:
        sys.stdout = log
    if sys.stderr is None:
        sys.stderr = log


def main() -> None:
    parser = argparse.ArgumentParser(description="HYDRO-DECK Studio Desktop App")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--web", action="store_true", help="Serve web UI without pywebview desktop shell")
    args = parser.parse_args()
    ensure_std_streams()

    srv = threading.Thread(target=run_server, args=(args.host, args.port), daemon=True)
    srv.start()
    time.sleep(0.9)

    url = f"http://{args.host}:{args.port}"
    if args.web:
        print(f"HYDRO-DECK Studio running at {url}")
        srv.join()
        return

    try:
        import webview
        webview.create_window(
            "HYDRO-DECK Studio — E32R28T Command Matrix",
            url,
            width=1440,
            height=900,
            min_size=(1160, 740),
            background_color="#06080d",
        )
        webview.start()
    except Exception as exc:
        print(f"Desktop webview unavailable ({exc}); open {url} in browser.")
        srv.join()


if __name__ == "__main__":
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    main()

