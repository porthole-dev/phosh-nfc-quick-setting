> **Unofficial.** Not affiliated with or endorsed by postmarketOS, Phosh or
> GNOME. Do not report problems with this plugin to them; open an issue here.
>
> **Experimental.** No warranty, see [COPYING](COPYING).
>
> **AI-assisted.** See [AI.md](AI.md).

# NFC quick setting

A phosh quick setting that turns the NFC radio on and off, so reaching NFC
costs one swipe instead of a trip through Settings.

Not a phosh fork. phosh publishes a `phosh-plugins` pkg-config and a public
`phosh-plugin.h` exactly so plugins can live out of tree, and its own
caffeine, dark-mode and location tiles are the pattern this follows: a
`PhoshQuickSetting` subclass with a `.ui` template, registered into the
`phosh-quick-setting-widget` extension point from a `GIOModule`.

## Build and install

Needs phosh's development files (the `phosh-plugins` and `libphosh-0.45`
pkg-config modules), GTK 3 and GLib.

```sh
meson setup _build
meson compile -C _build
meson install -C _build
```

The plugin installs into the directory phosh loads quick settings from, which
`phosh-plugins` names. On postmarketOS, the package is
`temp/phosh-nfc-quick-setting` in
[porthole-dev/pmaports](https://github.com/porthole-dev/pmaports), built from
this repository's release tarballs.

Then list it alongside the tiles already enabled:

```sh
gsettings set sm.puri.phosh.plugins quick-settings \
  "['wifi-hotspot-quick-setting', 'mobile-data-quick-setting', 'nfc-quick-setting']"
```

phosh scans its plugin directory once, at startup, so a newly installed tile
appears after phosh restarts, not when the gsetting changes. Changing the
setting alone logs `Custom quick setting 'nfc-quick-setting' not found`.

## Two things it has to know about neard

**No helper is needed to switch the radio.** neard's bus policy carries
`<policy at_console="true"><allow send_destination="org.neard"/>`, so the
logged-in user may write `org.neard.Adapter` `Powered` directly. Verified from
the session with plain `busctl`.

**neard refuses to power down while a poll loop is running.** An app left
scanning holds the radio up, and the property write comes back
`org.neard.Error.Failed`, which from the outside looks like a broken tile. So
turning off stops the poll loop first and then powers the adapter down. Turning
on is a single write.

## Why the tile does not wait for the radio

Powering an NFC controller is not instant, and a tile that only moves once
neard confirms reads as a tile that ignores you: press it twice quickly and the
second press sees the old state and asks for the same thing again, queueing
work behind work.

So the tile follows the request, not the radio. A press flips what is on screen
immediately and asks neard for that; a press while a request is in flight only
moves the target, and whichever request is running reconciles when it lands.
Only one request exists at a time however hard the tile is pressed, and the
radio's own PropertiesChanged has the last word once nothing is pending.

Measured on device: the tile responds in 0.6ms, the radio catches up in 64ms,
and five presses as fast as they can be delivered settle correctly in 262ms.

## What it shows

The tile hides itself where there is no adapter, which is nearly every
machine, the same way the Settings page hides its row. Its status page links
to Settings' own NFC page through phosh's `panel.launch-panel` action, so the
permission list is one tap from the radio switch.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

GPL-3.0-or-later, see [COPYING](COPYING).
