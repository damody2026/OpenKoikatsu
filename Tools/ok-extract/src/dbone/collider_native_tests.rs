//! Direct installed Unity collider oracle, including mirrored/singular rigs.
use super::*;
use serde_json::{Value,json};
fn vector<const N:usize>(value:&Value)->[f32;N]{
    let fields=["x","y","z","w"];
    std::array::from_fn(|i|{let v=value[fields[i]].as_f64().unwrap() as f32;assert!(v.is_finite());v})
}
#[test]
#[ignore="installed renderer-free direct collider oracle; fresh fixture/report required"]
fn native_collider_all_axes_mirrored_scales(){
    let fixture=std::env::var("OK_COLLIDER_FIXTURE").expect("native fixture required");
    let report=std::env::var("OK_COLLIDER_REPORT").expect("fresh report required");
    let dump:Value=serde_json::from_slice(&crate::gamefs::read(fixture).unwrap()).unwrap();
    let cases=dump["cases"].as_array().unwrap();assert!(matches!(cases.len(),162|180));
    let mut rows=Vec::new();let mut max=0f32;let mut stable_max=0f32;let mut scale_max=0f32;let mut endpoint_max=0f32;
    let mut stable_count=0usize;let mut near_axis_count=0usize;let mut rotation_max=0f32;let mut identity_max=0f32;
    let mut moved=0usize;let mut unchanged=0usize;let mut covered=HashMap::new();
    for case in cases{
        let nodes=case["nodes"].as_array().unwrap();assert_eq!(nodes.len(),3);
        let tfs=nodes.iter().map(|n|{
            let id=n["id"].as_i64().unwrap();let parent=n["parent"].as_i64().unwrap();
            (id,Tf{name:n["name"].as_str().unwrap().into(),parent:(parent!=0).then_some(parent),sibling_index:Some(id as u32),
                pos:vector(&n["pos"]),rot:vector(&n["rot"]),scale:vector(&n["scale"])})
        }).collect();
        let body=Skeleton{tfs,names:vec![],bone_ids:vec![],worlds:vec![],rest:vec![],smr_count:0,source_mesh:0};
        let collider=BodyCollider{transform:3,center:vector(&case["center"]),radius:case["radius"].as_f64().unwrap() as f32,
            height:case["height"].as_f64().unwrap() as f32,direction:case["direction"].as_u64().unwrap() as usize,inside:case["inside"].as_bool().unwrap()};
        let world=world_of(3,&body.tfs,&mut HashMap::new());let scale=transform_lossy_scale(3,&body.tfs,&world)[2].abs();
        let native_scale=vector::<3>(&nodes[2]["lossyScale"])[2].abs();
        let actual_rotation=lossy_scale_rotation(3,&body.tfs).0;let native_rotation=vector::<4>(&nodes[2]["worldRot"]);
        let re=[1f32,-1.].into_iter().map(|sign|actual_rotation.into_iter().zip(native_rotation).map(|(a,b)|(a-sign*b).abs()).fold(0f32,f32::max)).fold(f32::MAX,f32::min);
        rotation_max=rotation_max.max(re);
        let se=(scale-native_scale).abs();scale_max=scale_max.max(se);
        let half=((collider.height-collider.radius)*0.5).max(0.);let mut lo=collider.center;let mut hi=lo;
        lo[collider.direction]-=half;hi[collider.direction]+=half;
        let ee=vlen(vsub(mat_vec_local(&world,lo),vector(&case["a"]))).max(vlen(vsub(mat_vec_local(&world,hi),vector(&case["b"]))));
        endpoint_max=endpoint_max.max(ee);
        let samples=case["samples"].as_array().unwrap();assert_eq!(samples.len(),45);
        let mut cm=0f32;let mut stable_cm=0f32;let mut case_moved=0;
        for sample in samples{
            let input=vector(&sample["input"]);let expected=vector(&sample["output"]);
            let radius=sample["particleRadius"].as_f64().unwrap() as f32;
            let mut actual=input;collide_particle(&mut actual,radius,&collider,&body);
            assert!(actual.into_iter().all(f32::is_finite));
            let mut external=input;collide_particle_world(&mut external,radius,&collider,&world,scale);
            assert_eq!(actual,external,"authored and external scalar paths disagree");
            let error=vlen(vsub(actual,expected));cm=cm.max(error);
            // OutsideSphere/Capsule deliberately tests distance > 0 with no
            // epsilon. Near that singularity, ulp-sized endpoint differences
            // change direction by an arbitrary amount. Preserve raw failures
            // separately; do not regularize product behavior or claim parity.
            let a=vector(&case["a"]);let b=vector(&case["b"]);let ab=vsub(b,a);
            let length=vdot(ab,ab);let projection=vdot(vsub(input,a),ab);
            let closest=if half<=0.||projection<=0.{a}else if projection>=length{b}else{vadd(a,vscale(ab,projection/length))};
            let identity=case["name"].as_str().unwrap().starts_with("scale9_");
            if identity{identity_max=identity_max.max(error);}
            if vlen(vsub(input,closest))<2e-6&&!collider.inside&&!identity {
                near_axis_count+=1;
            }else{stable_count+=1;stable_cm=stable_cm.max(error);stable_max=stable_max.max(error);}
            if vlen(vsub(input,expected))>1e-6{moved+=1;case_moved+=1;}else{unchanged+=1;}
            let key=(collider.direction,collider.inside,sample["region"].as_str().unwrap().to_string());
            *covered.entry(key).or_insert(0usize)+=1;
        }
        assert!(case_moved>0,"zero-response native control {}",case["name"]);
        max=max.max(cm);rows.push(json!({"case":case["name"],"samples":samples.len(),"changed_native_samples":case_moved,"max_m":cm,"stable_max_m":stable_cm,"scale_z_error":se,"endpoint_max_m":ee}));
    }
    assert_eq!(covered.len(),18);assert!(moved>0&&unchanged>0);
    let passed=stable_max<2e-6&&scale_max<2e-6&&endpoint_max<2e-6&&rotation_max<2e-6;
    std::fs::write(report,serde_json::to_vec_pretty(&json!({"passed":passed,"cases":cases.len(),"samples":cases.len()*45,"identity_max_m":identity_max,
        "max_m":max,"all_samples_passed":max<2e-6,"stable_max_m":stable_max,"stable_samples":stable_count,"near_axis_samples":near_axis_count,
        "scale_z_max":scale_max,"rotation_component_max":rotation_max,"endpoint_max_m":endpoint_max,"changed_native_samples":moved,"unchanged_native_samples":unchanged,
        "tolerance":2e-6,"rows":rows,"scene_normality_verified":false,"clothing_intersections_verified":false})).unwrap()).unwrap();
    eprintln!("native collider cases={} samples={} stable={stable_max:.9}m raw={max:.9}m scale={scale_max:.9} endpoint={endpoint_max:.9} changed={moved} unchanged={unchanged} near_axis={near_axis_count}",cases.len(),cases.len()*45);
    assert!(passed,"native collider parity failed; inspect preserved report");
}
