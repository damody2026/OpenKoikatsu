use std::env;
use std::process::ExitCode;

use instant_meshes::{remesh_file, Options};

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(err) => {
            eprintln!("Error: {err}");
            ExitCode::from(1)
        }
    }
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut options = Options::default();
    let mut input = None;
    let mut output = None;
    let mut help = false;
    let mut threads: Option<usize> = None;
    let mut i = 0;
    while i < args.len() {
        let arg = &args[i];
        let need = |name: &str| -> Result<String, String> {
            args.get(i + 1)
                .cloned()
                .ok_or_else(|| format!("Missing argument for {name}"))
        };
        match arg.as_str() {
            "-h" | "--help" => help = true,
            "-d" | "--deterministic" => options.deterministic = true,
            "-i" | "--intrinsic" => options.extrinsic = false,
            "-b" | "--boundaries" => options.align_to_boundaries = true,
            "--no-fill-holes" => options.fill_holes = false,
            "--no-snap-boundary" => options.snap_boundary = false,
            "-D" | "--dominant" => options.pure_quad = false,
            "--cotangent" => options.cotangent = true,
            "-t" | "--threads" => {
                threads = Some(parse_u32(&need(arg)?)? as usize);
                i += 1;
            }
            "-S" | "--smooth" => {
                options.smooth_iter = parse_u32(&need(arg)?)? as i32;
                i += 1;
            }
            "-k" | "--knn" => {
                options.knn_points = parse_u32(&need(arg)?)?;
                i += 1;
            }
            "-c" | "--crease" => {
                options.crease_angle = parse_f32(&need(arg)?)?;
                i += 1;
            }
            "-r" | "--rosy" => {
                options.rosy = parse_i32(&need(arg)?)?;
                i += 1;
            }
            "-p" | "--posy" => {
                let mut posy = parse_i32(&need(arg)?)?;
                if posy == 6 {
                    posy = 3;
                }
                options.posy = posy;
                i += 1;
            }
            "-s" | "--scale" => {
                options.scale = parse_f32(&need(arg)?)?;
                i += 1;
            }
            "-f" | "--faces" => {
                options.face_count = parse_i32(&need(arg)?)?;
                i += 1;
            }
            "-v" | "--vertices" => {
                options.vertex_count = parse_i32(&need(arg)?)?;
                i += 1;
            }
            "-o" | "--output" => {
                output = Some(need(arg)?);
                i += 1;
            }
            "-F" | "--fullscreen" | "-C" | "--compat" => {
                eprintln!("Note: {arg} belongs to the original interactive viewer and is ignored.");
            }
            other if other.starts_with('-') => {
                return Err(format!("Invalid argument: \"{other}\""));
            }
            other => {
                if input.is_some() {
                    return Err("Only one input mesh can be given".into());
                }
                input = Some(other.to_string());
            }
        }
        i += 1;
    }

    let n_targets = (options.scale > 0.0) as i32
        + (options.face_count > 0) as i32
        + (options.vertex_count > 0) as i32;
    if n_targets > 1 {
        return Err("Only one of --scale, --faces and --vertices can be used at once".into());
    }
    if !(options.posy == 3 || options.posy == 4) || !(options.rosy == 2 || options.rosy == 4 || options.rosy == 6)
    {
        help = true;
        eprintln!("Error: Invalid symmetry type!");
    }

    if help || input.is_none() || output.is_none() {
        print_help();
        if help && input.is_none() {
            return Ok(());
        }
        if input.is_none() || output.is_none() {
            return Err("An input mesh and -o/--output are required. This port is batch-only; the original OpenGL viewer is not included.".into());
        }
    }

    if let Some(n) = threads {
        env::set_var("RAYON_NUM_THREADS", n.to_string());
    }

    remesh_file(
        std::path::Path::new(input.as_deref().unwrap()),
        std::path::Path::new(output.as_deref().unwrap()),
        &options,
    )
}

fn print_help() {
    println!("Instant Meshes (Rust port)");
    println!("Syntax: instant-meshes [options] -o <output.obj|ply|fbx> <input.obj|ply|fbx>");
    println!("Options:");
    println!("   -o, --output <output>     Writes the specified PLY/OBJ/FBX file");
    println!("   -t, --threads <count>     Number of threads used for the field solve");
    println!("   -d, --deterministic       Stable collapse order and a fixed random field");
    println!("   -c, --crease <degrees>    Dihedral angle threshold for creases");
    println!("   -S, --smooth <iter>       Smoothing and reprojection steps (default: 2)");
    println!("   -D, --dominant            Tri/quad dominant mesh instead of a pure tri/quad mesh");
    println!("   -i, --intrinsic           Intrinsic mode (extrinsic is the default)");
    println!("   -b, --boundaries          Align the fields to boundary edges");
    println!("   --no-fill-holes           Keep interior holes of 7+ edges open (original behaviour)");
    println!("   --no-snap-boundary        Leave boundaries where extraction put them (original behaviour)");
    println!("   -r, --rosy <number>       Orientation symmetry (2, 4, or 6)");
    println!("   -p, --posy <number>       Position symmetry (4 or 6)");
    println!("   -s, --scale <scale>       Desired edge length in the output");
    println!("   -f, --faces <count>       Desired face count of the output mesh");
    println!("   -v, --vertices <count>    Desired vertex count of the output mesh");
    println!("   -k, --knn <count>         Point-cloud mode: adjacent points to consider");
    println!("       --cotangent           Cotangent weights instead of uniform weights");
    println!("   -h, --help                Display this message");
}

fn parse_u32(s: &str) -> Result<u32, String> {
    s.parse()
        .map_err(|_| format!("Could not parse unsigned integer \"{s}\""))
}

fn parse_i32(s: &str) -> Result<i32, String> {
    s.parse()
        .map_err(|_| format!("Could not parse signed integer \"{s}\""))
}

fn parse_f32(s: &str) -> Result<f32, String> {
    s.parse()
        .map_err(|_| format!("Could not parse floating point value \"{s}\""))
}
