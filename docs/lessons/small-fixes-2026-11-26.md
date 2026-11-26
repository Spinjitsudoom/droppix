# L-2026-11-26-hardware-qa-small-fixes: smaller findings from the Nexus 10 QA pass

- **ID:** `L-2026-11-26-hardware-qa-small-fixes`
- **Tags:** `android`, `client`, `web`, `kwin`, `input`, `wrong-answer`, `low`
- **Date:** 2026-11-26

- **Touch ignored the letterbox.** The picture keeps its shape (bars when the view's aspect
  differs: nav bar visible, a phone), but touch was spread over the whole view, up to a bar's width
  off at the edges. `AspectFit.toStream` maps through the drawn rect (and the horizontal flip).
- **Web / desktop clients always asked for landscape.** The host builds the screen in the shape the
  HELLO orientation says. The web client sent 0 even upright on a phone; the desktop client's "0°"
  ignored a portrait monitor. Both now send their shape (`orientationForSize`, `hello_orientation`).
- **The PC's Orientation setting did nothing.** Every client's HELLO overrides it; removed (it was
  the source of a stray `--orientation 270`).
- **`kscreen-doctor output.X.replicationSource.0`** never existed as a command (it is a JSON key)
  and printed "Unable to parse arguments" every session. The reset is `output.X.mirror.none`,
  only where `--help` lists it.
- **`poll_control` left already-buffered messages** until the next bytes arrived (read_hello can
  read past the HELLO).
