"""
Andal vs MySQL Evaluation Framework
=====================================
Measures write throughput, query latency, and memory across several
analytics workloads representative of real product-analytics usage.

Requirements:
    pip install mysql-connector-python

MySQL connection is configured via environment variables (or edit MYSQL_CONFIG):
    MYSQL_HOST     default: localhost
    MYSQL_PORT     default: 3306
    MYSQL_USER     default: root
    MYSQL_PASSWORD default: (empty)
    MYSQL_DB       default: andal_bench

Usage:
    python demos/eval_mysql.py                  # 100K events
    python demos/eval_mysql.py 500000           # 500K events
    python demos/eval_mysql.py 100000 --no-mysql  # Andal only (no MySQL needed)
"""

import sys
import os
import time
import json
import random
import shutil
import gc
import tracemalloc
import argparse
from pathlib import Path
from contextlib import contextmanager

sys.path.insert(0, str(Path(__file__).parent.parent))
from andal import EventStore

# -- Config --------------------------------------------------------------------

MYSQL_CONFIG = {
    "host":     os.getenv("MYSQL_HOST",     "localhost"),
    "port":     int(os.getenv("MYSQL_PORT", "3306")),
    "user":     os.getenv("MYSQL_USER",     "root"),
    "password": os.getenv("MYSQL_PASSWORD", ""),
    "database": os.getenv("MYSQL_DB",       "andal_bench"),
}

BENCH_DIR   = Path(__file__).parent / "_bench_mysql"
T           = 1_735_689_600_000  # 2025-01-01 00:00:00 UTC in ms

EVENT_TYPES = ["page_view", "click", "purchase", "signup", "add_to_cart"]
WEIGHTS     = [0.50,        0.25,    0.08,       0.10,     0.07]

# -- CLI args ------------------------------------------------------------------

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("n_events",   nargs="?", type=int, default=100_000)
parser.add_argument("--no-mysql", action="store_true")
args, _ = parser.parse_known_args()

N        = args.n_events
NO_MYSQL = args.no_mysql
N_USERS  = max(100, N // 100)

# -- Timing / memory helpers ---------------------------------------------------

class Result:
    def __init__(self):
        self.elapsed_ms = 0.0
        self.peak_kb    = 0.0
        self.row_count  = 0


@contextmanager
def measure(result: Result):
    gc.collect()
    tracemalloc.start()
    t0 = time.perf_counter()
    yield
    result.elapsed_ms = (time.perf_counter() - t0) * 1000
    _, peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    result.peak_kb = peak / 1024


def fmt_ms(ms: float) -> str:
    return f"{ms/1000:.2f}s" if ms >= 1000 else f"{ms:.1f}ms"


def fmt_kb(kb: float) -> str:
    return f"{kb/1024:.1f}MB" if kb >= 1024 else f"{kb:.0f}KB"


def speedup(andal_ms: float, mysql_ms: float) -> str:
    if NO_MYSQL or mysql_ms == 0:
        return "N/A"
    r = mysql_ms / andal_ms
    if r > 1.05:
        return f"Andal  {r:.1f}x"
    if r < 0.95:
        return f"MySQL  {1/r:.1f}x"
    return "tie"


# -- Data generation -----------------------------------------------------------

def generate_events(n: int, seed: int = 42):
    rng = random.Random(seed)
    cum = []
    s = 0.0
    for w in WEIGHTS:
        s += w
        cum.append(s)

    out = []
    for i in range(n):
        uid   = rng.randint(1, N_USERS)
        ts    = T + i * 100 + rng.randint(0, 50)
        r     = rng.random()
        etype = EVENT_TYPES[-1]
        for j, thresh in enumerate(cum):
            if r < thresh:
                etype = EVENT_TYPES[j]
                break
        props = json.dumps({"session": rng.randint(1, 500), "v": rng.randint(1, 3)})
        out.append((etype, uid, ts, props))
    return out


# -- MySQL helpers -------------------------------------------------------------

def mysql_connect():
    try:
        import mysql.connector
    except ImportError:
        print("ERROR: mysql-connector-python not installed.")
        print("       pip install mysql-connector-python")
        sys.exit(1)

    # Connect without database first to create it if needed
    cfg = dict(MYSQL_CONFIG)
    db_name = cfg.pop("database")
    conn = mysql.connector.connect(**cfg)
    cur  = conn.cursor()
    cur.execute(f"CREATE DATABASE IF NOT EXISTS `{db_name}`")
    cur.execute(f"USE `{db_name}`")
    conn.database = db_name
    return conn


def mysql_setup(conn) -> None:
    cur = conn.cursor()
    cur.execute("DROP TABLE IF EXISTS events")
    cur.execute("""
        CREATE TABLE events (
            id         BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            event_type VARCHAR(64)  NOT NULL,
            user_id    BIGINT UNSIGNED NOT NULL,
            timestamp  BIGINT UNSIGNED NOT NULL,
            properties JSON
        ) ENGINE=InnoDB
    """)
    # Covering indexes matching common query patterns
    cur.execute("CREATE INDEX idx_type ON events(event_type)")
    cur.execute("CREATE INDEX idx_user ON events(user_id)")
    cur.execute("CREATE INDEX idx_ts   ON events(timestamp)")
    conn.commit()


# -- Benchmark suites ----------------------------------------------------------

def run_writes(events, mysql_conn):
    print(f"\n{'=' * 68}")
    print(f"  WRITES  ({N:,} events, {N_USERS:,} users)")
    print(f"{'=' * 68}")
    print(f"  {'Workload':<38} {'Andal':>9} {'MySQL':>9}  {'Winner':>12}")
    print(f"  {'-'*38} {'-'*9} {'-'*9}  {'-'*12}")

    # -- Andal -----------------------------------------------------------------
    andal_path = str(BENCH_DIR / "andal")
    if os.path.exists(andal_path):
        shutil.rmtree(andal_path)

    ar = Result()
    with measure(ar):
        db = EventStore(andal_path)
        for etype, uid, ts, props in events:
            db._store.append(etype, user_id=uid, timestamp=ts, properties=props)
        db.flush()
        db.close()

    # -- MySQL -----------------------------------------------------------------
    mr = Result()
    if not NO_MYSQL:
        mysql_setup(mysql_conn)
        with measure(mr):
            cur = mysql_conn.cursor()
            cur.executemany(
                "INSERT INTO events(event_type, user_id, timestamp, properties) "
                "VALUES (%s, %s, %s, %s)",
                events
            )
            mysql_conn.commit()

    print(f"  {'Insert (individual appends)':<38} {fmt_ms(ar.elapsed_ms):>9} "
          f"{fmt_ms(mr.elapsed_ms) if not NO_MYSQL else 'N/A':>9}  "
          f"{speedup(ar.elapsed_ms, mr.elapsed_ms):>12}")

    # Throughput line
    andal_tps = N / (ar.elapsed_ms / 1000)
    mysql_tps = N / (mr.elapsed_ms / 1000) if not NO_MYSQL and mr.elapsed_ms > 0 else 0
    print(f"  {'  -> throughput (events/sec)':<38} {andal_tps:>9,.0f} "
          f"{mysql_tps:>9,.0f}" if not NO_MYSQL else
          f"  {'  -> throughput (events/sec)':<38} {andal_tps:>9,.0f} {'N/A':>9}")

    return ar, mr


def run_reads(mysql_conn):
    print(f"\n{'=' * 68}")
    print(f"  READS")
    print(f"{'=' * 68}")
    print(f"  {'Workload':<38} {'Andal':>9} {'MySQL':>9}  {'Winner':>12}")
    print(f"  {'-'*38} {'-'*9} {'-'*9}  {'-'*12}")

    andal_path = str(BENCH_DIR / "andal")
    db = EventStore(andal_path)

    def bench(label, andal_fn, mysql_sql, mysql_params=()):
        ar = Result()
        with measure(ar):
            result = andal_fn()
            ar.row_count = len(result) if hasattr(result, "__len__") else 1

        mr = Result()
        if not NO_MYSQL:
            with measure(mr):
                cur = mysql_conn.cursor()
                cur.execute(mysql_sql, mysql_params)
                rows = cur.fetchall()
                mr.row_count = len(rows)

        rows_str = f"~{ar.row_count:,} rows"
        print(f"  {label+' ('+rows_str+')':<38} {fmt_ms(ar.elapsed_ms):>9} "
              f"{fmt_ms(mr.elapsed_ms) if not NO_MYSQL else 'N/A':>9}  "
              f"{speedup(ar.elapsed_ms, mr.elapsed_ms):>12}")
        return ar, mr

    # Filter by event type
    bench(
        "Filter: event_type='page_view'",
        lambda: db.filter(event_type="page_view"),
        "SELECT * FROM events WHERE event_type='page_view'",
    )

    # Filter by user
    bench(
        "Filter: user_id=1",
        lambda: db.filter(user_id=1),
        "SELECT * FROM events WHERE user_id=%s", (1,),
    )

    # Time range: last 25%
    quarter_start = T + (N * 100 * 3 // 4)
    end_ts        = T + N * 200
    bench(
        "Filter: time range (last 25%)",
        lambda: db.filter(start_time=quarter_start, end_time=end_ts),
        "SELECT * FROM events WHERE timestamp BETWEEN %s AND %s",
        (quarter_start, end_ts),
    )

    # Combined filter
    bench(
        "Filter: type + user_id",
        lambda: db.filter(event_type="page_view", user_id=1),
        "SELECT * FROM events WHERE event_type=%s AND user_id=%s",
        ("page_view", 1),
    )

    # count_by / GROUP BY
    bench(
        "Aggregate: count by event_type",
        lambda: db.count_by("event_type"),
        "SELECT event_type, COUNT(*) FROM events GROUP BY event_type",
    )

    # unique / COUNT DISTINCT
    bench(
        "Aggregate: unique user_ids",
        lambda: [db.unique("user_id")],
        "SELECT COUNT(DISTINCT user_id) FROM events",
    )

    # Funnel
    bench(
        "Funnel: 3-step, 1hr window",
        lambda: db.funnel(["page_view", "add_to_cart", "purchase"], within=3_600_000),
        """
        WITH pv AS (
            SELECT user_id, MIN(timestamp) ts FROM events
            WHERE event_type='page_view' GROUP BY user_id
        ),
        ac AS (
            SELECT e.user_id, MIN(e.timestamp) ts FROM events e
            JOIN pv ON e.user_id=pv.user_id
            WHERE e.event_type='add_to_cart' AND e.timestamp >= pv.ts
              AND e.timestamp <= pv.ts + 3600000
            GROUP BY e.user_id
        ),
        pu AS (
            SELECT e.user_id FROM events e
            JOIN ac ON e.user_id=ac.user_id
            WHERE e.event_type='purchase' AND e.timestamp >= ac.ts
              AND e.timestamp <= ac.ts + 3600000
            GROUP BY e.user_id
        )
        SELECT
            (SELECT COUNT(*) FROM pv),
            (SELECT COUNT(*) FROM ac),
            (SELECT COUNT(*) FROM pu)
        """,
    )

    db.close()


def run_memory(mysql_conn):
    print(f"\n{'=' * 68}")
    print(f"  MEMORY  (peak during page_view full scan)")
    print(f"{'=' * 68}")
    print(f"  {'Metric':<38} {'Andal':>9} {'MySQL':>9}")
    print(f"  {'-'*38} {'-'*9} {'-'*9}")

    andal_path = str(BENCH_DIR / "andal")

    gc.collect()
    tracemalloc.start()
    db = EventStore(andal_path)
    _ = db.filter(event_type="page_view")
    db.close()
    _, andal_peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    mysql_peak = 0
    if not NO_MYSQL:
        gc.collect()
        tracemalloc.start()
        cur = mysql_conn.cursor()
        cur.execute("SELECT * FROM events WHERE event_type='page_view'")
        _ = cur.fetchall()
        _, mysql_peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()

    print(f"  {'Peak heap (Python-visible)':<38} {fmt_kb(andal_peak/1024):>9} "
          f"{fmt_kb(mysql_peak/1024) if not NO_MYSQL else 'N/A':>9}")


# -- Entry point ---------------------------------------------------------------

if __name__ == "__main__":
    BENCH_DIR.mkdir(exist_ok=True)

    print(f"\nAndal vs MySQL  --  {N:,} events  |  {N_USERS:,} users  |  "
          f"{'MySQL disabled' if NO_MYSQL else MYSQL_CONFIG['host']+':'+str(MYSQL_CONFIG['port'])}")

    mysql_conn = None
    if not NO_MYSQL:
        print("Connecting to MySQL...", end=" ", flush=True)
        mysql_conn = mysql_connect()
        print("ok")

    print("Generating events...", end=" ", flush=True)
    events = generate_events(N)
    print("done")

    run_writes(events, mysql_conn)
    run_reads(mysql_conn)
    run_memory(mysql_conn)

    if mysql_conn:
        mysql_conn.close()

    print(f"\n{'=' * 68}\n")
