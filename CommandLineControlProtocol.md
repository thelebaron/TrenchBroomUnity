# Command Line Control Protocol

TrenchBroom can accept live requests from a local command-line client while the editor is open. Start
the GUI with an explicit local socket name:

```text
TrenchBroom.exe --command-line-control-socket trenchbroom-command-line
```

Connect to that local socket using a `QLocalSocket` compatible client. The server is
available only when the option is provided. The editor remains interactive while requests
are sent.

The build also creates `TrenchBroomCommandLineClient.exe`. It reads JSON requests from standard
input and writes responses to standard output:

```powershell
Get-Content .\requests.jsonl | .\TrenchBroomCommandLineClient.exe --socket trenchbroom-command-line
```

## Framing and responses

Send one JSON object per line. TrenchBroom returns one JSON response per request line,
with the same `id` value. A connection can carry multiple requests. Each request is
limited to 1 MiB.

Successful responses have this shape:

```json
{"id":1,"ok":true,"result":{}}
```

Failed requests return `ok: false` and an `error` message. Every request must include an
`id` string or number and a `command` string. Requests are handled on the GUI thread and
apply to the active document.

## Commands

### Read active map state

```json
{"id":1,"command":"status"}
```

Returns `has_map`, `path`, `game`, `map_format`, and `modified`.

### Create a Quake 1 map

```json
{"id":2,"command":"new_map","map_format":"standard"}
```

`map_format` is optional and defaults to `standard`; the supported values are `standard`
and `valve`. Replacing a modified map fails unless `discard_changes` is `true`.

### Load a Quake 1 map

```json
{"id":3,"command":"load_map","path":"C:/maps/room.map","map_format":"auto"}
```

`map_format` is optional and defaults to `auto`; supported values are `auto`, `standard`,
and `valve`. Replacing a modified map fails unless `discard_changes` is `true`.

### Save the active map

```json
{"id":4,"command":"save_map"}
```

### Save to another path

```json
{"id":5,"command":"save_map_as","path":"C:/maps/room-copy.map"}
```

An existing destination is preserved unless `overwrite` is explicitly `true`. The `.map`
extension is added when the destination has no extension.

### Move the 3D camera

```json
{"id":6,"command":"camera_move","translation":[64,0,16],"yaw_degrees":15,"pitch_degrees":-5}
```

`translation` is an optional world-space offset in map units. `yaw_degrees` and
`pitch_degrees` are optional relative rotations in degrees. At least one of these fields
must be present. The response includes the new camera `position` and `direction`.

### Frame all map geometry

```json
{"id":7,"command":"frame_geometry"}
```

Moves the 3D camera to frame the active map and resets its direction to TrenchBroom's
default 3D viewing angle. This is immediate, so a following screenshot captures the new
view without waiting for a camera animation.

### Capture the 3D viewport

```json
{"id":8,"command":"capture_viewport","path":"C:/maps/room.png"}
```

Captures the 3D viewport framebuffer only, without the editor panels or window chrome.
The 3D viewport must be visible. The image is PNG; `.png` is added when the path has no
extension, and an existing file is preserved unless `overwrite` is `true`. The response
includes the saved path and pixel dimensions.

### Create convex brushes

```json
{"id":9,"command":"create_brushes","brushes":[{"vertices":[[0,0,0],[128,0,0],[128,128,0],[0,128,0],[0,0,64],[128,0,64],[128,128,64],[0,128,64]]}]}
```

Each brush's `vertices` are finite `[x, y, z]` points in Quake map units. TrenchBroom
builds a convex hull from those points. The result contains each new brush's ID and
bounds. New brushes are selected and the operation is undoable.
