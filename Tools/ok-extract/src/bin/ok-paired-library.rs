//! `ok-paired-library convert <PairedAnimations> <base library>/paired` converts the shared paired / common animation
//! resources into the OK base library; `ok-paired-library verify <PairedAnimations> <base library>/paired` checks
//! that the library answers everything the original did, without a Unity bundle.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (Some(command), Some(source), Some(library)) = (args.get(1), args.get(2), args.get(3)) else {
        eprintln!("usage: ok-paired-library convert|verify <PairedAnimations root> <base library>/paired");
        std::process::exit(2);
    };
    let (source, library) = (std::path::Path::new(source), std::path::Path::new(library));
    let started = std::time::Instant::now();
    let result = match command.as_str() {
        "convert" => ok_extract::paired_library::convert(source, library).map(|i| vec![format!(
            "converted {} sources, {} keys, {} raw clips ({} unsupported in the original), {} pair layers, {} pairs, {} poses, {} MotionIK assets, IK config {}; pack {} entries, {} contents, {:.1} MB",
            i.sources.len(), i.keys.len(), i.leaves, i.unsupported.len(), i.pair_layers.len(), i.pairs, i.poses, i.motions, i.ik_config, i.pack_entries, i.pack_contents, i.pack_bytes as f64 / 1048576.0)]),
        "verify" => ok_extract::paired_library::verify(source, library),
        _ => Err(format!("unknown command {command}")),
    };
    match result {
        Ok(lines) => { for l in lines { println!("OK {l}"); } println!("OK done in {:.0} s", started.elapsed().as_secs_f64()); }
        Err(e) => { eprintln!("FAILED {e}"); std::process::exit(1); }
    }
}
