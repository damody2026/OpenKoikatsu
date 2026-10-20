# Crowd room: bright atrium

The saved `/Game/Scenes/CrowdRoom/CrowdRoom` map now contains a 50 x 50 m,
12 m high atrium instead of the gray test plane and black walls.

- Limestone tile floor with repeating stone grain, walnut perimeter trim.
- Cream wall piers, walnut window frames, brass mullions, teal window seats.
- Stylized garden/mountain window views, timber ceiling coffers, luminous
  skylight, suspended light fixtures and corner planting.
- 369 decorative instances in 11 HISM mesh/material groups. This is a group
  count, not a measured draw-call count; passes and visibility can split draws.
- Three streamed 512-square environment textures; nine shared materials.
- One movable directional light; decorative surfaces have a stable ambient
  color through emissive shading, without extra shadowed point/rect lights.
  The lit floor receives character shadows. Character color/exposure settings
  are unchanged.
- No per-frame decoration logic. Original NPC routes, player room clamp,
  animation 60 Hz, GameLowHairSafeR9 and NearDetailR6 remain in use.

The generator is editor-only in `OKCrowdRoomEnvironment.inl`. It modifies only
environment actors and keeps a fresh-tagged copy of the previous map in
`Saved/Crowd/RoomBackups`. Repeated generation replaces earlier geometry.

After building the authoring Editor with `-NoHotReload`, regenerate using:

```powershell
Tools/build_crowd_room_environment.ps1 -Tag FreshRoomTag
Tools/cook_crowd_development.ps1 -Tag FreshRoomTag -Cards (0..99) -CookWorkers 8
Tools/stage_crowd_development.ps1 -Tag FreshRoomTag -StageRoot D:/OpenKoikatsuCrowd/DevelopmentStage
Tools\lua\lua.exe Tools\run_crowd_optimized.lua --mode Play --tag FreshRoomPlay --max-fps 120
```

## Verified evidence, 2026-10-04

- `AtriumEditorBuildR5.log`: successful Editor build.
- `AtriumAuthorR3.log`: replaced 31 accumulated/legacy actors; saved exactly
  11 decoration groups, 369 instances and one directional light.
- `Cook-AtriumR3-A/B.log`, `Stage-AtriumR3.log`: successful Cook and staging.
- `DevelopmentAtriumMovement100R3.json`: loaded 100, failed 0; low-only initial
  load; process-to-ready 7.231916 s; movement and face round trip passed.
- `DevelopmentAtriumMovement100R3.png`: inspected the actual packaged
  Development third-person view. Floor, windows and materials display without
  the coplanar old floor or material fallback warnings.
- Functional measurement: mean 10.543461 ms, P95 18.648501 ms; 120 FPS target
  not met. This run validates room functionality and appearance, not an isolated
  performance comparison or a new 120 FPS acceptance result.

Failures and their general fixes are recorded in `CROWD_GPU_POSE_ERRORS.md`:
explicit instanced material usage, inactive-map actor iteration and correct
TObjectPtr iteration types. Cook success alone does not validate the image.

## General generation and current-mesh near shadows

The ordinary map-creation path now calls the same atrium generator used by
EnvironmentOnly. Existing incomplete/legacy rooms are upgraded with a backup;
complete atriums are reused. Rebuilding the actual room with
`GenericAtriumRepeatR4` removed exactly 12 environment actors and recreated
11 HISM groups / 369 instances / one light, without accumulating geometry.
`AtriumEnvironmentTestsR3` passed RoomEnvironment, Routes and SavedMap after
this rebuild. The room test checks serialized instances, finite transforms,
instanced material permutations, duplicate geometry and non-ticking decoration.

Near-detail characters cast shadows while their face/hair detail is displayed,
including the 1600–1800 cm retention band. The low body/clothing, visible high
HeadMesh and visible middle-detail hair cast their current pose. Hidden replaced
low face/hair components no longer cast a second hidden shadow. The distant
population keeps the existing radius/focus policy. Both cached and uncached
shadow policies follow this rule, and async detail completion refreshes it.
This does not load complete high-poly characters for shadows or change the
60 Hz animation rate.

Development verification:

- `DevelopmentAtriumNearShadow100R1.json`: loaded 100 / failed 0;
  movement and face round trip passed; 17 near-detail residents and 17 shadow
  residents; zero missing near-shadow components; process load 6.735 s.
- `DevelopmentAtriumNearShadowCache100R1.json`: cached policy with 100 cm
  hysteresis and focus-only fallback; face round trip passed; 36 near-detail
  residents and 36 shadow residents; zero missing near-shadow components.
- Actual third-person shadow screenshot inspected in
  `DevelopmentAtriumNearShadow100R1.png`.
- Functional runs were 8.477 ms mean for the moving camera and 21.151 ms for
  the stationary cached case. They differ in visible/near population and are
  not a controlled cached-versus-uncached performance comparison. 120 FPS is
  still not accepted.
- `GenericAtriumFinalR4` Cook/stage publishes the regenerated map with the
  Development near-shadow binary. The runner rejects a final report with
  near faces active but missing current-mesh shadows.
