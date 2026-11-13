from __future__ import annotations

import argparse
import threading
import time
import uvicorn


def run_server(host: str, port: int) -> None:
    uvicorn.run("backend.app:app", host=host, port=port, log_level="warning")


def main() -> None:
    parser = argparse.ArgumentParser(description="HYDRO-DECK Studio Desktop App")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--web", action="store_true", help="Serve web UI without pywebview desktop shell")
    args = parser.parse_args()

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

