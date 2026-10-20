//! Reproduce the same appearance request used by the editor, without a UI.
use std::path::Path;
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len()==3&&args[1]=="inspect"{
        let s=ok_extract::portable::load(Path::new(&args[2])).expect("load portable card");
        let parts=s.parts.iter().map(|p|{
            let bounds=|positions:&Vec<[f32;3]>|{
                let mut lo=[f32::INFINITY;3];let mut hi=[f32::NEG_INFINITY;3];
                for v in positions{for k in 0..3{lo[k]=lo[k].min(v[k]);hi[k]=hi[k].max(v[k]);}}
                serde_json::json!({"min":lo,"max":hi})
            };
            serde_json::json!({"name":p.name,"role":p.role,"visible":p.render_visible,"vertices":p.positions.len(),"rest":bounds(&p.rest_positions),"posed":bounds(&p.positions)})
        }).collect::<Vec<_>>();
        println!("{}",serde_json::json!({"valid":true,"parts":parts}));return;
    }
    if args.len() != 4 {
        eprintln!("usage: ok-appearance <game_root> <request.json> <report.json>");
        std::process::exit(2);
    }
    let started = std::time::Instant::now();
    let result = ok_extract::appearance::request(Path::new(&args[1]), Path::new(&args[2]));
    let report = match result {
        Ok(mut report) => { report["elapsed_seconds"] = started.elapsed().as_secs_f64().into(); report },
        Err(error) => serde_json::json!({"valid":false,"error":error,"elapsed_seconds":started.elapsed().as_secs_f64()}),
    };
    std::fs::write(&args[3], serde_json::to_vec_pretty(&report).unwrap()).expect("write report");
    println!("{}", report);
    if report["valid"] == false { std::process::exit(1); }
}
