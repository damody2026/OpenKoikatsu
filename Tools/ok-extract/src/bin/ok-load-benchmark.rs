use std::{path::Path,time::Instant};
fn run()->Result<serde_json::Value,String>{
    let args:Vec<_>=std::env::args().collect();
    if args.len()!=4{return Err("usage: ok-load-benchmark GAME ORIGINAL_CARD PORTABLE_APNG".into());}
    for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
    let mut rounds=Vec::new();
    for index in 0..3 {
        let start=Instant::now();
        let original_parts=ok_extract::open_session(Path::new(&args[1]),Some(Path::new(&args[2])),0)?;
        let original_seconds=start.elapsed().as_secs_f64();
        let start=Instant::now();
        let portable=ok_extract::portable::load(Path::new(&args[3]))?;
        let portable_seconds=start.elapsed().as_secs_f64();
        if !portable.game_root.as_os_str().is_empty()||portable.parts.is_empty(){return Err("portable load was not independent".into());}
        rounds.push(serde_json::json!({"round":index,"original_seconds":original_seconds,"portable_seconds":portable_seconds,
            "original_parts":original_parts,"portable_parts":portable.parts.len(),"outfits":portable.coordinate_count,
            "speedup":original_seconds/portable_seconds}));
    }
    Ok(serde_json::json!({"original":args[2],"portable":args[3],"outfit":0,"rounds":rounds,
        "scope":"native session reconstruction; excludes UE object creation, GPU upload and frame performance; disk cache not flushed"}))
}
fn main(){match run(){Ok(report)=>println!("{}",serde_json::to_string_pretty(&report).unwrap()),Err(error)=>{eprintln!("{error}");std::process::exit(1);}}}
