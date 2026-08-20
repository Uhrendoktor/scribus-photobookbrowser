# PhotoBook Browser for Scribus 1.6.5

Native C++/Qt persistent plugin prototype for Scribus 1.6.5, aimed at
CEWE-fotobook-style workflows: browse a folder of photos, see at a glance
which ones are already placed, and drag/insert them into the document.

It is intentionally built **inside the Scribus source tree**, because the
plugin uses internal Scribus classes. Scribus documents the native plugin
mechanism and the need for the exact host API/ABI. The 1.6.5 source also
contains the existing `picbrowser` plugin, which is the reference for
integration.

## Current feature set

Window / lifecycle:
- Dockable panel embedded in the main window (not a stray top-level window)
- Listed under **Windows** in the main menu, via `QDockWidget::toggleViewAction()`
- Hidden on program start; opens only when picked from the Windows menu
- Panel position/visibility persists with the rest of the Scribus workspace layout

Browsing:
- Recursive image-folder scan, run off the GUI thread (`QtConcurrent`)
- Last-used folder remembered across sessions (`QSettings`)
- Recursive folder watching (`QFileSystemWatcher`) - adding/removing/renaming
  photos anywhere under the folder triggers an automatic rescan
- Async, incrementally-streamed thumbnail generation with EXIF auto-rotation,
  so the grid fills in live instead of blocking on decode
- Search by filename/path, working **Used / Unused** status filter, and
  sort by name or by file date (newest/oldest first)
- Tooltips showing full path and, for placed photos, which page(s)

Used/unused indication:
- Custom item delegate (not just a stock icon grid): placed photos get a
  green checkmark/count badge and are dimmed; not-yet-used photos stay at
  full brightness so they draw the eye
- Status bar summary (`N photos | N used | N unused`)

Live document sync:
- Usage recomputed whenever the tracked document changes (plugin's own
  `setDoc`/`changedDoc` hooks, plus a direct connection to the document's
  change notification, debounced ~200ms so rapid edits don't cause a
  recompute storm)

Placing images:
- Double-click / context menu to add to the current page
- Context menu to replace the selected image frame
- **Drag a thumbnail straight onto the page/canvas** (exports `text/uri-list`
  so it behaves like an OS file-manager drag, which Scribus's canvas already
  accepts and turns into a picture frame)
- **Fill empty frames**: one click fills every empty image frame on the
  document with unused photos - the classic "flow the shoot into this
  template" fotobook action
- "Reveal in file manager" for the source file

The code intentionally keeps Scribus-specific operations in a small adapter
class (`PhotoBookBrowserPalette::insertImage`) so API adjustments from local
compiler feedback are easy.

## Things to double check against your local 1.6.5 checkout

**"Windows" menu registration** (`photobookbrowserplugin.cpp` /
`addToMainWindowMenu`) now uses the real `ScMWMenuManager` API rather than
guessing:

```cpp
#include "ui/scmwmenumanager.h"   // scribus.h only forward-declares the type
...
mw->scrMenuMgr->addMenuItem(m_toggleAction, "Windows");
```

This is the same call Scribus's own `pluginmanager.cpp` uses to wire up
*every* `ScActionPlugin` menu entry in the application
(`mw->scrMenuMgr->addMenuItem(mw->scrActions[ai.name], ai.menu)`), and
`"Windows"` is confirmed to already be a live, registered menu key at this
point in startup - `scriptercore.cpp` (which the persistent `ScriptPlugin`
delegates its own `addToMainWindowMenu` to) inserts its own top-level menu
with `menuMgr->addMenuStringToMenuBarBefore("Scripter", "Windows")`, which
only works if `"Windows"` already exists. `ScPersistentPlugin` has no
`ActionInfo`-based auto-registration the way `ScActionPlugin` does (that's
what `picbrowser` uses), so `addToMainWindowMenu()` has to make this call
itself - there's no separate `addMenuItemString` registration step needed
for a single already-constructed action.

If it still doesn't appear after building:
- Confirm `addToMainWindowMenu()` is actually being called (a temporary
  `qWarning("addToMainWindowMenu called");` at the top will show it in the
  console) - if it never fires, the issue is plugin registration/lifecycle,
  not this call.
- If your local `ScMWMenuManager::addMenuItem` overload set differs (e.g.
  requires `ScrAction*` specifically rather than accepting the base
  `QAction*`), wrap `m_toggleAction` accordingly; everything else in the
  plugin is unaffected either way.

**Live-refresh signal** (`photobookbrowserpalette.cpp` / `setDocument`) -
connects to `ScribusDoc::changed()` via the old-style `SIGNAL()/SLOT()`
macros. If that signal is named differently in your tree, update the two
`connect`/`disconnect` calls here; nothing else depends on it.

Everything else (`itemAdd` adapter in `insertImage`, image-frame usage
scan in the model) uses only well-established Scribus core APIs already
present in, or a direct extension of, the original prototype.

## Integration

Copy this directory to:

    scribus/plugins/photobookbrowser

Then add it next to `picbrowser` in the parent plugin CMake file. The
helper script in the repository can do this automatically.

Do not build this as an independent application. `scplugin.h`,
`scribusdoc.h`, `pageitem.h`, etc. are internal Scribus headers.

## License

GPL-2.0-or-later.
