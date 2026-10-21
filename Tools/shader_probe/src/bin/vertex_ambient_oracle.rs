//! Execute installed vertex DXBC directly through D3D11 stream output.
//! Config supplies reflected constant buffers and semantic indices, not a translated shader.
use std::{ffi::c_void, path::PathBuf};
use windows::{core::PCSTR, Win32::{Foundation::HMODULE, Graphics::{Direct3D::*, Direct3D11::*, Dxgi::Common::*}}};

unsafe fn buffer(device:&ID3D11Device, bytes:&[u8], bind:D3D11_BIND_FLAG)->windows::core::Result<ID3D11Buffer>{
    let desc=D3D11_BUFFER_DESC{ByteWidth:bytes.len() as u32,Usage:D3D11_USAGE_DEFAULT,BindFlags:bind.0 as u32,..Default::default()};
    let data=D3D11_SUBRESOURCE_DATA{pSysMem:bytes.as_ptr() as *const c_void,..Default::default()};let mut out=None;
    device.CreateBuffer(&desc,Some(&data),Some(&mut out))?;Ok(out.unwrap())
}
fn raw(values:&[f32])->&[u8]{unsafe{std::slice::from_raw_parts(values.as_ptr() as *const u8,values.len()*4)}}
unsafe fn run(config:serde_json::Value)->Result<serde_json::Value,Box<dyn std::error::Error>>{
    let shader=std::fs::read(config["shader"].as_str().ok_or("shader missing")?)?;
    let mut device=None;let mut context=None;
    D3D11CreateDevice(None,D3D_DRIVER_TYPE_HARDWARE,HMODULE::default(),D3D11_CREATE_DEVICE_FLAG(0),None,D3D11_SDK_VERSION,Some(&mut device),None,Some(&mut context))?;
    let device=device.unwrap();let context=context.unwrap();
    let mut vs=None;device.CreateVertexShader(&shader,None,Some(&mut vs))?;context.VSSetShader(vs.as_ref(),None);
    let mut cbs=Vec::new();
    for row in config["buffers"].as_array().ok_or("buffers missing")?{
        let values:Vec<f32>=row.as_array().ok_or("buffer not array")?.iter().map(|v|v.as_f64().unwrap() as f32).collect();
        cbs.push(Some(buffer(&device,raw(&values),D3D11_BIND_CONSTANT_BUFFER)?));
    }
    context.VSSetConstantBuffers(0,Some(&cbs));
    let semantics=[(b"POSITION\0".as_slice(),0),(b"TANGENT\0".as_slice(),0),(b"NORMAL\0".as_slice(),0),(b"TEXCOORD\0".as_slice(),0),(b"TEXCOORD\0".as_slice(),1),(b"TEXCOORD\0".as_slice(),2),(b"TEXCOORD\0".as_slice(),3),(b"COLOR\0".as_slice(),0)];
    let elements:Vec<_>=semantics.iter().enumerate().map(|(i,(name,index))|D3D11_INPUT_ELEMENT_DESC{SemanticName:PCSTR(name.as_ptr()),SemanticIndex:*index,Format:DXGI_FORMAT_R32G32B32A32_FLOAT,InputSlot:0,AlignedByteOffset:(i*16) as u32,InputSlotClass:D3D11_INPUT_PER_VERTEX_DATA,InstanceDataStepRate:0}).collect();
    let mut layout=None;device.CreateInputLayout(&elements,&shader,Some(&mut layout))?;context.IASetInputLayout(layout.as_ref());
    let normal=config["normal_semantic"].as_u64().ok_or("normal semantic missing")? as u32;
    let ambient=config["ambient_semantic"].as_u64().ok_or("ambient semantic missing")? as u32;
    let mut declarations=Vec::new();
    for i in 0..3{declarations.push(D3D11_SO_DECLARATION_ENTRY{Stream:0,SemanticName:PCSTR(b"TEXCOORD\0".as_ptr()),SemanticIndex:normal+i,StartComponent:2,ComponentCount:1,OutputSlot:0});}
    declarations.push(D3D11_SO_DECLARATION_ENTRY{Stream:0,SemanticName:PCSTR(b"TEXCOORD\0".as_ptr()),SemanticIndex:ambient,StartComponent:0,ComponentCount:3,OutputSlot:0});
    let mut gs=None;device.CreateGeometryShaderWithStreamOutput(&shader,Some(&declarations),Some(&[24]),D3D11_SO_NO_RASTERIZED_STREAM,None,Some(&mut gs))?;context.GSSetShader(gs.as_ref(),None);
    const COUNT:usize=2048;
    let mut vertices=vec![0f32;COUNT*32];
    for i in 0..COUNT{
        let y=1.-2.*(i as f32+0.5)/COUNT as f32;let angle=i as f32*2.3999631;let radius=(1.-y*y).sqrt();let at=i*32;
        vertices[at+3]=1.;vertices[at+4]=1.;vertices[at+7]=1.;vertices[at+8]=radius*angle.cos();vertices[at+9]=y;vertices[at+10]=radius*angle.sin();vertices[at+28..at+32].fill(1.);
    }
    let vb=buffer(&device,raw(&vertices),D3D11_BIND_VERTEX_BUFFER)?;let vbs=[Some(vb)];context.IASetVertexBuffers(0,1,Some(vbs.as_ptr()),Some(&128),Some(&0));context.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    let output=buffer(&device,&vec![0u8;COUNT*24],D3D11_BIND_STREAM_OUTPUT)?;let targets=[Some(output.clone())];context.SOSetTargets(1,Some(targets.as_ptr()),Some(&0));context.Draw(COUNT as u32,0);context.SOSetTargets(0,None,None);
    let desc=D3D11_BUFFER_DESC{ByteWidth:(COUNT*24) as u32,Usage:D3D11_USAGE_STAGING,CPUAccessFlags:D3D11_CPU_ACCESS_READ.0 as u32,..Default::default()};let mut staging=None;device.CreateBuffer(&desc,None,Some(&mut staging))?;let staging=staging.unwrap();context.CopyResource(&staging,&output);
    let mut mapped=D3D11_MAPPED_SUBRESOURCE::default();context.Map(&staging,0,D3D11_MAP_READ,0,Some(&mut mapped))?;let values=std::slice::from_raw_parts(mapped.pData as *const f32,COUNT*6).to_vec();context.Unmap(&staging,0);
    let higher:Vec<Vec<f64>>=config["higher"].as_array().ok_or("higher missing")?.iter().map(|r|r.as_array().unwrap().iter().map(|v|v.as_f64().unwrap()).collect()).collect();
    let mut max_error=0f64;let mut max_signal=0f64;
    for row in values.chunks_exact(6){
        if row.iter().any(|v|!v.is_finite()){return Err("native vertex output is nonfinite".into());}
        let n=[row[0] as f64,row[1] as f64,row[2] as f64];let terms=[n[0]*n[1],n[1]*n[2],n[2]*n[2],n[2]*n[0]];
        if (n.iter().map(|x|x*x).sum::<f64>()-1.).abs()>0.00001{return Err("native normal missing or not unit length".into());}
        for c in 0..3{let expected=(0..4).map(|k|higher[c][k]*terms[k]).sum::<f64>()+higher[3][c]*(n[0]*n[0]-n[1]*n[1]);max_error=max_error.max((expected-row[3+c] as f64).abs());max_signal=max_signal.max((row[3+c] as f64).abs());}
    }
    if max_signal<0.001||max_error>0.000001{return Err(format!("native vertex ambient mismatch: error={max_error}, signal={max_signal}").into());}
    Ok(serde_json::json!({"passed":true,"shader":config["shader"],"vertices":COUNT,"max_error":max_error,"max_signal":max_signal}))
}
fn main(){
    let arguments:Vec<_>=std::env::args_os().skip(1).collect();assert_eq!(arguments.len(),2,"vertex_ambient_oracle <config.json> <report.json>");
    let result=std::fs::read(&arguments[0]).map_err(|e|e.to_string()).and_then(|bytes|serde_json::from_slice(&bytes).map_err(|e|e.to_string())).and_then(|config|unsafe{run(config)}.map_err(|e|e.to_string()));
    let report=match result{Ok(r)=>r,Err(e)=>serde_json::json!({"passed":false,"error":e})};std::fs::write(PathBuf::from(&arguments[1]),serde_json::to_vec_pretty(&report).unwrap()).unwrap();println!("{report}");if report["passed"]!=true{std::process::exit(1);}
}
