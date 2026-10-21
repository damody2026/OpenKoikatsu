# Native Studio shader input probe

This Rust D3D11 plugin copies bound vertex/pixel constant buffers to staging buffers at a Unity rendering event. It does not modify source cards, scene settings, shaders or GPU resources owned by the game. Captures contain raw buffer bytes and their float4 interpretation, including padding and unused values; use the original DXBC reflection to identify fields.

```text
cargo build --release --manifest-path Tools/shader_probe/Cargo.toml
Tools/lua/lua.exe Tools/GamePoseProbe/build.lua
Tools/lua/lua.exe Tools/GamePoseProbe/run_scene_visual_audit.lua --tag NativeHairInputs --scenes <scene.png> --gpu-probe Tools/shader_probe/target/release/ok_shader_probe.dll --mesh cf_hair_b_17_00
```

The Lua runner temporarily installs the managed probe, starts its own Studio process with D3D11, verifies capture completion, and removes the managed probe afterwards. It refuses an existing Studio process or an already-installed probe. Native output is under `Saved/Tests/<tag>/Scene1.*`; use a fresh tag. The managed probe draws the selected renderer's ForwardBase pass into a temporary render target inside the original camera's rendering context, then calls the Rust event before restoring the camera target.

`ok_shader_probe_configure` borrows the native D3D11 texture pointer supplied by Unity. The host must retain that texture until `<output>.gpu.json` is written. `ok_shader_probe_render_event` returns the native render-thread callback for `CommandBuffer.IssuePluginEvent`. The capture must contain pixel constant buffer 0 to pass.
