use super::vorbis_lookup::VORBIS_LOOKUP;
use crate::header::StreamInfo;
use crate::read::{ReadError, Reader};
use lewton::{
    audio::{read_audio_packet_generic, PreviousWindowRight},
    header::{read_header_ident, read_header_setup, IdentHeader, SetupHeader},
};
use std::{
    error::Error,
    fmt::{Display, Formatter, Result as FmtResult},
    io::{Error as IoError, Read, Write},
};
use tap::Pipe;
use vorbis_rs::{VorbisBitrateManagementStrategy, VorbisEncoderBuilder};

pub(super) fn encode<R: Read, W: Write>(
    info: &StreamInfo,
    source: &mut Reader<R>,
    sink: W,
) -> Result<W, VorbisError> {
    // The stream should have contained the CRC32 of a setup header in a header chunk.
    // Otherwise, the stream cannot be encoded correctly.
    let crc32 = info
        .vorbis_crc32
        .ok_or_else(|| VorbisError::new(VorbisErrorKind::MissingCrc32))?;

    // construct headers needed for decoding packets from stream data
    let (id_header, setup_header) =
        init_headers(info.sample_rate.into(), info.channels.into(), crc32)?;

    // construct encoder that prioritizes audio quality
    let mut encoder = VorbisEncoderBuilder::new(info.sample_rate, info.channels, sink)
        .map_err(VorbisError::from_vorbis(VorbisErrorKind::CreateEncoder))?
        .bitrate_management_strategy(VorbisBitrateManagementStrategy::QualityVbr {
            target_quality: 1.0,
        })
        .build()
        .map_err(VorbisError::from_vorbis(VorbisErrorKind::CreateEncoder))?;

    let start_pos = source.position();
    let stream_size = u32::from(info.size) as usize;
    let mut window = PreviousWindowRight::new();

    while source.position() - start_pos < stream_size {
        // OpenKoikatsu: some streams (Koikatsu Sunshine kks_bgm_14 and 260 Studio voices) end with a single padding
        // byte instead of a 0 / 0xFFFF packet size; reading a size from it failed the whole stream.
        if stream_size - (source.position() - start_pos) < 2 {
            break;
        }
        let packet_size = source
            .le_u16()
            .map_err(VorbisError::from_read(VorbisErrorKind::ReadPacket))?;

        // signals end of stream data
        if packet_size == u16::MIN || packet_size == u16::MAX {
            break;
        }

        let packet = source
            .take(packet_size as usize)
            .map_err(VorbisError::from_read(VorbisErrorKind::ReadPacket))?;

        let block: Vec<_> =
            read_audio_packet_generic(&id_header, &setup_header, packet.as_slice(), &mut window)
                .map_err(Into::into)
                .map_err(VorbisError::from_lewton(VorbisErrorKind::DecodePacket))?;

        encoder
            .encode_audio_block(block)
            .map_err(VorbisError::from_vorbis(VorbisErrorKind::EncodeBlock))?;
    }

    encoder
        .finish()
        .map_err(VorbisError::from_vorbis(VorbisErrorKind::FinishStream))
}

// default blocksize values for FMOD sound banks are 256 and 2048
const MIN_BLOCK_SIZE_EXP2: u8 = 8;
const MAX_BLOCK_SIZE_EXP2: u8 = 11;

fn init_headers(
    sample_rate: u32,
    channels: u8,
    crc32: u32,
) -> Result<(IdentHeader, SetupHeader), VorbisError> {
    // construct identification header from scratch
    let id_header = init_id_header_data(sample_rate, channels)
        .expect("writing to an in-memory buffer is infallible")
        .pipe_as_ref(read_header_ident)
        .map_err(Into::into)
        .map_err(VorbisError::from_lewton(VorbisErrorKind::CreateHeaders))?;

    // construct setup header from lookup table
    let setup_header_data = *VORBIS_LOOKUP
        .get(&crc32)
        .ok_or_else(|| VorbisError::new(VorbisErrorKind::Crc32Lookup))?;

    let setup_header = read_header_setup(
        setup_header_data,
        channels,
        (MIN_BLOCK_SIZE_EXP2, MAX_BLOCK_SIZE_EXP2),
    )
    .map_err(Into::into)
    .map_err(VorbisError::from_lewton(VorbisErrorKind::CreateHeaders))?;

    Ok((id_header, setup_header))
}

fn init_id_header_data(sample_rate: u32, channels: u8) -> Result<Vec<u8>, IoError> {
    // Vorbis file header information taken from:
    // [1]: https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html (sections 4.1.1 and 4.1.2)

    const BLOCK_SIZES: u8 = (MAX_BLOCK_SIZE_EXP2 << 4) | (MIN_BLOCK_SIZE_EXP2);

    let mut data = Vec::with_capacity(30);

    data.write_all(&[1])?;
    data.write_all(b"vorbis")?;
    data.write_all(&[0; 4])?;
    data.write_all(&[channels])?;
    data.write_all(sample_rate.to_le_bytes().as_slice())?;
    data.write_all(&[0; 4])?;
    data.write_all(&[0; 4])?;
    data.write_all(&[0; 4])?;
    data.write_all(&[BLOCK_SIZES])?;
    data.write_all(&[1])?;

    Ok(data)
}

/// Represents an error that can occur when encoding a Vorbis stream.
///
/// See [`VorbisErrorKind`] for the different kinds of errors that can occur.
#[derive(Debug)]
pub struct VorbisError {
    kind: VorbisErrorKind,
    source: Option<VorbisErrorSource>,
}

/// A variant of a [`VorbisError`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum VorbisErrorKind {
    /// A CRC32 checksum was not found in the stream header within the sound bank.
    /// This checksum is needed to reconstruct the Vorbis decoder state and encode audio samples.
    MissingCrc32,
    /// Failed to create the file headers needed for the Vorbis decoder.
    CreateHeaders,
    /// The stream's associated CRC32 checksum was found, but it did not match any existing entries in the lookup table.
    Crc32Lookup,
    /// Failed to create the Vorbis encoder for writing audio samples.
    CreateEncoder,
    /// Failed to read an audio packet from the stream data.
    ReadPacket,
    /// Failed to decode an audio packet from the stream data into a sample.
    DecodePacket,
    /// Failed to encode an audio sample to the writer.
    EncodeBlock,
    /// Failed to flush the writer after encoding the entire stream.
    FinishStream,
}

#[derive(Debug)]
enum VorbisErrorSource {
    Encode(vorbis_rs::VorbisError),
    Decode(lewton::VorbisError),
    Read(ReadError),
}

impl VorbisError {
    fn new(kind: VorbisErrorKind) -> Self {
        Self { kind, source: None }
    }

    fn from_vorbis(kind: VorbisErrorKind) -> impl FnOnce(vorbis_rs::VorbisError) -> Self {
        move |source| Self {
            kind,
            source: Some(VorbisErrorSource::Encode(source)),
        }
    }

    fn from_lewton(kind: VorbisErrorKind) -> impl FnOnce(lewton::VorbisError) -> Self {
        move |source| Self {
            kind,
            source: Some(VorbisErrorSource::Decode(source)),
        }
    }

    fn from_read(kind: VorbisErrorKind) -> impl FnOnce(ReadError) -> Self {
        move |source| Self {
            kind,
            source: Some(VorbisErrorSource::Read(source)),
        }
    }

    /// Returns the [`VorbisErrorKind`] associated with this error.
    #[must_use]
    pub fn kind(&self) -> VorbisErrorKind {
        self.kind
    }
}

impl Display for VorbisError {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        self.kind.fmt(f)
    }
}

impl Error for VorbisError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match &self.source {
            Some(source) => match source {
                VorbisErrorSource::Encode(e) => Some(e),
                VorbisErrorSource::Decode(e) => Some(e),
                VorbisErrorSource::Read(e) => Some(e),
            },
            None => None,
        }
    }
}

impl Display for VorbisErrorKind {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        f.write_str(match self {
            Self::MissingCrc32 => "file header did not contain CRC32 of Vorbis setup header",
            Self::CreateHeaders => "failed to create dummy Vorbis headers",
            Self::Crc32Lookup => "CRC32 of Vorbis setup header was not found in lookup table",
            Self::CreateEncoder => "failed to create Vorbis stream encoder",
            Self::ReadPacket => "failed to read audio packet from Vorbis stream",
            Self::DecodePacket => "failed to decode audio packet from Vorbis stream",
            Self::EncodeBlock => "failed to encode block of samples",
            Self::FinishStream => "failed to finalize writing Vorbis stream data",
        })
    }
}
