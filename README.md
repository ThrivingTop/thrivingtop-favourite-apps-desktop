# Favourite Apps Desktop Launcher

A Windhawk tool mod that provides a compact desktop panel for favourite apps. Press **Ctrl+Alt+Space** to show or hide it. Click an app to launch it; the panel can sort shortcuts by how often they are launched. Hover over the compact header to reveal apps, or click **PIN** to keep the list open. Drag the header to move the panel.

Choose a list or two-column grid, adjust opacity, and configure whether the panel stays above other windows in Settings. Windows 11 uses an acrylic backdrop when available. Right-click the panel for layout, pinning, topmost, hide, and Windhawk actions.

## Add apps

Open the mod's **Settings** and edit **Apps**. Add one app per line in this format:

```text
Display name|path or command
```

Use the **+** button to search Start Menu apps and shortcuts, or drag an `.exe`, `.lnk`, or folder from the desktop or File Explorer onto the panel. Added items are saved locally. Drag an app onto another app to change its position. Disable **Sort by most used** to keep your manual order.

Example:

```text
Browser|C:\Program Files\Google\Chrome\Application\chrome.exe
Notepad|notepad.exe
Downloads|shell:Downloads
```

Paths can include environment variables such as `%USERPROFILE%`. The panel starts with common Windows shortcuts, which can be replaced in Settings. Launch counts are stored locally in your user profile. This mod does not monitor app activity outside of launches made through the panel.

## Install

Install Windhawk, then install this mod from its source or paste the contents of [`thrivingtop-favourite-apps-desktop.wh.cpp`](thrivingtop-favourite-apps-desktop.wh.cpp) into Windhawk's mod editor. Configure the app list and appearance in the mod's Settings.

This is a Windhawk **tool mod** and runs separately from Explorer. The context-menu option **Deactivate this mod...** opens Windhawk; switch the mod off from Installed Mods because a mod cannot deactivate itself directly.

## Hotkey

- **Ctrl+Alt+Space** — show or hide the panel

## License

No license has been specified yet. All rights reserved unless the author adds a license.
