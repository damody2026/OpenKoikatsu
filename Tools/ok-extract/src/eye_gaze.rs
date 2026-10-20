//! EyeLookMaterialControll.Update. Sampler Clamp runs after UV interpolation.
#[derive(Clone,serde::Serialize,serde::Deserialize)]
pub(crate) struct IrisRuntime {
    pub settings:[f32;14],
    pub raw_uvs:Vec<[f32;2]>,
}

/// Koikano/main_eye keeps highlights on UV1/UV2, independently of MainTex.
#[derive(Clone,serde::Serialize,serde::Deserialize)]
pub(crate) struct EyeDraw {
    pub kind:i32,
    pub textures:[Option<Vec<u8>>;2],
    pub colors:[[f32;4];2],
    pub offsets:[[f32;2];2],
    pub rotations:[f32;3],
    pub color:[f32;4],
    pub enabled:f32,
    pub raw_uvs:[Vec<[f32;2]>;2],
    pub texcoord_st:[[f32;4];2],
    pub main_texcoord_st:[f32;4],
    pub sample_uvs:Vec<[f32;2]>,
}
impl EyeDraw {
    /// Native expression varying precedes pupil rotation and MainTex EyeLook ST.
    /// Koikano's vertex shader alone applies _texcoord_ST to that varying.
    pub(crate) fn expression_uv(&self,raw:[f32;2])->Option<[f32;2]> {
        let uv=match self.kind {
            1=>[raw[0]*self.main_texcoord_st[0]+self.main_texcoord_st[2],raw[1]*self.main_texcoord_st[1]+self.main_texcoord_st[3]],
            2=>raw,
            _=>return None,
        };
        uv.iter().all(|v|v.is_finite()).then_some(uv)
    }
    pub fn apply(&mut self,st:[f32;4],main:&[[f32;2]],up:&mut Vec<[f32;2]>,down:&mut Vec<[f32;2]>) {
        let rotate=|uv:[f32;2],turns:f32| {let (s,c)=(turns*std::f32::consts::TAU).sin_cos();let x=uv[0]-0.5;let y=uv[1]-0.5;
            [c*x+s*y+0.5,-s*x+c*y+0.5]};
        let transform=|uv:[f32;2],st:[f32;4]|[uv[0]*st[0]+st[2],uv[1]*st[1]+st[3]];
        // Both operations are affine, so doing them on the vertex streams is
        // equivalent to the fragment transform. Sampler clamp stays on GPU.
        self.sample_uvs=main.iter().map(|uv| if self.kind==1 {
            rotate(transform(transform(*uv,self.main_texcoord_st),st),self.rotations[0])
        } else {transform(rotate(*uv,self.rotations[0]),st)}).collect();
        for (layer,out) in [up,down].into_iter().enumerate() {
            out.clear();
            let tc=self.texcoord_st[layer];
            out.extend(self.raw_uvs[layer].iter().map(|uv| {let uv=[
                (uv[0]*tc[0]+tc[2])*st[0]+st[2]+self.offsets[layer][0],
                (uv[1]*tc[1]+tc[3])*st[1]+st[3]+self.offsets[layer][1],
            ];if self.kind==1 {rotate(uv,self.rotations[0]+self.rotations[layer+1]-0.5)}else{uv}}));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn expression_varying_ignores_pupil_and_eyelook_transforms() {
        let mut draw=EyeDraw {kind:2,textures:[None,None],colors:[[1.;4];2],offsets:[[0.;2];2],rotations:[0.25,0.5,0.5],color:[1.;4],enabled:1.,raw_uvs:[vec![],vec![]],texcoord_st:[[1.,1.,0.,0.];2],main_texcoord_st:[2.,3.,0.125,-0.25],sample_uvs:vec![]};
        let raw=[0.2,0.7];let mut up=vec![];let mut down=vec![];
        draw.apply([2.,3.,0.1,-0.2],&[raw],&mut up,&mut down);
        assert_eq!(draw.expression_uv(raw),Some(raw));
        assert_ne!(draw.sample_uvs[0],raw);
        draw.kind=1;
        assert_eq!(draw.expression_uv(raw),Some([raw[0]*2.+0.125,raw[1]*3.-0.25]));
        draw.rotations[0]=0.75;draw.apply([0.5,4.,0.2,0.3],&[raw],&mut up,&mut down);
        assert_eq!(draw.expression_uv(raw),Some([raw[0]*2.+0.125,raw[1]*3.-0.25]));
        assert_eq!(draw.expression_uv([f32::NAN,0.]),None);draw.kind=0;assert_eq!(draw.expression_uv(raw),None);
    }
    #[test]
    fn native_eye_material_transforms() {
        let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Saved/Tests/EyeGaze284");
        let mut count=0;
        for name in ["GameMarchGaze284","GameAliceGaze284"] {
            let text=crate::gamefs::read_to_string(root.join(format!("{name}.json.eye-gaze.tsv"))).expect("fresh native gaze table");
            for line in text.lines() {
                let fields:Vec<_>=line.split('\t').collect();
                if fields[0]!="formula" {continue;}
                let n=|i:usize|fields[i].parse::<f32>().unwrap();
                let runtime=IrisRuntime {settings:std::array::from_fn(|i|n(4+i)),raw_uvs:vec![]};
                for (i,value) in runtime.st(n(2),n(3)).iter().enumerate() {
                    assert!((value-n(18+i)).abs()<3e-7,"{name} formula {} ST{i} rust={value} native={}",fields[1],n(18+i));
                }
                count+=1;
            }
        }
        assert_eq!(count,400);
    }
    #[test]
    #[ignore = "loads five actual mod cards and fresh game shader fixtures"]
    fn native_eye_draw_loaded_cards() {
        let game=&crate::test_support::game_path(r"");
        let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Saved/Tests/EyeShader286");
        let mut transforms=0;let mut uv_count=0;
        for card in ["[Card] March_7th_keiyuu","Alice_keiyuu","Hayase_yuuka_keiyuu_V1_02 For KKS","KK_091207","KK_384283"] {
            let stem=card.chars().map(|c|if c.is_ascii_alphanumeric()||c=='_' {c}else{'_'}).collect::<String>()+"EyeShader286";
            let prefix=root.join(format!("{stem}.json"));
            let done:serde_json::Value=serde_json::from_str(&crate::gamefs::read_to_string(&prefix).unwrap()).unwrap();assert_eq!(done["error"],"");
            let text=crate::gamefs::read_to_string(format!("{}.eye-shader.tsv",prefix.display())).unwrap();
            let rows=text.lines().map(|l|l.split('\t').collect::<Vec<_>>()).collect::<Vec<_>>();
            let session=crate::session::open(game,Some(&game.join(format!("UserData/chara/female/{card}.png"))),0).unwrap();
            let parts=session.parts.iter().filter(|p|p.eye_draw.is_some()).collect::<Vec<_>>();
            assert_eq!(parts.len(),2,"{card}: source main_eye did not reach draw material");
            for part in parts {
                let draw=part.eye_draw.as_ref().unwrap();let runtime=part.iris_runtime.as_ref().unwrap();
                let eye=rows.iter().filter(|r|r.len()>1 && r[1]==part.name).collect::<Vec<_>>();
                assert_eq!(eye.iter().find(|r|r[0]=="shader").unwrap()[2],if draw.kind==1 {"Koikano/main_eye"}else{"Shader Forge/toon_eye_lod0"});
                let floats=|key:&str|eye.iter().find(|r|r[0]=="float"&&r[2]==key).unwrap()[3].parse::<f32>().unwrap();
                for (i,key) in ["_rotation","_rotation1","_rotation2"].iter().enumerate() {
                    if i==0 || draw.kind==1 {assert!((draw.rotations[i]-floats(key)).abs()<1e-6,"{card} {} {key} rust={} native={}",part.name,draw.rotations[i],floats(key));}
                }
                assert_eq!(draw.enabled,floats("_isHighLight"));
                for (i,key) in ["_Color","_overcolor1","_overcolor2"].iter().enumerate() {
                    if i==0 && draw.kind==2 {continue;}
                    let row=eye.iter().find(|r|r[0]=="color"&&r[2]==*key).unwrap();
                    let values=if i==0 {draw.color}else{draw.colors[i-1]};
                    for c in 0..4 {assert!((values[c]-row[3+c].parse::<f32>().unwrap()).abs()<2e-6,"{card} {} {key} channel{c}",part.name);}
                }
                let offset=eye.iter().find(|r|r[0]=="offset").unwrap();
                for layer in 0..2 {for c in 0..2 {assert!((draw.offsets[layer][c]-offset[2+layer*2+c].parse::<f32>().unwrap()).abs()<1e-6);}}
                for layer in 0..2 {
                    let key=format!("_overtex{}",layer+1);
                    let row=eye.iter().find(|r|r[0]=="texture"&&r[2]==key).unwrap();
                    assert_eq!(row[4],"1","native fixtures use clamp samplers");
                    let path=format!("{}.{}{key}.png",prefix.display(),part.name);
                    let native=image::open(path).unwrap().to_rgba8();
                    let extracted=crate::teximg::load_from_memory(draw.textures[layer].as_ref().expect("source highlight texture")).unwrap().to_rgba8();
                    extracted.save(format!("{}.{}{key}.extracted.png",prefix.display(),part.name)).unwrap();
                    assert_eq!(native.dimensions(),extracted.dimensions(),"{card} {} {key} native texture={}",part.name,row[3]);
                    let difference=|sample:&image::RgbaImage|native.as_raw().iter().zip(sample.as_raw()).map(|(a,b)|(*a as f64-*b as f64).abs()).sum::<f64>()/native.as_raw().len() as f64;
                    let error=difference(&extracted);let flipped=difference(&image::imageops::flip_vertical(&extracted));
                    eprintln!("EYE_TEXTURE_SOURCE card={card} eye={} layer={layer} mean_error_255={error} wrong_flip_error_255={flipped}",part.name);
                    assert!(error<2.,"{card} {} {key}: loaded PNG differs from native readback; mean={error} flipped={flipped}",part.name);
                }
                for row in eye.iter().filter(|r|r[0]=="uv") {
                    let layer=row[2].parse::<usize>().unwrap();let v=row[3].parse::<usize>().unwrap();
                    let uv=if layer==0 {runtime.raw_uvs[v]}else{draw.raw_uvs[layer-1][v]};
                    for c in 0..2 {assert!((uv[c]-row[4+c].parse::<f32>().unwrap()).abs()<1e-6);}
                    uv_count+=1;
                }
                for layer in 0..2 {
                    if draw.kind==2 {continue;}
                    let key=format!("_texcoord{}_ST",layer+2);
                    let row=eye.iter().find(|r|r[0]=="vector"&&r[2]==key).unwrap();
                    for c in 0..4 {assert!((draw.texcoord_st[layer][c]-row[3+c].parse::<f32>().unwrap()).abs()<1e-6,"{card} {key}");}
                }
                for row in eye.iter().filter(|r|r[0]=="st") {
                    let h=row[4].parse::<f32>().unwrap()*if runtime.settings[0]==1. {-1.}else{1.};let v=row[5].parse().unwrap();
                    let layer=if row[3]=="_overtex1" {Some(0)}else if row[3]=="_overtex2" {Some(1)}else{None};
                    let mut st=runtime.st(h,v);if let Some(l)=layer {st[2]+=draw.offsets[l][0];st[3]+=draw.offsets[l][1];}
                    for c in 0..4 {assert!((st[c]-row[6+c].parse::<f32>().unwrap()).abs()<5e-7,"{card} {} {}",part.name,row[3]);}
                    transforms+=1;
                }
            }
        }
        assert_eq!(transforms,750);assert_eq!(uv_count,5370);
        eprintln!("EYE_DRAW_NATIVE cards=5 eyes=10 transforms={transforms} raw_uv_vertices={uv_count}");
    }
}
impl IrisRuntime {
    pub fn st(&self,h:f32,v:f32)->[f32;4] {
        let s=&self.settings;
        let mut xy=[h+s[10],v+s[11]];
        let mag=(xy[0]*xy[0]+xy[1]*xy[1]).sqrt();
        if mag>1. {xy[0]/=mag;xy[1]/=mag;}
        let lerp=|a:f32,b:f32,t:f32| a+(b-a)*t.clamp(0.,1.);
        let x=lerp(s[1],s[2],(xy[0]+1.)*0.5)*(s[9]*lerp(1.,5.,s[12]));
        let y=lerp(s[4],s[3],(xy[1]+1.)*0.5)*(s[9]*lerp(1.,5.,s[13]));
        // Mathf.Clamp also defines reversed limits; Rust f32::clamp panics.
        let clamp=|value:f32,min:f32,max:f32| if value<min {min}else if value>max {max}else{value};
        let sx=1.+s[12];let sy=1.+s[13];
        [sx,sy,clamp(x,s[5],s[6])+(1.-sx)*0.5,clamp(y,s[7],s[8])+(1.-sy)*0.5]
    }
    pub fn apply(&self,uvs:&mut [[f32;2]],h:f32,v:f32) {
        let st=self.st(h,v);
        for (uv,raw) in uvs.iter_mut().zip(&self.raw_uvs) {
            *uv=[raw[0]*st[0]+st[2],raw[1]*st[1]+st[3]];
        }
    }
}
