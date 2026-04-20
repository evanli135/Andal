"""
Basic usage example for Fast Event Store
"""

import time
from andal import EventStore


def main():
    # Create/open event store
    with EventStore("example_events.db") as db:
        # Track some events
        now = int(time.time() * 1000)

        print("Tracking events...")
        db.track("page_view", user_id=123, page="/home", timestamp=now)
        db.track("page_view", user_id=123, page="/pricing", timestamp=now + 1000)
        db.track("click", user_id=123, button="signup", timestamp=now + 2000)
        db.track("page_view", user_id=456, page="/home", timestamp=now + 3000)
        db.track("click", user_id=456, button="login", timestamp=now + 4000)


        print("\nFiltering events...")
        # Get all page views
        page_views = db.filter(event_type="page_view")
        print(f"Total page views: {len(page_views)}")

        # Get all events for user 123
        user_events = db.filter(user_id=123)
        print(f"Events for user 123: {len(user_events)}")

        # Get page views for user 123
        user_page_views = db.filter(event_type="page_view", user_id=123)
        print(f"Page views for user 123: {len(user_page_views)}")

        print("\nAggregations...")
        # Count events by type
        type_counts = db.count_by("event_type")
        print(f"Event counts by type: {type_counts}")

        # Count unique users
        unique_users = db.unique("user_id", event_type="page_view")
        print(f"Unique users with page views: {unique_users}")



if __name__ == "__main__":
    main()
