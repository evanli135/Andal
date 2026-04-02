"""
Fast Event Store - High-performance embedded event store for Python

A columnar, indexed event store optimized for analytics workloads.
"""

from .store import EventStore
from .types import Event, QuerySpec, EventStats

__version__ = "0.1.0"
__all__ = ["EventStore", "Event", "QuerySpec", "EventStats"]
