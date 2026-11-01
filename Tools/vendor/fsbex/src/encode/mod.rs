//! Various types associated with encoding stream data from sound banks.

use crate::header::{AudioFormat, StreamInfo};
use crate::read::Reader;
use std::io::{Read, Write};

mod error;
mod pcm;
mod vorbis;
mod vorbis_lookup;

pub use error::EncodeError;
use pcm::{Endianness, Format};
pub use pcm::{PcmError, PcmErrorKind};
pub use vorbis::{VorbisError, VorbisErrorKind};

pub(crate) fn encode<R: Read, W: Write>(
    format: AudioFormat,
    flags: u32,
    info: &StreamInfo,
    source: &mut Reader<R>,
    sink: W,
) -> Result<W, EncodeError> {
    // method of determining sample endianness for PCM24, PCM32, and PCMFLOAT is currently unknown
    Ok(match format {
        AudioFormat::Pcm8 => {
            // endianness doesn't matter when samples are 1 byte wide
            pcm::encode::<_, _, 1>(Format::Integer, Endianness::Little, info, source, sink)?
        }
        AudioFormat::Pcm16 => {
            // determine sample endianness from flags in file header
            let order = if flags & 0x01 == 1 {
                Endianness::Big
            } else {
                Endianness::Little
            };

            pcm::encode::<_, _, 2>(Format::Integer, order, info, source, sink)?
        }
        AudioFormat::Pcm24 => {
            pcm::encode::<_, _, 3>(Format::Integer, Endianness::Little, info, source, sink)?
        }
        AudioFormat::Pcm32 => {
            pcm::encode::<_, _, 4>(Format::Integer, Endianness::Little, info, source, sink)?
        }
        AudioFormat::PcmFloat => {
            pcm::encode::<_, _, 4>(Format::Float, Endianness::Little, info, source, sink)?
        }
        AudioFormat::Vorbis => vorbis::encode(info, source, sink)?,
        _ => return Err(EncodeError::UnsupportedFormat { format }),
    })
}
