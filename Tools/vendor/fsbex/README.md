# fsbex

[![Crates.io](https://img.shields.io/crates/v/fsbex)](https://crates.io/crates/fsbex)
[![Docs.rs](https://img.shields.io/docsrs/fsbex)](https://docs.rs/fsbex)
[![License](https://img.shields.io/crates/l/fsbex)](#license)

`fsbex` is a library for extracting audio from FMOD sound banks. Only FSB version 5 is supported for now.

## Example

Parsing a sound bank, then writing streams to files:

```rust
use fsbex::{Bank, AudioFormat};
use std::{
    error::Error,
    io::{BufReader, BufWriter},
    fs::File,
};

fn main() -> Result<(), Box<dyn Error>> {
    // open file for reading sound bank
    let file = BufReader::new(File::open("example.fsb")?);
    let bank = Bank::new(file)?;

    // report number of streams contained within the sound bank
    println!("{} streams within this sound bank", bank.num_streams());

    // check stream audio format
    if bank.format() != AudioFormat::Vorbis {
        return Err("expected Vorbis format".into());
    }

    // iterate over streams
    for (index, stream) in bank.into_iter().enumerate() {
        // check stream name
        let file_name = if let Some(name) = stream.name() {
            format!("{name}.ogg")
        } else {
            format!("stream_{index}.ogg")
        };

        // write stream data to file
        let output_file = BufWriter::new(File::create(file_name)?);
        stream.write(output_file)?;
    }

    Ok(())
}
```

## Supported formats

`fsbex` supports encoding stream data for the following formats:
- PCM (8, 16, 24, 32-bit integer)
- PCM (32-bit float)
- Vorbis

## Acknowledgements

`fsbex` would not be possible without these projects:
- [vgmstream](https://github.com/vgmstream/vgmstream)
- [Fmod5Sharp](https://github.com/SamboyCoding/Fmod5Sharp)

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

## Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions.
