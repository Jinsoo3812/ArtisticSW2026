# Local room test commands

**Retired on 2026-10-28:** `SW.DevTest.Local.Host` and `SW.DevTest.Local.Join` registrations are commented out. Use the existing room UI to create/continue/join a room and `sw.Room.ServerBackend` to select the editor or packaged server. The examples below are historical and are not active console commands.

Development/editor builds only. Rebuild ArtisticSW2026Editor before use.
Run two standalone game windows with the existing SWRoomEditorExe and SWRoomProject path overrides. No SWRoomLocalTest flag is required for these console commands.

In the first game window, open the Unreal console (~) and enter:

```text
SW.DevTest.Local.Host Host
```

This creates a NEW room using the existing save flow, starts its local server on UDP 7777, waits for readiness, then automatically connects the host to 127.0.0.1 with its host key. It skips public IP lookup and room-code generation. A new room follows the normal new-room save semantics; this is not an isolated temporary save.

After the host enters the game, run in the second window:

```text
SW.DevTest.Local.Join Guest
```

For another PC on the same LAN, use the server PC's actual private IPv4:

```text
SW.DevTest.Local.Join Guest 192.168.55.200
```

The server retains its existing all-interface UDP listener; it is not restricted to loopback. The host connects through loopback, while LAN clients use the server PC's private address. Windows firewall must permit LAN access. Public IPv4 addresses are rejected by Local.Join.

Names must differ. Only one hosted room on UDP 7777 can run on the PC at a time. Commands require an idle/failed game client and use the existing connection lifecycle. No room-menu interaction or room code is needed. Open the game console, not the separate log window.

Local commands are unavailable in Shipping/Test builds. They do not enable gameplay cheats automatically. The existing UI flow is unchanged when the SWRoomLocalTest launch flag is absent.
