# Third-party notices

Shudder is an independent application and is not affiliated with Twitch, Streamlink, mpv, Qt, or libsecret.

The GNU General Public License v3.0 or later in the repository root applies to Shudder-authored source code. Third-party software and assets retain their original licenses and are not relicensed by this project.

## Qt

Shudder uses Qt 6 modules including Qt Core, Qt Gui, Qt Quick, Qt Quick Controls, Qt Network, Qt WebSockets, Qt WebEngine, and Qt Test. Qt is available under GPL and LGPL licensing options. Distribution maintainers must follow the license terms applicable to their chosen Qt build.

Authoritative Qt licensing information:

<https://www.qt.io/licensing/open-source-lgpl-obligations>

## Streamlink

Project: <https://github.com/streamlink/streamlink>

License: BSD 2-Clause

Shudder invokes Streamlink as a separate process for Twitch stream URL and quality resolution. Release formats that bundle Streamlink must pin the exact version, checksum, license text, and corresponding source information.

## mpv and libmpv

Project: <https://github.com/mpv-player/mpv>

Shudder links to libmpv and renders through mpv's render API. mpv can be built under GPLv2-or-later or, with relevant GPL components disabled, LGPLv2.1-or-later. Linked libraries such as FFmpeg affect the resulting binary's license.

Authoritative licensing details:

<https://github.com/mpv-player/mpv/blob/master/Copyright>

## libsecret

Project: <https://gitlab.gnome.org/GNOME/libsecret>

Shudder uses libsecret/freedesktop Secret Service for credentials where available. Shudder does not silently fall back to plaintext credential storage.

## Twitch Marks and CDN Assets

Shudder may show Twitch names, avatars, badges, emotes, channel thumbnails, and related marks only to identify Twitch content and accounts. Their use does not imply affiliation with or endorsement by Twitch. Twitch names and marks remain the property of Twitch and their respective owners.

## Third-party Emote Providers

Shudder can show emote names and images from 7TV, FrankerFaceZ, and BetterTTV through their public APIs and CDNs. Those services, emote artwork, names, and marks remain owned by their respective providers and creators.

- 7TV: <https://7tv.app/>
- FrankerFaceZ: <https://www.frankerfacez.com/>
- BetterTTV: <https://betterttv.com/>
