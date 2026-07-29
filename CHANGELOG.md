# Changelog

All notable changes to Shudder will be documented in this file.

## [0.1.4] - 2026-07-29

### Fixed

- Restored global and channel 7TV emotes when Twitch credentials are unavailable, and kept channel identity updates and delayed responses from crossing streams.
- Added the Qt WebP image-format dependency to native packages and verified it in AppImage and CI builds.
- Stabilized native libmpv playback during continuous resizing, maximize and fullscreen transitions, source replacement, render-context recreation, and active teardown.

### Changed

- Replaced fixed native-player redraw polling with coalesced mpv update callbacks and asynchronous source commands.
- Expanded 7TV parsing, caching, image lifecycle, and native and Standard playback lifecycle regression coverage.

## [0.1.3] - 2026-07-25

### Fixed

- Restored AppImage startup on systems where linuxdeploy corrupts the bundled LeanCrypto dynamic hash table.
- Added bundled-library hash validation and an extracted AppRun startup check under headless Weston.

## [0.1.2] - 2026-07-25

### Fixed

- Isolated host Streamlink and Python processes from AppImage libraries, preventing bundled OpenSSL from overriding host-compatible libraries.
- Replaced raw Streamlink tracebacks in playback status with concise authentication, unsupported-stream, missing-tool, and runtime-library errors while retaining redacted diagnostics.

## [0.1.1] - 2026-07-25

### Fixed

- Made live chat following deterministic across new messages, model resets, resizing, and manual history reading.
- Stabilized badge resolution across channels and prevented stale image requests from replacing current artwork.
- Unified local and server-confirmed chat username colors without transient white fallbacks.
- Prevented cancelled, replaced, timed-out, or crashed Streamlink processes from retrying or updating stale playback state.
- Recovered directory browsing cleanly after Twitch credential changes and isolated credential-scoped caches.
- Rejected stale Device Code callbacks and preserved valid tokens after malformed refresh responses.
- Corrected fragmented loopback player requests, moderation role updates, and IRC tag decoding.

### Changed

- Expanded chat, playback, authentication, networking, image lifecycle, and packaging regression coverage.
- Improved keyboard shortcut scoping, settings accessibility, installation metadata, and release validation.

## [0.1.0] - 2026-07-22

### Added

- Initial public release.
- Native Linux interface built with Qt 6 and QML.
- Wayland-first desktop support.
- Native playback through Streamlink and libmpv.
- Standard Twitch playback through Qt WebEngine.
- Twitch authentication, directory browsing, search, and followed channels.
- Native Twitch chat with badges, emotes, replies, mentions, and moderation events.
- Linux desktop integration and release packaging.
