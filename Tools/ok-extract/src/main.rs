use std::env;
use std::path::PathBuf;

fn main() {
    std::panic::set_hook(Box::new(|info| {
        eprintln!("ok-extract error: {info}");
    }));
    let mut args = env::args().skip(1);
    let Some(game) = args.next() else {
        eprintln!("usage: ok-extract <game_root> <out_dir> [card.png]");
        std::process::exit(1);
    };
    let Some(out) = args.next() else {
        eprintln!("usage: ok-extract <game_root> <out_dir> [card.png]");
        std::process::exit(1);
    };
    let game = PathBuf::from(game);
    if out == "--motion-catalogue" {
        // ok-extract . --motion-catalogue <Saved/MotionCatalogue.tsv> <Content/IndependentResources/MotionCatalogue/MotionCatalogue.json>
        let (Some(tsv), Some(json)) = (args.next(), args.next()) else {
            eprintln!("usage: ok-extract . --motion-catalogue <in.tsv> <out.json>");
            std::process::exit(1);
        };
        if let Err(e) = ok_extract::motion_catalogue::cli(PathBuf::from(tsv).as_path(), PathBuf::from(json).as_path()) { eprintln!("motion-catalogue: {e}"); std::process::exit(1); }
        return;
    }
    if out == "--okphysics" {
        let rest: Vec<String> = args.collect();
        if let Err(e) = ok_extract::okphysics::cli(&rest) { eprintln!("okphysics: {e}"); std::process::exit(1); }
        return;
    }
    if out == "--oklib" {
        let rest: Vec<String> = args.collect();
        std::env::set_var("OK_NOCACHE", "1");
        if let Err(e) = ok_extract::oklib::cli(&game, &rest) { eprintln!("oklib: {e}"); std::process::exit(1); }
        return;
    }
    if out == "--appearance" {
        let request=args.next().expect("request JSON required");
        let report=args.next().expect("report JSON required");
        let result=ok_extract::appearance::request(&game,PathBuf::from(request).as_path());
        let value=match result {Ok(v)=>v,Err(e)=>serde_json::json!({"valid":false,"error":e})};
        std::fs::write(report,serde_json::to_vec(&value).unwrap()).expect("write appearance report");
        if value["valid"]!=true {std::process::exit(1);}return;
    }
    if out == "--list-clips" {
        for name in ok_extract::clip_names(&game) {
            println!("{name}");
        }
        return;
    }
    if out == "--export-training-clips" {
        let dir=args.next().expect("output dir required");let filter=args.next().unwrap_or_default();
        let limit=args.next().and_then(|v|v.parse().ok()).unwrap_or(usize::MAX);
        match ok_extract::video_motion::export_training_clips(&game,std::path::Path::new(&dir),&filter,limit){Ok(n)=>println!("exported {n}"),Err(e)=>{eprintln!("export failed: {e}");std::process::exit(1);}}
        return;
    }
    if out == "--video-pair" {
        // ok-extract x --video-pair female.npz male_aligned.npz out_female.okanim out_male.okanim
        //   --skeleton f.json --skeleton-male m.json --smplx X.npz --model m.json --card fcard.png --male-card mcard.png [--game root] [--female-game root]
        let fm=args.next().expect("female.npz");let mm=args.next().expect("male.npz");let fo=args.next().expect("out_female");let mo=args.next().expect("out_male");
        let rest:Vec<String>=args.collect();
        let opt=|k:&str|rest.iter().position(|a|a==k).and_then(|i|rest.get(i+1)).cloned();
        let num=|k:&str,d:f64|opt(k).and_then(|v|v.parse().ok()).unwrap_or(d);
        let (sk,skm,smplx,model)=(opt("--skeleton").expect("--skeleton"),opt("--skeleton-male").expect("--skeleton-male"),opt("--smplx").expect("--smplx"),opt("--model").expect("--model"));
        let (card,mcard,game)=(opt("--card").expect("--card"),opt("--male-card").expect("--male-card"),opt("--game"));
        let mut params=ok_extract::video_motion::retarget::Params::default();
        params.arm_reach=num("--arm-reach",params.arm_reach);params.collar_scale=num("--collar-scale",params.collar_scale);params.smooth=num("--smooth",params.smooth);
        let p=std::path::Path::new;
        // --female-game: the female validation card is a game card (no portable snapshot).
        let fgame=opt("--female-game");
        let f=ok_extract::video_motion::ConvertOptions{motion:p(&fm),out:p(&fo),skeleton:p(&sk),smplx:p(&smplx),model:p(&model),card:Some(p(&card)),game:fgame.as_deref().map(std::path::Path::new),rounds:num("--rounds",4.) as usize,params:params.clone()};
        let m=ok_extract::video_motion::ConvertOptions{motion:p(&mm),out:p(&mo),skeleton:p(&skm),smplx:p(&smplx),model:p(&model),card:Some(p(&mcard)),game:game.as_deref().map(std::path::Path::new),rounds:num("--rounds",4.) as usize,params};
        match ok_extract::video_motion::convert_pair(&f,&m){Ok(v)=>println!("{}",serde_json::json!({"female":{"frames":v["female"]["frames"],"duration_seconds":v["female"]["duration_seconds"],"bad_frames":v["female"]["validation"]["bad_frames"],"frame":v["female"]["repair"]["frame"]},
            "male":{"frames":v["male"]["frames"],"bad_frames":v["male"]["validation"]["bad_frames"]},"pair_fit":v["pair_fit"],"seconds":v["seconds"]})),
            Err(e)=>{eprintln!("video pair failed: {e}");std::process::exit(1);}}
        return;
    }
    if out == "--video-clip" {
        // ok-extract x --video-clip motion.npz out.okanim --skeleton s.json --smplx SMPLX.npz --model m.json
        //   [--card c.png [--game root]] [--rounds 4] [--no-fix] [--arm-reach 1] [--collar-scale .5] [--smooth 1]
        let motion=args.next().expect("motion.npz");let target=args.next().expect("out.okanim");
        let rest:Vec<String>=args.collect();
        let opt=|k:&str|rest.iter().position(|a|a==k).and_then(|i|rest.get(i+1)).cloned();
        let num=|k:&str,d:f64|opt(k).and_then(|v|v.parse().ok()).unwrap_or(d);
        let (skeleton,smplx,model)=(opt("--skeleton").expect("--skeleton"),opt("--smplx").expect("--smplx"),opt("--model").expect("--model"));
        let (card,game)=(opt("--card"),opt("--game"));
        let mut params=ok_extract::video_motion::retarget::Params::default();
        params.fix=!rest.iter().any(|a|a=="--no-fix");
        // --frame yaw,x0,z0,ground,xz_scale : share the anchor's placement (pairs)
        if let Some(v)=opt("--frame"){let x:Vec<f64>=v.split(',').filter_map(|t|t.parse().ok()).collect();
            if x.len()==5{params.frame=Some(ok_extract::video_motion::retarget::Frame{yaw:x[0],x0:x[1],z0:x[2],ground:x[3],xz_scale:x[4]});}}
        params.arm_reach=num("--arm-reach",params.arm_reach);params.collar_scale=num("--collar-scale",params.collar_scale);params.smooth=num("--smooth",params.smooth);
        let o=ok_extract::video_motion::ConvertOptions{motion:std::path::Path::new(&motion),out:std::path::Path::new(&target),
            skeleton:std::path::Path::new(&skeleton),smplx:std::path::Path::new(&smplx),model:std::path::Path::new(&model),
            card:card.as_deref().map(std::path::Path::new),game:game.as_deref().map(std::path::Path::new),
            rounds:num("--rounds",4.) as usize,params};
        match ok_extract::video_motion::convert(&o){Ok(v)=>println!("{}",serde_json::json!({"frames":v["frames"],"duration_seconds":v["duration_seconds"],"bad_frames":v["validation"]["bad_frames"],"seconds":v["seconds"]})),
            Err(e)=>{eprintln!("video clip failed: {e}");std::process::exit(1);}}
        return;
    }
    // Offline training on native clips (video_motion::train; replaces the Python scripts).
    if matches!(out.as_str(), "--train-video-helpers" | "--train-video-limits" | "--train-video-baseline" | "--export-video-model") {
        let rest: Vec<String> = args.by_ref().collect();
        let p = |i: usize| std::path::PathBuf::from(rest.get(i).cloned().unwrap_or_else(|| { eprintln!("usage: see Tools/ok-extract/src/video_motion/train.rs"); std::process::exit(2) }));
        let r = match out.as_str() {
            "--train-video-helpers" => ok_extract::video_motion::train::train_helpers(&p(0), &p(1), &p(2)),
            "--train-video-limits" => ok_extract::video_motion::train::train_limits(&p(0), &p(1), &p(2)),
            "--train-video-baseline" => ok_extract::video_motion::train::train_baseline(&rest[1..].iter().map(std::path::PathBuf::from).collect::<Vec<_>>(), &p(0)),
            _ => ok_extract::video_motion::train::export_model(&p(0)),
        };
        match r { Ok(text) => print!("{text}"), Err(e) => { eprintln!("{out} failed: {e}"); std::process::exit(1); } }
        return;
    }
    if out == "--validate-video-clip" {
        // ok-extract x --validate-video-clip card clip out.jsonl [gameRoot]
        let card=args.next().expect("card");let clip=args.next().expect("clip");let dump=args.next().expect("out.jsonl");
        let game=args.next();
        if let Err(e)=ok_extract::video_motion::validate_dump(std::path::Path::new(&card),std::path::Path::new(&clip),game.as_deref().map(std::path::Path::new),30.,std::path::Path::new(&dump)){eprintln!("validate failed: {e}");std::process::exit(1);}
        return;
    }
    if out == "--compare-native-pose" {
        // ok-extract x --compare-native-pose card root keyfile t skeleton model [scene_mode]
        let a:Vec<String>=args.collect();
        let key=std::fs::read_to_string(&a[2]).expect("key file");
        if let Err(e)=ok_extract::video_motion::compare_native(std::path::Path::new(&a[0]),std::path::Path::new(&a[1]),key.trim(),a[3].parse().unwrap(),
            std::path::Path::new(&a[4]),std::path::Path::new(&a[5]),a.get(6).and_then(|v|v.parse().ok()).unwrap_or(0)){eprintln!("compare failed: {e}");std::process::exit(1);}
        return;
    }
    if out == "--dump-video-pose" {
        let card=args.next().expect("card");let clip=args.next().expect("clip");let t:f32=args.next().expect("time").parse().expect("time");let obj=args.next().expect("out.obj");
        let game=args.next();
        if let Err(e)=ok_extract::video_motion::dump_pose_obj(std::path::Path::new(&card),std::path::Path::new(&clip),t,std::path::Path::new(&obj),game.as_deref().map(std::path::Path::new)){eprintln!("dump failed: {e}");std::process::exit(1);}
        return;
    }
    if out == "--stretch-video-clip" {
        let card=args.next().expect("card required");let clip=args.next().expect("clip required");
        if let Err(error)=ok_extract::video_motion::stretch(std::path::Path::new(&card),std::path::Path::new(&clip),args.next().as_deref().map(std::path::Path::new)){eprintln!("stretch failed: {error}");std::process::exit(1);}
        return;
    }
    if out == "--probe-video-clip" {
        let card=args.next().expect("card required");let clip=args.next().expect("clip required");
        if let Err(error)=ok_extract::video_motion::probe(std::path::Path::new(&card),std::path::Path::new(&clip),args.next().as_deref().map(std::path::Path::new)){eprintln!("probe failed: {error}");std::process::exit(1);}
        return;
    }
    if out == "--dump-rest-skeleton" {
        let output=args.next().expect("output path required");
        let card=args.next().map(PathBuf::from);
        let result=ok_extract::video_motion::rest_skeleton_json(&game,card.as_deref())
            .and_then(|v|std::fs::write(&output,serde_json::to_vec_pretty(&v).unwrap()).map_err(|e|e.to_string()));
        if let Err(error)=result {eprintln!("rest skeleton dump failed: {error}");std::process::exit(1);}
        return;
    }
    if out == "--export-runtime-clips" {
        // A list of "<output file>\t<qualified source|clip or states: key>" lines: all in one process (the bundles and
        // controllers are parsed once). Existing outputs are kept; each line reports ok or why not.
        let list=args.next().expect("list file required");
        let text=std::fs::read_to_string(&list).expect("read list");
        let (mut ok,mut failed)=(0,0);
        for line in text.lines().filter(|l|!l.trim().is_empty()) {
            let Some((output,key))=line.split_once('\t') else {eprintln!("bad line: {line}");failed+=1;continue;};
            if std::path::Path::new(output).exists() {ok+=1;continue;}
            match ok_extract::export_common_runtime_clip(&game,key).and_then(|bytes|std::fs::write(output,bytes).map_err(|e|e.to_string())) {
                Ok(())=>{ok+=1;println!("ok\t{output}");}
                Err(e)=>{failed+=1;println!("fail\t{output}\t{e}");}
            }
        }
        println!("exported {ok}, failed {failed}");
        return;
    }
    if out == "--posed-objs" {
        // ok-extract <game> --posed-objs <out dir> <card> (OK_OUTFIT): the session's skinned parts as OBJs.
        let dir = PathBuf::from(args.next().expect("output dir required"));
        let card = PathBuf::from(args.next().expect("card required"));
        let outfit = std::env::var("OK_OUTFIT").ok().and_then(|s| s.parse().ok()).unwrap_or(0);
        match ok_extract::export_session_posed_objs(&game, &card, outfit, &dir) {
            Ok(n) => println!("wrote {n} parts to {}", dir.display()),
            Err(e) => { eprintln!("ok-extract error: {e}"); std::process::exit(1); }
        }
        return;
    }
    if out == "--export-runtime-clip" {
        let key=args.next().expect("qualified source|clip required");
        let output=args.next().expect("output path required");
        let result=ok_extract::export_common_runtime_clip(&game,&key).and_then(|bytes|{
            use std::io::Write;
            let mut file=std::fs::OpenOptions::new().write(true).create_new(true).open(&output).map_err(|e|e.to_string())?;
            file.write_all(&bytes).map_err(|e|e.to_string())
        });
        if let Err(error)=result {eprintln!("common runtime clip export failed: {error}");std::process::exit(1);}
        return;
    }
    let out = PathBuf::from(out);
    let card = args.next().map(PathBuf::from);
    if std::env::var("OK_SESSION_ONLY").is_ok() {
        let t0 = std::time::Instant::now();
        let outfit = std::env::var("OK_OUTFIT")
            .ok()
            .and_then(|s| s.parse().ok())
            .unwrap_or(0);
        match ok_extract::open_session(&game, card.as_deref(), outfit) {
            Ok(n) => {
                println!(
                    "session parts={n} {:.1}ms",
                    t0.elapsed().as_secs_f64() * 1000.0
                );
                return;
            }
            Err(e) => {
                eprintln!("ok-extract error: {e}");
                std::process::exit(1);
            }
        }
    }
    let run = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // OK_OUTFIT picks the coordinate here too (default 0), as for OK_SESSION_ONLY.
        let outfit = std::env::var("OK_OUTFIT").ok().and_then(|s| s.parse().ok()).unwrap_or(0);
        ok_extract::extract_card_outfit(&game, card.as_deref(), &out, outfit)
    }));
    match run {
        Ok(Ok(manifest)) => println!("wrote {}", manifest.display()),
        Ok(Err(e)) => {
            eprintln!("ok-extract error: {e}");
            std::process::exit(1);
        }
        Err(_) => {
            eprintln!("ok-extract error: internal panic");
            std::process::exit(1);
        }
    }
}
