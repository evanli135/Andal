"""
Andal Playground
================
Seeds a realistic e-commerce event store and runs example queries.

Run interactively to keep the store open for manual exploration:

    python -i demos/playground.py

Variables available after seeding:
    db          EventStore (open, ready to query)
    T           base timestamp (ms)
    USER_IDS    list of user IDs in the dataset
"""

import sys
import os
import json
import time
import random
import shutil
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from andal import EventStore

# ── Config ────────────────────────────────────────────────────────────────────

DB_PATH   = str(Path(__file__).parent / "_playground_data")
N_USERS   = 200
N_EVENTS  = 5_000
T         = 1_735_689_600_000  # 2025-01-01 00:00:00 UTC in ms
USER_IDS  = list(range(1, N_USERS + 1))

PAGES     = ["/home", "/pricing", "/docs", "/blog", "/signup", "/dashboard"]
BUTTONS   = ["signup", "login", "buy_now", "add_to_cart", "learn_more"]
PRODUCTS  = ["pro_monthly", "pro_annual", "team_monthly", "enterprise"]

# ── Seed ──────────────────────────────────────────────────────────────────────

def seed(db: EventStore, n: int = N_EVENTS) -> None:
    rng = random.Random(42)
    print(f"Seeding {n:,} events across {N_USERS} users...")

    for i in range(n):
        uid  = rng.choice(USER_IDS)
        ts   = T + i * 3_000 + rng.randint(-1000, 1000)
        roll = rng.random()

        if roll < 0.55:
            db.track("page_view",  user_id=uid, timestamp=ts,
                     page=rng.choice(PAGES), session=rng.randint(1, 50))
        elif roll < 0.75:
            db.track("click",      user_id=uid, timestamp=ts,
                     button=rng.choice(BUTTONS))
        elif roll < 0.88:
            db.track("signup",     user_id=uid, timestamp=ts,
                     plan=rng.choice(["free", "trial"]))
        elif roll < 0.96:
            db.track("add_to_cart", user_id=uid, timestamp=ts,
                     product=rng.choice(PRODUCTS))
        else:
            db.track("purchase",   user_id=uid, timestamp=ts,
                     product=rng.choice(PRODUCTS),
                     amount=rng.choice([9.99, 99.0, 199.0, 499.0]))

    db.flush()
    print(f"  {db.size():,} events stored.\n")


# ── Demo queries ──────────────────────────────────────────────────────────────

def _section(title: str) -> None:
    print(f"\n{'─' * 60}")
    print(f"  {title}")
    print(f"{'─' * 60}")


def run_demo(db: EventStore) -> None:
    _section("Event counts by type")
    counts = db.event_counts()
    for k, v in sorted(counts.items(), key=lambda x: -x[1]):
        bar = "█" * (v * 30 // max(counts.values()))
        print(f"  {k:<15} {v:>6,}  {bar}")

    _section("Unique users per event type")
    for etype in sorted(counts):
        u = db.unique("user_id", event_type=etype)
        print(f"  {etype:<15} {u:>4} unique users")

    _section("Purchases (first 5)")
    purchases = db.filter(event_type="purchase")
    print(f"  Total purchases: {len(purchases)}")
    for p in purchases[:5]:
        props = p["properties"] or {}
        print(f"  user={p['user_id']:<4}  product={props.get('product','?'):<14}  "
              f"amount=${props.get('amount', 0):.2f}")

    _section("Revenue by product")
    revenue: dict = {}
    for p in purchases:
        prod = (p["properties"] or {}).get("product", "unknown")
        revenue[prod] = revenue.get(prod, 0.0) + (p["properties"] or {}).get("amount", 0)
    for prod, amt in sorted(revenue.items(), key=lambda x: -x[1]):
        print(f"  {prod:<16}  ${amt:>8,.2f}")

    _section("Funnel: page_view → add_to_cart → purchase (1-hour window)")
    funnel = db.funnel(
        steps=["page_view", "add_to_cart", "purchase"],
        within=3_600_000
    )
    for step in funnel:
        bar = "█" * int(step["conversion_rate"] * 40)
        pct = step["conversion_rate"] * 100
        print(f"  {step['step']:<15} {step['users']:>5} users  "
              f"({pct:5.1f}%)  {bar}")

    _section("Time range: first 30 minutes only")
    window_end = T + 30 * 60 * 1000
    recent = db.filter(start_time=T, end_time=window_end)
    print(f"  {len(recent):,} events in first 30 minutes")
    sub_counts: dict = {}
    for e in recent:
        sub_counts[e["event_type"]] = sub_counts.get(e["event_type"], 0) + 1
    for k, v in sorted(sub_counts.items(), key=lambda x: -x[1]):
        print(f"    {k:<15} {v}")

    _section("Activity for user 1")
    u1 = db.filter(user_id=1)
    print(f"  {len(u1)} events")
    u1_counts: dict = {}
    for e in u1:
        u1_counts[e["event_type"]] = u1_counts.get(e["event_type"], 0) + 1
    for k, v in u1_counts.items():
        print(f"    {k:<15} {v}")

    first = db.first(user_id=1)
    last  = db.last(user_id=1)
    if first and last:
        span_min = (last["timestamp"] - first["timestamp"]) / 60_000
        print(f"  Span: {span_min:.1f} minutes")

    print()


# ── Main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__" or True:
    # Wipe and reseed on each run so the playground is predictable
    if os.path.exists(DB_PATH):
        shutil.rmtree(DB_PATH)

    db = EventStore(DB_PATH)
    seed(db)
    run_demo(db)

    # When run with python -i, db stays open for interactive use.
    # Try: db.filter(event_type="purchase", user_id=42)
    #      db.count_by("user_id", event_type="purchase")
    print("db is open. Try: db.filter(event_type='purchase')")
    print("                 db.count_by('user_id', event_type='purchase')")
    print("                 db.funnel(['signup', 'purchase'])")
