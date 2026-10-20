use std::path::Path;

fn interesting_abm(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.contains("kokan")
        || n.contains("vagina")
        || n.contains("thigh")
        || n.contains("siri")
        || n.contains("waist")
        || n.contains("leg")
        || n.contains("dan")
}

fn summarize(path: &Path) -> Result<(), String> {
    let info = ok_extract::parse_card(path)?;
    let name = path.file_name().and_then(|s| s.to_str()).unwrap_or("?");
    let guid = info.body_guid.clone().unwrap_or_else(|| "-".into());
    let extremes: Vec<String> = info
        .shape_body
        .iter()
        .enumerate()
        .filter(|(_, v)| (**v - 0.5).abs() > 0.35)
        .map(|(i, v)| format!("{i}:{v:.3}"))
        .collect();
    let abm: Vec<String> = info
        .abm
        .iter()
        .filter(|b| interesting_abm(&b.name))
        .map(|b| {
            let d = b.for_coord(0);
            format!(
                "{} loc={} scl={:?} len={:.3} pos={:?} rot={:?}",
                b.name, b.location, d.scale, d.length, d.pos, d.rot
            )
        })
        .collect();
    let floats: Vec<String> = info
        .me_float
        .iter()
        .filter(|((ty, _, _, mat, prop), _)| {
            *ty == 4
                && (mat.to_ascii_lowercase().contains("body")
                    || prop.to_ascii_lowercase().contains("line")
                    || prop.to_ascii_lowercase().contains("alpha")
                    || prop.to_ascii_lowercase().contains("mask"))
        })
        .map(|((_, coord, slot, mat, prop), v)| format!("{coord}/{slot}/{mat}/{prop}={v}"))
        .collect();
    println!(
        "{name}\tsex={}\tguid={guid}\tline={}\tnip={}\tunderhair={}\tshapes={}\tabm={}\tfloats={}",
        info.sex,
        info.body_draw_add_line,
        info.nip_id,
        info.underhair_id,
        extremes.join(","),
        abm.len(),
        floats.len()
    );
    if name.eq_ignore_ascii_case("C019.png") || name.eq_ignore_ascii_case("KK_384914.png") {
        println!("  SHAPE {}", extremes.join(" "));
        for row in &abm {
            println!("  ABM {row}");
        }
        for row in &floats {
            println!("  FLOAT {row}");
        }
        println!("  SHAPE_ALL {:?}", info.shape_body);
        println!("  PREG {:?}", info.pregnancy.enabled);
    }
    Ok(())
}

fn main() {
    let mut args = std::env::args_os().skip(1);
    let Some(first) = args.next() else {
        eprintln!("usage: dump_crotch_cards <card.png> [more.png]");
        std::process::exit(1);
    };
    let mut failed = false;
    for path in std::iter::once(first).chain(args) {
        if let Err(error) = summarize(Path::new(&path)) {
            eprintln!("{}: {error}", Path::new(&path).display());
            failed = true;
        }
    }
    if failed {
        std::process::exit(1);
    }
}
