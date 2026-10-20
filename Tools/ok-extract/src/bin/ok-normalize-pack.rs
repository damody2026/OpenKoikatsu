fn main(){let args:Vec<_>=std::env::args().collect();
    if args.len()!=3{eprintln!("usage: ok-normalize-pack INPUT.png OUTPUT.png");std::process::exit(1);}
    match ok_extract::portable::normalize(std::path::Path::new(&args[1]),std::path::Path::new(&args[2])){
        Ok(report)=>println!("{}",serde_json::to_string_pretty(&report).unwrap()),
        Err(error)=>{eprintln!("{error}");std::process::exit(1);}
    }
}
