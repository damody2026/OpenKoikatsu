// Writes the original H breathing tables (all four experience banks) as the json KKHvoice.cpp reads:
//   ok-hbreath GAME_ROOT OUTPUT.json   (OUTPUT usually GAME_ROOT/voice_extract/h_breath_index.json)
fn main() {
    let a: Vec<String> = std::env::args().collect();
    if a.len() != 3 {
        eprintln!("usage: ok-hbreath GAME_ROOT OUTPUT.json");
        std::process::exit(2);
    }
    let json = ok_extract::h_breath_table(std::path::Path::new(&a[1]));
    if let Err(e) = std::fs::write(&a[2], &json) {
        eprintln!("{e}");
        std::process::exit(1)
    }
    println!("{} bytes", json.len());
}
