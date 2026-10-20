use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args_os().skip(1);
    let card = PathBuf::from(args.next().ok_or("usage: dump-ksox <card.png> <output-dir>")?);
    let output = PathBuf::from(args.next().ok_or("usage: dump-ksox <card.png> <output-dir>")?);
    let info = ok_extract::parse_card(&card)?;
    println!("skin={:?} sub={:?} nail={:?} paint={:?} underhair={}",
        info.skin_color, info.skin_sub_color, info.nail_color, info.paint_ids, info.underhair_id);
    std::fs::create_dir_all(&output)?;
    for ((coordinate, kind), png) in info.ksox {
        let path = output.join(format!("coordinate-{coordinate}-kind-{kind}.png"));
        std::fs::write(&path, png)?;
        println!("{}", path.display());
    }
    Ok(())
}
