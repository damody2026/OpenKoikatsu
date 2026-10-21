//! Read-only D3D11 constant-buffer capture at a Unity rendering event.
//! The host owns the target texture and keeps it alive until completion.
use std::{ffi::{c_char, c_void, CStr}, mem::ManuallyDrop, path::PathBuf, sync::Mutex};
use windows::{core::Interface, Win32::Graphics::Direct3D11::*};

struct Request { texture: usize, output: PathBuf }
static REQUEST: Mutex<Option<Request>> = Mutex::new(None);
fn suffixed(path: &std::path::Path, suffix: &str) -> PathBuf {
    let mut result = path.as_os_str().to_os_string();
    result.push(suffix);
    result.into()
}

#[no_mangle]
pub unsafe extern "C" fn ok_shader_probe_configure(texture: *mut c_void, output: *const c_char) -> i32 {
    if texture.is_null() || output.is_null() { return -1; }
    let Ok(output) = CStr::from_ptr(output).to_str() else { return -2; };
    let Ok(mut request) = REQUEST.lock() else { return -3; };
    *request = Some(Request { texture: texture as usize, output: output.into() });
    0
}

unsafe fn capture(request: &Request) -> Result<serde_json::Value, String> {
    // Borrow Unity's COM reference; never release or modify the target resource.
    let texture = ManuallyDrop::new(ID3D11Texture2D::from_raw(request.texture as *mut c_void));
    let device = texture.GetDevice().map_err(|e| e.to_string())?;
    let context = device.GetImmediateContext().map_err(|e| e.to_string())?;
    let mut rows = Vec::new();
    for stage in ["ps", "vs"] {
        let mut buffers: [Option<ID3D11Buffer>; 14] = Default::default();
        if stage == "ps" { context.PSGetConstantBuffers(0, Some(&mut buffers)); }
        else { context.VSGetConstantBuffers(0, Some(&mut buffers)); }
        for (slot, buffer) in buffers.into_iter().enumerate() {
            let Some(buffer) = buffer else { continue; };
            let mut desc = D3D11_BUFFER_DESC::default();
            buffer.GetDesc(&mut desc);
            if desc.ByteWidth == 0 || desc.ByteWidth > 65536 { continue; }
            let staging_desc = D3D11_BUFFER_DESC { ByteWidth: desc.ByteWidth,
                Usage: D3D11_USAGE_STAGING, CPUAccessFlags: D3D11_CPU_ACCESS_READ.0 as u32,
                ..Default::default() };
            let mut staging = None;
            device.CreateBuffer(&staging_desc, None, Some(&mut staging)).map_err(|e| e.to_string())?;
            let staging = staging.ok_or("staging buffer absent")?;
            context.CopyResource(&staging, &buffer);
            let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
            context.Map(&staging, 0, D3D11_MAP_READ, 0, Some(&mut mapped)).map_err(|e| e.to_string())?;
            let bytes = std::slice::from_raw_parts(mapped.pData as *const u8, desc.ByteWidth as usize).to_vec();
            context.Unmap(&staging, 0);
            let file = suffixed(&request.output, &format!(".{stage}-cb{slot}.bin"));
            std::fs::write(&file, &bytes).map_err(|e| e.to_string())?;
            let values: Vec<_> = bytes.chunks_exact(4).map(|b| {
                let f = f32::from_le_bytes(b.try_into().unwrap());
                if f.is_finite() { serde_json::json!(f) } else { serde_json::Value::Null }
            }).collect();
            rows.push(serde_json::json!({ "stage":stage, "slot":slot, "bytes":desc.ByteWidth,
                "file":file, "float4":values.chunks(4).collect::<Vec<_>>() }));
        }
    }
    if !rows.iter().any(|row| row["stage"] == "ps" && row["slot"] == 0) {
        return Err("Unity rendering event has no pixel constant buffer at slot zero".into());
    }
    Ok(serde_json::json!({"passed":true,"buffers":rows}))
}

unsafe extern "system" fn render_event(_: i32) {
    let Some(request) = REQUEST.lock().ok().and_then(|mut r| r.take()) else { return; };
    let report = match capture(&request) {
        Ok(report) => report,
        Err(error) => serde_json::json!({"passed":false,"error":error}),
    };
    let _ = std::fs::write(suffixed(&request.output, ".gpu.json"), report.to_string());
}

#[no_mangle]
pub extern "C" fn ok_shader_probe_render_event() -> *const c_void {
    render_event as *const c_void
}
