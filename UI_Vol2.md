# UI Vol2: Current vs Proposed

## Current UI (observed/inferred)
- Dark theme, card-based main screen with large players/max, trend arrows, map/time, restart timer, status, and last update.
- Top bar: navigation to Settings/History, server index indicator, SD status, WiFi icon.
- Secondary servers: up to 3 compact cards with players/max, trend, day/night, time; tap to switch.
- History screen: chart with range buttons (1h, 8h, 24h, 1w), dynamic Y-scaling, axis labels.
- Settings: WiFi credentials, server add/delete (BM ID, name, map), refresh interval, screensaver timeout, alerts threshold, restart schedule.
- Screensaver/backlight control with touch wake and long-press off; visual+buzzer alerts; SD JSON history and NVS backup.

## Proposed UI Enhancements (Vol2)
- Glanceable summary chips: show ?Peak 24h? and ?Avg 1h? on the main card; stale badge when data older than refresh interval (?Stale ? 5m ago?).
- Health badges: small pill in top bar for API latency and WiFi RSSI with color coding; clear SD/USB state (USB mode, SD missing warning pill).
- Secondary server UX: allow reorder (tap-hold or up/down); optional auto-sort by player count; keep cards compact but legible.
- Quick actions: bottom tray with icon buttons (refresh, alert snooze, dim) alongside large Prev/Next buttons.
- Color/legibility: reserve green/amber/red for status/alerts; neutral chrome; high-contrast toggle; larger touch targets; optional font size bump.
- History usability: tap tooltips showing exact time/value; smoothing toggle; per-range min/avg/max chips under chart.
- Restart clarity: label source (?Manual? vs ?Detected?), show timezone, and a slim urgency bar/ring on the main card.
- Alerts: per-server quiet hours/snooze; repeat optional; keep banners slide-in/out with gentle motion.
- Motion & responsiveness: light easing on bars/banners; avoid heavy animations; keep UI thread non-blocking.
- Future hooks: room for OTA/web/push without UI rewrite; keep navigation predictable (top bar + bottom nav) and information hierarchy (players/max first, trend/restart second, metadata last).
