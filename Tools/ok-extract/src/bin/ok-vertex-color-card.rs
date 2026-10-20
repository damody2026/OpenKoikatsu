//! ok-vertex-color-card convert INPUT.png OUTPUT.png BAKE_ROOT [--ratio R] [--edge-texels T] [--color-weight W] [--detail-fraction F]
//!   BAKE_ROOT/outfitN/bake.json comes from ok.BakeCardAlbedo; outfits without one stay textured.
//! ok-vertex-color-card --probe CARD.png [OUTFIT]
use std::path::{Path, PathBuf};

fn usage()->!{
    eprintln!("usage: ok-vertex-color-card convert INPUT.png OUTPUT.png BAKE_ROOT [--ratio R] [--edge-texels T] [--color-weight W] [--detail-fraction F]");
    eprintln!("       ok-vertex-color-card --probe CARD.png [OUTFIT]");
    std::process::exit(1)
}

fn main(){
    let args:Vec<String>=std::env::args().collect();
    let result=match args.get(1).map(String::as_str) {
        Some("--probe") if args.len()==3||args.len()==4 => ok_extract::vertcolor::probe(Path::new(&args[2]),
            args.get(3).and_then(|s|s.parse().ok()).unwrap_or(0)),
        Some("convert") if args.len()>=5 => {
            let mut options=ok_extract::vertcolor::Options::default();
            let mut rest=args[5..].iter();
            while let Some(flag)=rest.next() {
                let value=rest.next().and_then(|v|v.parse::<f64>().ok()).unwrap_or_else(||usage());
                match flag.as_str() {
                    "--ratio"=>options.ratio=value as f32,
                    "--edge-texels"=>options.edge_texels=value as f32,
                    "--color-weight"=>options.color_weight=value,
                    "--detail-fraction"=>options.detail_fraction=value as f32,
                    _=>usage(),
                }
            }
            let root=PathBuf::from(&args[4]);
            ok_extract::vertcolor::convert_card(Path::new(&args[2]),Path::new(&args[3]),
                &|outfit|Some(root.join(format!("outfit{outfit}"))),&options)
        }
        _ => usage(),
    };
    match result {
        Ok(report)=>println!("{}",serde_json::to_string_pretty(&report).unwrap()),
        Err(error)=>{eprintln!("{error}");std::process::exit(1);}
    }
}
