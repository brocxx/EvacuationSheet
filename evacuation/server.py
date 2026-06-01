"""
server.py — Evacuation Route Planner: Python HTTP Server
Usage: python server.py [fire|gas] [seed]
"""

import http.server
import subprocess
import sys
import os

# ─────────────────────────────────────────────────────────
#  CONFIG
# ─────────────────────────────────────────────────────────

PORT = 8000
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Determine platform executable name
if sys.platform == "win32":
    EXEC_NAME = "evacuation.exe"
    COMPILE_CMD = ["gcc", "-o", EXEC_NAME, "evacuation.c", "-lm"]
    RUN_CMD_BASE = [EXEC_NAME]
else:
    EXEC_NAME = "./evacuation"
    COMPILE_CMD = ["gcc", "-o", "evacuation", "evacuation.c", "-lm"]
    RUN_CMD_BASE = ["./evacuation"]

# ─────────────────────────────────────────────────────────
#  STEP 1 — COMPILE
# ─────────────────────────────────────────────────────────

print("=" * 56)
print("  Evacuation Route Planner — Python Server")
print("=" * 56)
print(f"\n[1/3] Compiling evacuation.c ...")

try:
    result = subprocess.run(
        COMPILE_CMD,
        cwd=SCRIPT_DIR,
        capture_output=True,
        text=True,
        timeout=30
    )
    if result.returncode != 0:
        print(f"\n  ✗ Compilation FAILED:\n{result.stderr}")
        sys.exit(1)
    print(f"  ✓ Compilation successful → {EXEC_NAME}")
except FileNotFoundError:
    print("\n  ✗ ERROR: 'gcc' not found. Please install GCC and ensure it's on PATH.")
    sys.exit(1)
except subprocess.TimeoutExpired:
    print("\n  ✗ Compilation timed out.")
    sys.exit(1)

# ─────────────────────────────────────────────────────────
#  STEP 2 — RUN SIMULATION
# ─────────────────────────────────────────────────────────

# Parse args: python server.py [fire|gas] [seed]
disaster = "fire"
seed = None

if len(sys.argv) >= 2:
    disaster = sys.argv[1].lower()
    if disaster not in ("fire", "gas"):
        print(f"  ⚠ Unknown disaster type '{disaster}', defaulting to 'fire'")
        disaster = "fire"

if len(sys.argv) >= 3:
    seed = sys.argv[2]

run_cmd = list(RUN_CMD_BASE) + [disaster]
if seed is not None:
    run_cmd.append(seed)

print(f"\n[2/3] Running simulation: {' '.join(run_cmd)}")
print(f"      Disaster: {disaster.upper()}{f'  Seed: {seed}' if seed else ''}")
print("      (This may take a few seconds ...)\n")

try:
    sim_result = subprocess.run(
        run_cmd,
        cwd=SCRIPT_DIR,
        capture_output=False,   # Let stdout flow to terminal
        text=True,
        timeout=60
    )
    if sim_result.returncode != 0:
        print(f"\n  ✗ Simulation exited with code {sim_result.returncode}")
        sys.exit(1)
    json_path = os.path.join(SCRIPT_DIR, "simulation.json")
    if not os.path.exists(json_path):
        print("\n  ✗ simulation.json was not created. Check C program output.")
        sys.exit(1)
    print(f"\n  ✓ simulation.json generated successfully")
except FileNotFoundError:
    print(f"\n  ✗ Cannot find {EXEC_NAME}. Compilation may have failed silently.")
    sys.exit(1)
except subprocess.TimeoutExpired:
    print("\n  ✗ Simulation timed out (> 60s).")
    sys.exit(1)

# ─────────────────────────────────────────────────────────
#  STEP 3 — HTTP SERVER
# ─────────────────────────────────────────────────────────

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".css":  "text/css; charset=utf-8",
    ".js":   "application/javascript; charset=utf-8",
}

class EvacuationHandler(http.server.BaseHTTPRequestHandler):

    def log_message(self, format, *args):
        # Suppress default per-request logging noise
        pass

    def do_GET(self):
        # Route "/" → index.html
        path = self.path.split("?")[0]  # strip query string
        if path == "/":
            path = "/index.html"

        # Map to filesystem
        safe_path = path.lstrip("/").replace("/", os.sep)
        file_path = os.path.join(SCRIPT_DIR, safe_path)

        # Security: stay inside SCRIPT_DIR
        abs_file = os.path.realpath(file_path)
        abs_base = os.path.realpath(SCRIPT_DIR)
        if not abs_file.startswith(abs_base):
            self.send_error(403, "Forbidden")
            return

        if not os.path.isfile(abs_file):
            self.send_error(404, f"File not found: {path}")
            return

        _, ext = os.path.splitext(abs_file)
        content_type = CONTENT_TYPES.get(ext.lower(), "application/octet-stream")

        try:
            with open(abs_file, "rb") as f:
                data = f.read()
        except OSError as e:
            self.send_error(500, str(e))
            return

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        # Allow cross-origin (helps during dev)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        import json
        import random
        if self.path == '/api/generate_random':
            seed = str(random.randint(1, 1000000))
            run_cmd = list(RUN_CMD_BASE) + [disaster, seed]
            subprocess.run(run_cmd, cwd=SCRIPT_DIR, capture_output=True, text=True)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({'status': 'ok', 'seed': seed}).encode('utf-8'))
            return
        elif self.path == '/api/simulate_custom':
            content_length = int(self.headers.get('Content-Length', 0))
            if content_length > 0:
                post_data = self.rfile.read(content_length)
                try:
                    data = json.loads(post_data.decode('utf-8'))
                    grid = data.get('grid', [])
                    map_path = os.path.join(SCRIPT_DIR, "custom_map.txt")
                    with open(map_path, "w") as f:
                        for row in grid:
                            f.write(row + "\n")
                    run_cmd = list(RUN_CMD_BASE) + ["--map", "custom_map.txt"]
                    subprocess.run(run_cmd, cwd=SCRIPT_DIR, capture_output=True, text=True)
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps({'status': 'ok'}).encode('utf-8'))
                    return
                except Exception as e:
                    self.send_error(400, f"Bad Request: {str(e)}")
                    return
            self.send_error(400, "Bad Request")
            return
            
        self.send_error(404, "Not Found")


print(f"\n[3/3] Starting HTTP server on port {PORT} ...")
print(f"\n{'=' * 56}")
print(f"  ✓ Simulation ready!")
print(f"  Open http://localhost:{PORT} in your browser")
print(f"{'=' * 56}\n")
print("  Press Ctrl+C to stop.\n")

try:
    httpd = http.server.HTTPServer(("", PORT), EvacuationHandler)
    httpd.serve_forever()
except KeyboardInterrupt:
    print("\n  Server stopped.")
    sys.exit(0)
