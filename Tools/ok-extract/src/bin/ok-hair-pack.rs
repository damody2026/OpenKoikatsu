use std::path::Path;
fn main() {
    let a:Vec<String>=std::env::args().collect();
    let result=match a.get(1).map(String::as_str) {
        Some("inspect") if a.len()==4 => ok_extract::portable::inspect_hair_base(Path::new(&a[2]),Path::new(&a[3])),
        Some("import") if a.len()==5 => ok_extract::portable::import_hair_card(Path::new(&a[2]),Path::new(&a[3]),Path::new(&a[4])),
        _=>Err("usage: ok-hair-pack inspect BASE.png OUTPUT.json | import BASE.png HAIR.json OUTPUT.png".into()),
    };
    match result {Ok(v)=>println!("{}",serde_json::to_string_pretty(&v).unwrap()),Err(e)=>{eprintln!("{e}");std::process::exit(1)}}
}
