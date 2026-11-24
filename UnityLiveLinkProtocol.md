# Unity Live Link Protocol

The live link keeps TrenchBroom's 3D camera and the Unity editor's scene view camera in
sync, and lets TrenchBroom start Unity's play mode from a chosen spot (Play From Here).
TrenchBroom listens on a local socket and the Unity editor connects to it. On Windows
the socket is the named pipe `\\.\pipe\<socket name>`.

- TrenchBroom: the socket name is **Socket name** under **Live Link** in Unity
  preferences (`Unity/Live link socket name`, default `trenchbroom-unity`). Empty turns
  the live link off. Changing it takes effect right away.
- Unity: the same name is set in **Preferences > TrenchBroom**
  (`Packages/com.junk.trenchbroom/Junk.Trenchbroom.Editor/LiveLink`). Unity reconnects
  whenever TrenchBroom restarts.

One Unity editor is connected at a time; a new connection replaces the previous one.

## Framing

Each message is one compact JSON object followed by `\n`, in either direction. Messages
have a `type` and no request IDs: any message can be sent at any time, and none expects a
reply except `request_camera`. The current protocol version is 2; version 2 added
`play`, `stop` and `play_state`. Messages over 64 KiB make TrenchBroom disconnect.

## Coordinates

Cameras are in Quake map units with Z up. `direction` and `up` are unit vectors (the
receiver normalizes them). Converting to Unity is Unity's job, using the same axis swap
(`x, z, y`) and `scalingFactor` as the Scopa importer for that map.

## Messages

### `hello`

Sent by each side when the connection opens.

```json
{"type":"hello","version":2}
{"type":"hello","version":2,"map":"C:/.../Assets/Maps/e1m1.map","live_camera":false}
```

TrenchBroom's hello includes the active map path (empty for an unsaved map) and whether
the live camera is on. Unity takes TrenchBroom's live camera state. A version mismatch is
logged as a warning.

### `camera`

```json
{"type":"camera","map":"C:/.../e1m1.map","position":[256,128,64],"direction":[1,0,0],"up":[0,0,1]}
```

Moves the receiver's camera. TrenchBroom includes the active map path so Unity can find
the map's import scale. Unity's camera messages may carry a `map` field; TrenchBroom
ignores it.

### `request_camera`

```json
{"type":"request_camera"}
```

Asks the other side to reply with a `camera` message.

### `live_camera`

```json
{"type":"live_camera","enabled":true}
```

Turns the live camera on or off on both sides. The receiver doesn't send it back.

### `play` (TrenchBroom to Unity)

```json
{"type":"play","map":"C:/.../e1m1.map","anchor":"eye","position":[256,128,64],"direction":[1,0,0],"up":[0,0,1]}
```

Starts playing with the player at the given pose. With `anchor` `eye`, the position is
where the player's eyes go (Play in Unity, from the 3D camera); with `feet`, it is where
the player stands (Play From Here on a clicked surface), and the direction is level.
TrenchBroom saves the map first if it has changes. Unity refreshes its assets, comes to
the front, enters play mode, starts a game past the menu if it is at the splash screen or
the menu, and moves the player there once the level has streamed in. If Unity is already
playing, it only moves the player.

### `stop` (TrenchBroom to Unity)

```json
{"type":"stop"}
```

Leaves play mode.

### `play_state` (Unity to TrenchBroom)

```json
{"type":"play_state","playing":true}
```

Sent when Unity enters or leaves play mode, and on connecting while playing. Just
before `play_state` with `playing` false, Unity sends a `camera` message with the
player's eyes, so TrenchBroom's camera continues where the player stopped.

## Live camera

While the live camera is on, the side whose application is active (has focus) sends its
camera about 30 times a second, but only when the camera has moved since the last
camera sent or received. The inactive side only follows. Each side remembers the last
camera it received, so a received camera is never echoed back.
