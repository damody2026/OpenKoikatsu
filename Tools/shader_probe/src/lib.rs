//! Read-only D3D11 constant-buffer capture at a Unity rendering event.
//! The host owns the target texture and keeps it alive until completion.
use std::{ffi::{c_char, c_void, CStr}, mem::ManuallyDrop, path::PathBuf, sync::Mutex};
use windows::{core::Interface, Win32::Graphics::Direct3D11::*};

struct Request { texture: usize, output: PathBuf }
static REQUEST: Mutex<Option<Request>> = Mutex::new(None);
static TEXTURES: Mutex<Vec<Request>> = Mutex::new(Vec::new());
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
    if let Ok(mut textures)=TEXTURES.lock(){textures.clear();}
    0
}

#[no_mangle]
pub unsafe extern "C" fn ok_shader_probe_add_texture(texture:*mut c_void,output:*const c_char)->i32{
    if texture.is_null()||output.is_null(){return -1;}
    let Ok(output)=CStr::from_ptr(output).to_str() else{return -2;};
    let Ok(mut textures)=TEXTURES.lock() else{return -3;};
    textures.push(Request{texture:texture as usize,output:output.into()});0
}

unsafe fn capture_texture(request:&Request)->Result<serde_json::Value,String>{
    let resource=ManuallyDrop::new(ID3D11Resource::from_raw(request.texture as *mut c_void));
    let texture=resource.cast::<ID3D11Texture2D>().map_err(|e|e.to_string())?;
    let device=texture.GetDevice().map_err(|e|e.to_string())?;let context=device.GetImmediateContext().map_err(|e|e.to_string())?;
    let mut d=D3D11_TEXTURE2D_DESC::default();texture.GetDesc(&mut d);
    let(format_bytes,compressed)=match d.Format.0{70..=72|79..=81=>(8,true),73..=78|82..=84|94..=99=>(16,true),9|10=>(8,false),1|2=>(16,false),28|29|87|91=>(4,false),v=>return Err(format!("unsupported raw texture format {v}"))};
    let staging_desc=D3D11_TEXTURE2D_DESC{Usage:D3D11_USAGE_STAGING,BindFlags:0,CPUAccessFlags:D3D11_CPU_ACCESS_READ.0 as u32,MiscFlags:0,..d};
    let mut staging=None;device.CreateTexture2D(&staging_desc,None,Some(&mut staging)).map_err(|e|e.to_string())?;let staging=staging.ok_or("texture staging absent")?;context.CopyResource(&staging,&texture);
    let mut subresources=Vec::new();
    for face in 0..d.ArraySize{for mip in 0..d.MipLevels{
        let w=(d.Width>>mip).max(1);let h=(d.Height>>mip).max(1);
        let row_bytes=if compressed{w.div_ceil(4)*format_bytes}else{w*format_bytes};let rows=if compressed{h.div_ceil(4)}else{h};
        let index=face*d.MipLevels+mip;let mut mapped=D3D11_MAPPED_SUBRESOURCE::default();context.Map(&staging,index,D3D11_MAP_READ,0,Some(&mut mapped)).map_err(|e|e.to_string())?;
        let mut bytes=Vec::new();for row in 0..rows{let p=(mapped.pData as *const u8).add((row*mapped.RowPitch) as usize);bytes.extend_from_slice(std::slice::from_raw_parts(p,row_bytes as usize));}context.Unmap(&staging,index);
        let file=suffixed(&request.output,&format!(".face{face}.mip{mip}.bin"));std::fs::write(&file,&bytes).map_err(|e|e.to_string())?;
        subresources.push(serde_json::json!({"face":face,"mip":mip,"width":w,"height":h,"file":file,"bytes":bytes.len()}));
    }}
    Ok(serde_json::json!({"passed":true,"width":d.Width,"height":d.Height,"faces":d.ArraySize,"mips":d.MipLevels,"format":d.Format.0,"subresources":subresources}))
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
    let mut resources=Vec::new();
    let mut views:[Option<ID3D11ShaderResourceView>;32]=Default::default();
    context.PSGetShaderResources(0,Some(&mut views));
    for(slot,view) in views.into_iter().enumerate(){
        let Some(view)=view else{continue;};let mut description=D3D11_SHADER_RESOURCE_VIEW_DESC::default();view.GetDesc(&mut description);
        let resource=view.GetResource().map_err(|e|e.to_string())?;
        if let Ok(texture)=resource.cast::<ID3D11Texture2D>(){
            let mut d=D3D11_TEXTURE2D_DESC::default();texture.GetDesc(&mut d);
            resources.push(serde_json::json!({"slot":slot,"dimension":description.ViewDimension.0,"format":description.Format.0,"width":d.Width,"height":d.Height,"mips":d.MipLevels,"array_size":d.ArraySize,"misc_flags":d.MiscFlags}));
        }
    }
    Ok(serde_json::json!({"passed":true,"buffers":rows,"resources":resources}))
}

unsafe extern "system" fn render_event(_: i32) {
    let Some(request) = REQUEST.lock().ok().and_then(|mut r| r.take()) else { return; };
    let mut report = match capture(&request) {
        Ok(report) => report,
        Err(error) => serde_json::json!({"passed":false,"error":error}),
    };
    if let Ok(mut textures)=TEXTURES.lock(){for texture in textures.drain(..){
        let result=match capture_texture(&texture){Ok(r)=>r,Err(e)=>serde_json::json!({"passed":false,"error":e})};
        if result["passed"] != true { report["passed"]=serde_json::json!(false); report["texture_error"]=result["error"].clone(); }
        let _=std::fs::write(suffixed(&texture.output,".texture.json"),result.to_string());
    }}
    // Completion marker follows every queued texture readback.
    let _ = std::fs::write(suffixed(&request.output, ".gpu.json"), report.to_string());
}

#[no_mangle]
pub extern "C" fn ok_shader_probe_render_event() -> *const c_void {
    render_event as *const c_void
}
