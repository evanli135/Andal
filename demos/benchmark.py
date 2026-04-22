"""
Andal vs SQLite Benchmark
=========================
Compares write and query performance across several workloads.

Usage:
    python demos/benchmark.py            # default: 100K events
    python demos/benchmark.py 500000     # 500K events
"""

import sys
import os
import time
import sqlite3
import random
import shutil
import json
import gc
import tracemalloc
from pathlib import Path
from contextlib import contextmanager

sys.path.insert(0, str(Path(__file__).parent.parent))
from andal import EventStore

# ── Config ────────────────────────────────────────────────────────────────────

N          = int(sys.argv[1]) if len(sys.argv) > 1 else 100_000
N_USERS    = 1_000
BENCH_DIR  = Path(__file__).parent / "_bench_data"
T          = 1_735_689_600_000  # base timestamp ms

EVENT_TYPES = ["page_view", "click", "purchase", "signup", "add_to_cart"]
WEIGHTS     = [0.50,        0.25,    0.08,       0.10,     0.07]

# ── Helpers ───────────────────────────────────────────────────────────────────

@contextmanager
def timer():
    gc.collect()
    t0 = time.perf_counter()
    yield
    elapsed = (time.perf_counter() - t0) * 1000  # ms
    timer.last = elapsed


timer.last = 0.0


def fmt_ms(ms: float) -> str:
    if ms >= 1000:
        return f"{ms/1000:.2f}s"
    return f"{ms:.1f}ms"


def generate_events(n: int, seed: int = 42):
    rng = random.Random(seed)
    cumulative = []
    total = 0.0
    for w in WEIGHTS:
        total += w
        cumulative.append(total)

    events = []
    for i in range(n):
        uid = rng.randint(1, N_USERS)
        ts  = T + i * 100 + rng.randint(0, 50)
        r   = rng.random()
        etype = EVENT_TYPES[-1]
        for j, threshold in enumerate(cumulative):
            if r < threshold:
                etype = EVENT_TYPES[j]
                break
        props = json.dumps({"session": rng.randint(1, 100)})
        events.append((etype, uid, ts, props))
    return events


def header(title: str) -> None:
    print(f"\n{'━' * 64}")
    print(f"  {title}")
    print(f"{'━' * 64}")
    print(f"  {'Benchmark':<35} {'Andal':>10} {'SQLite':>10}  {'Winner':>8}")
    print(f"  {'─'*35} {'─'*10} {'─'*10}  {'─'*8}")


def row(label: str, andal_ms: float, sqlite_ms: float) -> None:
    ratio  = sqlite_ms / andal_ms if andal_ms > 0 else float("inf")
    winner = f"Andal {ratio:.1f}x" if ratio > 1.05 else (
             f"SQLite {1/ratio:.1f}x" if ratio < 0.95 else "tie")
    print(f"  {label:<35} {fmt_ms(andal_ms):>10} {fmt_ms(sqlite_ms):>10}  {winner:>10}")


# ── SQLite setup ──────────────────────────────────────────────────────────────

def sqlite_create(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS events (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            event_type TEXT    NOT NULL,
            user_id    INTEGER NOT NULL,
            timestamp  INTEGER NOT NULL,
            properties TEXT
        )
    """)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_type ON events(event_type)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_user ON events(user_id)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_ts   ON events(timestamp)")
    conn.commit()
    return conn


# ── Benchmarks ────────────────────────────────────────────────────────────────

def bench_writes(events):
    header(f"Writes  ({N:,} events, {N_USERS:,} users)")

    # Andal — individual track() calls
    andal_path = str(BENCH_DIR / "andal")
    if os.path.exists(andal_path):
        shutil.rmtree(andal_path)

    with timer():
        db = EventStore(andal_path)
        for etype, uid, ts, props in events:
            db._store.append(etype, user_id=uid, timestamp=ts, properties=props)
        db.flush()
        db.close()
    andal_write = timer.last

    # SQLite — executemany bulk insert
    sqlite_path = str(BENCH_DIR / "bench.db")
    if os.path.exists(sqlite_path):
        os.remove(sqlite_path)

    with timer():
        conn = sqlite_create(sqlite_path)
        conn.executemany(
            "INSERT INTO events(event_type, user_id, timestamp, properties) VALUES (?,?,?,?)",
            events
        )
        conn.commit()
        conn.close()
    sqlite_write = timer.last

    row(f"Insert {N:,} events", andal_write, sqlite_write)
    return andal_write, sqlite_write


def bench_queries(events):
    # Re-open stores (already written by bench_writes)
    andal_path  = str(BENCH_DIR / "andal")
    sqlite_path = str(BENCH_DIR / "bench.db")

    db   = EventStore(andal_path)
    conn = sqlite_create(sqlite_path)

    results = {}

    # ── Filter by event type ──────────────────────────────────────────────────
    header("Read benchmarks")

    with timer():
        r = db.filter(event_type="page_view")
    andal_ms = timer.last
    n_andal = len(r)

    with timer():
        cur = conn.execute("SELECT * FROM events WHERE event_type='page_view'")
        r = cur.fetchall()
    sqlite_ms = timer.last

    row(f"Filter by event_type (~{n_andal:,} rows)", andal_ms, sqlite_ms)
    results["filter_type"] = (andal_ms, sqlite_ms)

    # ── Filter by user_id ─────────────────────────────────────────────────────
    sample_uid = events[0][1]

    with timer():
        r = db.filter(user_id=sample_uid)
    andal_ms = timer.last
    n_andal = len(r)

    with timer():
        cur = conn.execute("SELECT * FROM events WHERE user_id=?", (sample_uid,))
        r = cur.fetchall()
    sqlite_ms = timer.last

    row(f"Filter by user_id (~{n_andal} rows)", andal_ms, sqlite_ms)
    results["filter_user"] = (andal_ms, sqlite_ms)

    # ── Time range: last 25% ──────────────────────────────────────────────────
    quarter = T + (N * 100 * 3 // 4)
    end_ts  = T + N * 150

    with timer():
        r = db.filter(start_time=quarter, end_time=end_ts)
    andal_ms = timer.last
    n_andal = len(r)

    with timer():
        cur = conn.execute(
            "SELECT * FROM events WHERE timestamp BETWEEN ? AND ?", (quarter, end_ts)
        )
        r = cur.fetchall()
    sqlite_ms = timer.last

    row(f"Time range query (~{n_andal:,} rows)", andal_ms, sqlite_ms)
    results["time_range"] = (andal_ms, sqlite_ms)

    # ── count_by event_type ───────────────────────────────────────────────────
    with timer():
        r = db.count_by("event_type")
    andal_ms = timer.last

    with timer():
        cur = conn.execute(
            "SELECT event_type, COUNT(*) FROM events GROUP BY event_type"
        )
        r = cur.fetchall()
    sqlite_ms = timer.last

    row("count_by event_type (GROUP BY)", andal_ms, sqlite_ms)
    results["count_by"] = (andal_ms, sqlite_ms)

    # ── unique user count ─────────────────────────────────────────────────────
    with timer():
        r = db.unique("user_id")
    andal_ms = timer.last

    with timer():
        cur = conn.execute("SELECT COUNT(DISTINCT user_id) FROM events")
        r = cur.fetchone()
    sqlite_ms = timer.last

    row("unique user_id (COUNT DISTINCT)", andal_ms, sqlite_ms)
    results["unique"] = (andal_ms, sqlite_ms)

    # ── Funnel ────────────────────────────────────────────────────────────────
    with timer():
        r = db.funnel(["page_view", "add_to_cart", "purchase"], within=3_600_000)
    andal_ms = timer.last

    with timer():
        # Equivalent funnel in SQL: three self-joins ordered by timestamp
        cur = conn.execute("""
            WITH steps AS (
                SELECT user_id, event_type, timestamp FROM events
                WHERE event_type IN ('page_view','add_to_cart','purchase')
            ),
            s1 AS (SELECT DISTINCT user_id FROM steps WHERE event_type='page_view'),
            s2 AS (
                SELECT DISTINCT s.user_id FROM steps s
                JOIN s1 ON s.user_id = s1.user_id
                WHERE s.event_type='add_to_cart'
                  AND s.timestamp >= (
                    SELECT MIN(timestamp) FROM steps s2
                    WHERE s2.user_id=s.user_id AND s2.event_type='page_view')
            ),
            s3 AS (
                SELECT DISTINCT s.user_id FROM steps s
                JOIN s2 ON s.user_id = s2.user_id
                WHERE s.event_type='purchase'
                  AND s.timestamp >= (
                    SELECT MIN(timestamp) FROM steps s3
                    WHERE s3.user_id=s.user_id AND s3.event_type='add_to_cart')
            )
            SELECT
                (SELECT COUNT(*) FROM s1),
                (SELECT COUNT(*) FROM s2),
                (SELECT COUNT(*) FROM s3)
        """)
        r = cur.fetchone()
    sqlite_ms = timer.last

    row("Funnel (3-step, 1hr window)", andal_ms, sqlite_ms)
    results["funnel"] = (andal_ms, sqlite_ms)

    db.close()
    conn.close()
    return results


def bench_memory(events):
    header("Memory: peak RSS during full table scan")
    andal_path  = str(BENCH_DIR / "andal")
    sqlite_path = str(BENCH_DIR / "bench.db")

    # Andal
    gc.collect()
    tracemalloc.start()
    db = EventStore(andal_path)
    _ = db.filter(event_type="page_view")
    db.close()
    _, andal_peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    # SQLite
    gc.collect()
    tracemalloc.start()
    conn = sqlite_create(sqlite_path)
    cur = conn.execute("SELECT * FROM events WHERE event_type='page_view'")
    _ = cur.fetchall()
    conn.close()
    _, sqlite_peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    def fmt_mem(b):
        if b >= 1024 * 1024:
            return f"{b/1024/1024:.1f}MB"
        return f"{b/1024:.0f}KB"

    andal_str  = fmt_mem(andal_peak)
    sqlite_str = fmt_mem(sqlite_peak)
    ratio      = sqlite_peak / andal_peak if andal_peak > 0 else 1
    winner     = f"Andal {ratio:.1f}x" if ratio > 1.05 else (
                 f"SQLite {1/ratio:.1f}x" if ratio < 0.95 else "tie")

    print(f"  {'page_view full scan':<35} {andal_str:>10} {sqlite_str:>10}  {winner:>10}")


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    BENCH_DIR.mkdir(exist_ok=True)

    print(f"\nAndal vs SQLite  —  {N:,} events, {N_USERS:,} users")
    print(f"Python {sys.version.split()[0]}")

    print("\nGenerating synthetic events...", end=" ", flush=True)
    events = generate_events(N)
    print("done")

    w_andal, w_sqlite = bench_writes(events)
    bench_queries(events)
    bench_memory(events)

    print(f"\n{'━' * 64}\n")
