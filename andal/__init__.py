"""
andal — High-performance embedded event store for Python
"""

from .store import EventStore
from .types import Event

__version__ = "0.1.0"
__all__ = ["EventStore", "Event"]
