import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

import chess  # pip install python-chess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# --------------------------
# EPD-ish parsing
# --------------------------

MOVE_RE = re.compile(r"\b(?P<kind>bm|am)\s+(?P<moves>[^;]+);", re.IGNORECASE)
ID_RE = re.compile(r'\bid\s+"(?P<id>[^"]+)"\s*;', re.IGNORECASE)

@dataclass
class TestCase:
    id: str
    fen: str          # first 4 fields only
    kind: str         # bm or am
    moves_san: str    # may contain multiple SAN tokens


def parse_epd_like_line(line: str, fallback_id: str) -> Optional[TestCase]:
    line = line.strip()
    if not line or line.startswith("#"):
        return None

    parts = line.split()
    if len(parts) < 4:
        raise ValueError(f"Line too short to contain FEN: {line}")

    fen = " ".join(parts[:4])

    m_move = MOVE_RE.search(line)
    if not m_move:
        # Some suites have positions without bm/am; skip them
        return None

    kind = m_move.group("kind").lower().strip()
    moves = m_move.group("moves").strip()

    m_id = ID_RE.search(line)
    tid = m_id.group("id").strip() if m_id else fallback_id

    return TestCase(id=tid, fen=fen, kind=kind, moves_san=moves)


def expected_uci_moves(fen: str, moves_field: str) -> List[str]:
    """
    Convert bm/am move field into a list of UCI moves.
    Treats tokens as alternatives (OR). Skips unparsable tokens.
    """
    board = chess.Board(fen)

    # Split on whitespace and common separators
    tokens = re.split(r"[,\|]\s*|\s+", moves_field.strip())
    tokens = [t.strip() for t in tokens if t.strip()]

    ucis: List[str] = []
    for san in tokens:
        san = san.rstrip("!?")
        try:
            mv = board.parse_san(san)
            ucis.append(mv.uci())
        except Exception:
            continue

    if not ucis:
        raise ValueError(f"No parseable SAN moves in field: {moves_field!r} for FEN: {fen}")

    # Dedup preserve order
    seen = set()
    out = []
    for u in ucis:
        if u not in seen:
            seen.add(u)
            out.append(u)
    return out


# --------------------------
# UCI engine subprocess
# --------------------------

class UCIEngine:
    def __init__(self, path: str, cwd: Optional[str] = None):
        self.path = os.path.abspath(path)
        if cwd is None:
            cwd = os.path.dirname(self.path)

        self.p = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=cwd,
        )
        assert self.p.stdin and self.p.stdout and self.p.stderr

        # tiny grace period for Windows process startup
        time.sleep(0.05)
        self._check_dead("startup")
        self._uci_handshake()

    def _check_dead(self, where: str) -> None:
        rc = self.p.poll()
        if rc is not None:
            out = self.p.stdout.read() if self.p.stdout else ""
            err = self.p.stderr.read() if self.p.stderr else ""
            raise RuntimeError(
                f"Engine exited ({where}) with code {rc}\n"
                f"--- stdout ---\n{out}\n"
                f"--- stderr ---\n{err}\n"
            )

    def _send(self, cmd: str) -> None:
        self._check_dead(f"before send '{cmd}'")
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def _readline(self) -> str:
        line = self.p.stdout.readline()
        if line == "":
            self._check_dead("readline EOF")
            raise RuntimeError("EOF on stdout but process still alive (unexpected).")
        return line.rstrip("\n")

    def _read_until(self, token: str, max_lines: int = 200000) -> List[str]:
        lines = []
        for _ in range(max_lines):
            self._check_dead(f"waiting for '{token}'")
            ln = self._readline()
            lines.append(ln)
            if token in ln:
                return lines
        raise RuntimeError(f"Did not see '{token}' after {max_lines} lines. Last lines: {lines[-10:]}")

    def _uci_handshake(self) -> None:
        self._send("uci")
        self._read_until("uciok")
        self._send("isready")
        self._read_until("readyok")

    def set_option(self, name: str, value: str) -> None:
        self._send(f"setoption name {name} value {value}")

    def sync_ready(self) -> None:
        self._send("isready")
        self._read_until("readyok")

    def set_position_fen(self, fen: str) -> None:
        self._send(f"position fen {fen}")
        self.sync_ready()

    def go_movetime(self, movetime_ms: int) -> Tuple[str, List[str]]:
        self._send(f"go movetime {movetime_ms}")
        info_lines = []
        while True:
            self._check_dead("during go")
            ln = self._readline()
            if ln.startswith("bestmove "):
                parts = ln.split()
                best = parts[1] if len(parts) > 1 else "0000"
                return best, info_lines
            info_lines.append(ln)

    def quit(self) -> None:
        try:
            if self.p.poll() is None:
                self._send("quit")
                try:
                    self.p.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    pass
        finally:
            if self.p.poll() is None:
                self.p.kill()


# --------------------------
# Reporting helpers
# --------------------------

INFO_DEPTH_RE = re.compile(r"\bdepth\s+(\d+)\b")
INFO_TIME_RE  = re.compile(r"\btime\s+(\d+)\b")
INFO_NODES_RE = re.compile(r"\bnodes\s+(\d+)\b")
INFO_SCORE_RE = re.compile(r"\bscore\s+(cp|mate)\s+(-?\d+)\b")

def extract_info_fields(info_lines: List[str]) -> dict:
    """
    Pull a few common UCI 'info' fields from the *last* info line that contains them.
    """
    out = {}
    for ln in reversed(info_lines):
        if " depth " in f" {ln} " and " nodes " in f" {ln} ":
            m = INFO_DEPTH_RE.search(ln)
            if m: out["depth"] = int(m.group(1))
            m = INFO_TIME_RE.search(ln)
            if m: out["time_ms"] = int(m.group(1))
            m = INFO_NODES_RE.search(ln)
            if m: out["nodes"] = int(m.group(1))
            m = INFO_SCORE_RE.search(ln)
            if m:
                out["score_type"] = m.group(1)
                out["score"] = int(m.group(2))
            break
    return out

def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def git_cmd(args: List[str]) -> Optional[str]:
    try:
        return subprocess.check_output(["git"] + args, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return None

def ensure_dir(path: str) -> None:
    os.makedirs(path, exist_ok=True)

def now_utc_stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d_%H-%M-%SZ")


# --------------------------
# Main run
# --------------------------

def run_suite(engine_path: str, test_file: str, movetime_ms: int,
              record: bool, results_dir: str,
              threads: Optional[int], hash_mb: Optional[int]) -> None:
    
    results_dir = os.path.abspath(results_dir)
    test_file = os.path.abspath(test_file)
    suite_name = os.path.splitext(os.path.basename(test_file))[0]
    suite_hash = sha256_file(test_file)

    commit_full = git_cmd(["rev-parse", "HEAD"]) or "nogit"
    commit_short = git_cmd(["rev-parse", "--short", "HEAD"]) or "nogit"
    commit_time = git_cmd(["show", "-s", "--format=%cI", "HEAD"]) or None
    branch = git_cmd(["rev-parse", "--abbrev-ref", "HEAD"]) or None

    # Load cases
    cases: List[TestCase] = []
    with open(test_file, "r", encoding="utf-8") as f:
        for i, raw in enumerate(f, start=1):
            tc = parse_epd_like_line(raw, fallback_id=f"line_{i:05d}")
            if tc:
                cases.append(tc)

    if not cases:
        raise RuntimeError("No bm/am test cases parsed from suite.")

    # Start engine
    eng = UCIEngine(engine_path)

    # Apply options for reproducibility (if your engine supports them)
    if threads is not None:
        eng.set_option("Threads", str(threads))
    if hash_mb is not None:
        eng.set_option("Hash", str(hash_mb))
    if threads is not None or hash_mb is not None:
        eng.sync_ready()

    # Prepare recording
    run_path = None
    run_f = None
    if record:
        ensure_dir(os.path.join(results_dir, "runs"))
        stamp = now_utc_stamp()
        run_path = os.path.join(results_dir, "runs", f"{stamp}__{suite_name}__{commit_short}.jsonl")
        run_path_rel = os.path.relpath(run_path, start=SCRIPT_DIR)
        run_f = open(run_path, "w", encoding="utf-8")

        run_meta = {
            "type": "run_meta",
            "timestamp_utc": stamp,
            "suite_name": suite_name,
            "suite_path": test_file,
            "suite_sha256": suite_hash,
            "movetime_ms": movetime_ms,
            "engine_path": os.path.abspath(engine_path),
            "git_commit": commit_full,
            "git_commit_short": commit_short,
            "git_commit_time": commit_time,
            "git_branch": branch,
            "options": {"Threads": threads, "Hash": hash_mb},
            "host": {
                "platform": platform.platform(),
                "python": sys.version.split()[0],
            }
        }
        run_f.write(json.dumps(run_meta) + "\n")
        run_f.flush()

    # Run evaluation
    bm_total = bm_pass = 0
    am_total = am_pass = 0

    for tc in cases:
        expected_ucis = expected_uci_moves(tc.fen, tc.moves_san)

        eng.set_position_fen(tc.fen)
        got_uci, info_lines = eng.go_movetime(movetime_ms)
        info_fields = extract_info_fields(info_lines)

        if tc.kind == "bm":
            bm_total += 1
            ok = got_uci in expected_ucis
            if ok: bm_pass += 1
        else:
            am_total += 1
            ok = got_uci not in expected_ucis
            if ok: am_pass += 1

        if run_f:
            rec = {
                "type": "case",
                "id": tc.id,
                "fen": tc.fen,
                "kind": tc.kind,
                "moves_san": tc.moves_san,
                "expected_uci": expected_ucis,
                "got_uci": got_uci,
                "ok": ok,
            }
            rec.update(info_fields)
            run_f.write(json.dumps(rec) + "\n")

    eng.quit()
    if run_f:
        run_f.flush()
        run_f.close()

    total = bm_total + am_total
    passed = bm_pass + am_pass

    bm_acc = (bm_pass / bm_total) if bm_total else None
    am_acc = (am_pass / am_total) if am_total else None
    overall = passed / total if total else 0.0

    print(f"Suite: {suite_name} | movetime={movetime_ms}ms | commit={commit_short}")
    if bm_total:
        print(f"  bm hit-rate:     {bm_pass}/{bm_total} = {bm_acc:.1%}")
    if am_total:
        print(f"  am avoid-rate:   {am_pass}/{am_total} = {am_acc:.1%}")
    print(f"  overall:         {passed}/{total} = {overall:.1%}")

    # Append index row
    if record:
        index_path = os.path.join(results_dir, "index.csv")
        ensure_dir(results_dir)
        new_file = not os.path.exists(index_path)

        with open(index_path, "a", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            if new_file:
                w.writerow([
                    "timestamp_utc","suite","suite_sha256","movetime_ms",
                    "commit","commit_short","branch","commit_time",
                    "threads","hash_mb",
                    "total","passed","overall",
                    "bm_total","bm_pass","bm_rate",
                    "am_total","am_pass","am_rate",
                    "run_file"
                ])
            w.writerow([
                now_utc_stamp(), suite_name, suite_hash, movetime_ms,
                commit_full, commit_short, branch, commit_time,
                threads, hash_mb,
                total, passed, overall,
                bm_total, bm_pass, bm_acc if bm_acc is not None else "",
                am_total, am_pass, am_acc if am_acc is not None else "",
                run_path_rel
            ])

        print(f"\nWrote run:   {run_path}")
        print(f"Updated:     {index_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine", help="Path to your UCI engine executable")
    ap.add_argument("suite", help="Path to suite file (EPD-like)")
    ap.add_argument("movetime_ms", type=int, nargs="?", default=50)
    ap.add_argument("--record", action="store_true", help="Write per-run JSONL + append index.csv")
    ap.add_argument("--results-dir", default=os.path.join(SCRIPT_DIR, "results"), help="Directory to write benchmark results")
    ap.add_argument("--threads", type=int, default=None)
    ap.add_argument("--hash", dest="hash_mb", type=int, default=None)
    args = ap.parse_args()

    run_suite(
        engine_path=args.engine,
        test_file=args.suite,
        movetime_ms=args.movetime_ms,
        record=args.record,
        results_dir=args.results_dir,
        threads=args.threads,
        hash_mb=args.hash_mb,
    )

if __name__ == "__main__":
    main()