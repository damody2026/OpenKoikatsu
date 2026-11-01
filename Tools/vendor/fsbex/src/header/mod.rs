use crate::read::Reader;
pub(crate) mod error;
use bilge::prelude::*;
use error::{
    ChunkError, ChunkErrorKind, HeaderError, HeaderErrorKind, NameError, NameErrorKind,
    StreamError, StreamErrorKind,
};
use std::{
    ffi::CStr,
    fmt::{Display, Formatter, Result as FmtResult},
    io::Read,
    iter::zip,
    num::{NonZeroU32, NonZeroU8},
    ops::Mul,
};
use tap::Pipe;

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Header {
    pub(crate) format: AudioFormat,
    pub(crate) flags: u32,
    pub(crate) stream_info: Box<[StreamInfo]>,
}

impl Header {
    pub(crate) fn parse<R: Read>(reader: &mut Reader<R>) -> Result<Self, HeaderError> {
        // check for file signature
        match reader.take_const() {
            Ok(data) if data == FSB5_MAGIC => Ok(()),
            Err(e) => Err(HeaderError::new_with_source(HeaderErrorKind::Magic, e)),
            _ => Err(HeaderError::new(HeaderErrorKind::Magic)),
        }?;

        // determines how encoding flags are read
        let version = reader
            .le_u32()
            .map_err(HeaderError::factory(HeaderErrorKind::Version))?
            .try_into()?;

        let num_streams = reader
            .le_u32()
            .map_err(HeaderError::factory(HeaderErrorKind::StreamCount))?
            .try_into()
            .map_err(|_| HeaderError::new(HeaderErrorKind::ZeroStreams))?;

        let stream_headers_size = reader
            .le_u32()
            .map_err(HeaderError::factory(HeaderErrorKind::StreamHeadersSize))?;

        let name_table_size = reader
            .le_u32()
            .map_err(HeaderError::factory(HeaderErrorKind::NameTableSize))?;

        let total_stream_size = reader
            .le_u32()
            .map_err(HeaderError::factory(HeaderErrorKind::TotalStreamSize))?
            .try_into()
            .map_err(|_| HeaderError::new(HeaderErrorKind::ZeroTotalStreamSize))?;

        let format = reader
            .le_u32()
            .map_err(HeaderError::factory(HeaderErrorKind::AudioFormat))
            .and_then(AudioFormat::parse)?;

        // read encoding flags
        let (flags, base_header_size) = match version {
            Version::V0 => (0, 64),
            Version::V1 => {
                reader
                    .skip(4)
                    .map_err(HeaderError::factory(HeaderErrorKind::EncodingFlags))?;

                let flags = reader
                    .le_u32()
                    .map_err(HeaderError::factory(HeaderErrorKind::EncodingFlags))?;

                (flags, 60)
            }
        };

        // skip unknown header data
        reader
            .advance_to(base_header_size)
            .map_err(HeaderError::factory(HeaderErrorKind::Metadata))?;

        let mut stream_info = parse_stream_headers(reader, num_streams, total_stream_size)?;

        let header_size = base_header_size + stream_headers_size as usize;

        // make sure base header + stream headers have been read
        reader.advance_to(header_size).map_err(HeaderError::factory(
            HeaderErrorKind::WrongHeaderSize {
                expected: header_size,
                actual: reader.position(),
            },
        ))?;

        // Read stream names, if present.
        // The name table has two parts: name offsets, then names (stored as null-terminated strings).
        // Differences in consecutive offsets are calculated to get the actual name lengths:
        // for example, if the first name offset is 0 and the second name offset is 12,
        // then the first name's length (including the null terminator) is 12 - 0 = 12.
        // The final name offset is subtracted from the name table size to get the final name's length.
        if name_table_size != 0 {
            let mut name_offsets = Vec::with_capacity(u32::from(num_streams) as usize + 1);

            for index in 0..num_streams.into() {
                let offset = reader
                    .le_u32()
                    .map_err(NameError::read_factory(index, NameErrorKind::NameOffset))?;

                name_offsets.push(offset);
            }
            name_offsets.push(name_table_size);

            read_stream_names(reader, &name_offsets, &mut stream_info)?;
        }

        Ok(Self {
            format,
            flags,
            stream_info: stream_info.into_boxed_slice(),
        })
    }
}

const FSB5_MAGIC: [u8; 4] = *b"FSB5";

enum Version {
    V0,
    V1,
}

impl TryFrom<u32> for Version {
    type Error = HeaderError;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::V0),
            1 => Ok(Self::V1),
            version => Err(HeaderError::new(HeaderErrorKind::UnknownVersion { version })),
        }
    }
}

/// Represents known audio formats of streams within a sound bank.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum AudioFormat {
    /// PCM with 8-bit integer samples.
    Pcm8,
    /// PCM with 16-bit integer samples.
    Pcm16,
    /// PCM with 24-bit integer samples.
    Pcm24,
    /// PCM with 32-bit integer samples.
    Pcm32,
    /// PCM with 32-bit float (IEEE 754) samples.
    PcmFloat,
    /// GC ADPCM, used in games for the GameCube, Wii and Wii U.
    GcAdpcm,
    /// IMA ADPCM, developed by the
    /// [Interactive Multimedia Association](https://en.wikipedia.org/wiki/Interactive_Multimedia_Association).
    ImaAdpcm,
    /// VAG, an ADPCM format used in games for the PS1, PS2, and PSP.
    Vag,
    /// HEVAG, an ADPCM format used in games for the PS Vita and PS4.
    /// HEVAG is an improved version of VAG that is compatible with the original format.
    HeVag,
    /// XMA, used in games for the Xbox 360.
    /// XMA is based on the Windows Media format (WMA).
    Xma,
    /// MPEG, developed by the
    /// [ISO/IEC Moving Picture Experts Group](https://en.wikipedia.org/wiki/Moving_Picture_Experts_Group).
    Mpeg,
    /// CELT, developed by the [Xiph.Org Foundation](https://en.wikipedia.org/wiki/Xiph.Org_Foundation).
    /// The CELT format is obsolete, and its functionality has been merged into Opus.
    Celt,
    /// ATRAC9, used in PlayStation games and debuting with the PS Vita.
    /// ATRAC9 is part of the ATRAC family of audio formats.
    Atrac9,
    /// xWMA, used in games for Windows and Xbox systems.
    /// xWMA is similar to the WAVE and XMA formats.
    Xwma,
    /// Vorbis, developed by the [Xiph.Org Foundation](https://en.wikipedia.org/wiki/Xiph.Org_Foundation).
    Vorbis,
    /// FADPCM, an ADPCM format developed by Firelight Technologies for use with FMOD.
    FAdpcm,
    /// Opus, developed by the [Xiph.Org Foundation](https://en.wikipedia.org/wiki/Xiph.Org_Foundation).
    /// Opus is intended to replace older Xiph.Org formats such as Vorbis.
    Opus,
}

impl AudioFormat {
    fn parse(value: u32) -> Result<Self, HeaderError> {
        match value {
            1 => Ok(Self::Pcm8),
            2 => Ok(Self::Pcm16),
            3 => Ok(Self::Pcm24),
            4 => Ok(Self::Pcm32),
            5 => Ok(Self::PcmFloat),
            6 => Ok(Self::GcAdpcm),
            7 => Ok(Self::ImaAdpcm),
            8 => Ok(Self::Vag),
            9 => Ok(Self::HeVag),
            10 => Ok(Self::Xma),
            11 => Ok(Self::Mpeg),
            12 => Ok(Self::Celt),
            13 => Ok(Self::Atrac9),
            14 => Ok(Self::Xwma),
            15 => Ok(Self::Vorbis),
            16 => Ok(Self::FAdpcm),
            17 => Ok(Self::Opus),
            flag => Err(HeaderError::new(HeaderErrorKind::UnknownAudioFormat { flag })),
        }
    }
}

impl Display for AudioFormat {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        f.write_str(match self {
            Self::Pcm8 => "PCM (8-bit, integer)",
            Self::Pcm16 => "PCM (16-bit, integer)",
            Self::Pcm24 => "PCM (24-bit, integer)",
            Self::Pcm32 => "PCM (32-bit, integer)",
            Self::PcmFloat => "PCM (32-bit, float)",
            Self::GcAdpcm => "GC ADPCM",
            Self::ImaAdpcm => "IMA ADPCM",
            Self::Vag => "VAG",
            Self::HeVag => "HEVAG",
            Self::Xma => "XMA",
            Self::Mpeg => "MPEG",
            Self::Celt => "CELT",
            Self::Atrac9 => "ATRAC9",
            Self::Xwma => "xWMA",
            Self::Vorbis => "Vorbis",
            Self::FAdpcm => "FADPCM",
            Self::Opus => "Opus",
        })
    }
}

fn parse_stream_headers<R: Read>(
    reader: &mut Reader<R>,
    num_streams: NonZeroU32,
    total_stream_size: NonZeroU32,
) -> Result<Vec<StreamInfo>, HeaderError> {
    let num_streams_usize = u32::from(num_streams) as usize;

    let mut stream_headers = Vec::with_capacity(num_streams_usize);
    let mut stream_offsets = Vec::with_capacity(num_streams_usize + 1);

    for index in 0..num_streams.into() {
        // Stream headers contain information such as sample rate (Hz) and number of channels.
        // They can also contain metadata chunks useful for decoding and encoding stream data.
        // Sometimes, flags for header fields are set to 0 while the actual values are stored in chunks.
        let mut stream_header = match reader.le_u64() {
            Ok(n) => RawStreamHeader::from(n).parse(index),
            Err(e) => Err(StreamError::new_with_source(index, StreamErrorKind::StreamInfo, e)),
        }?;

        if stream_header.has_chunks {
            parse_stream_chunks(reader, &mut stream_header)
                .map_err(|e| e.into_stream_err(index))?;
        }

        stream_offsets.push(stream_header.data_offset);
        stream_headers.push(stream_header);
    }
    stream_offsets.push(total_stream_size.into());

    // Only stream offsets are stored in stream headers, so they are processed to get stream lengths.
    // Stream lengths are calculated the same way as name lengths in the name table.

    let mut stream_info = Vec::with_capacity(num_streams_usize);

    for ((size, header), index) in zip(
        stream_offsets.windows(2).map(|window| window[1] - window[0]),
        stream_headers,
    )
    .zip(0..)
    {
        stream_info.push(
            header.with_stream_size(
                size.try_into()
                    .map_err(|_| HeaderError::new(HeaderErrorKind::ZeroStreamSize { index }))?,
            ),
        );
    }

    Ok(stream_info)
}

#[bitsize(64)]
#[derive(FromBits)]
struct RawStreamHeader {
    has_chunks: bool,
    sample_rate: u4,
    channels: u2,
    data_offset: u27,
    num_samples: u30,
}

#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct StreamHeader {
    has_chunks: bool,
    sample_rate: NonZeroU32,
    channels: NonZeroU8,
    data_offset: u32,
    num_samples: NonZeroU32,
    stream_loop: Option<Loop>,
    dsp_coeffs: Option<Box<[i16]>>,
    vorbis_crc32: Option<u32>,
}

impl RawStreamHeader {
    fn parse(self, stream_index: u32) -> Result<StreamHeader, StreamError> {
        let sample_rate = match self.sample_rate().value() {
            0 => Ok(4000),
            1 => Ok(8000),
            2 => Ok(11000),
            3 => Ok(11025),
            4 => Ok(16000),
            5 => Ok(22050),
            6 => Ok(24000),
            7 => Ok(32000),
            8 => Ok(44100),
            9 => Ok(48000),
            10 => Ok(96000),
            flag => Err(StreamError::new(
                stream_index,
                StreamErrorKind::UnknownSampleRate { flag },
            )),
        }?
        .try_into()
        .unwrap();

        let channels = match self.channels().value() {
            0 => 1,
            1 => 2,
            2 => 6,
            3 => 8,
            _ => unreachable!(),
        }
        .try_into()
        .unwrap();

        let num_samples = self
            .num_samples()
            .value()
            .try_into()
            .map_err(|_| StreamError::new(stream_index, StreamErrorKind::ZeroSamples))?;

        // Some information (e.g. playback loops) are read from stream header chunks,
        // which happens after parsing the stream header, so their values are set to None for now.
        Ok(StreamHeader {
            has_chunks: self.has_chunks(),
            sample_rate,
            channels,
            data_offset: self.data_offset().value() * 32,
            num_samples,
            stream_loop: None,
            dsp_coeffs: None,
            vorbis_crc32: None,
        })
    }
}

fn parse_stream_chunks<R: Read>(
    reader: &mut Reader<R>,
    stream: &mut StreamHeader,
) -> Result<(), ChunkError> {
    use crate::header::Loop;
    #[allow(clippy::enum_glob_use)]
    use StreamChunkKind::*;

    for index in 0.. {
        let chunk = match reader.le_u32() {
            Ok(n) => RawStreamChunk::from(n).parse(index),
            Err(e) => Err(ChunkError::new_with_source(index, ChunkErrorKind::Flag, e)),
        }?;

        let start_position = reader.position();

        match chunk.kind {
            Channels => {
                stream.channels = reader
                    .u8()
                    .map_err(ChunkError::factory(index, ChunkErrorKind::ChannelCount))?
                    .try_into()
                    .map_err(|_| ChunkError::new(index, ChunkErrorKind::ZeroChannels))?;
            }
            SampleRate => {
                stream.sample_rate = reader
                    .le_u32()
                    .map_err(ChunkError::factory(index, ChunkErrorKind::SampleRate))?
                    .try_into()
                    .map_err(|_| ChunkError::new(index, ChunkErrorKind::ZeroSampleRate))?;
            }
            Loop => {
                let start = reader
                    .le_u32()
                    .map_err(ChunkError::factory(index, ChunkErrorKind::LoopStart))?;

                let end = reader
                    .le_u32()
                    .map_err(ChunkError::factory(index, ChunkErrorKind::LoopEnd))?;

                stream.stream_loop = Some(Loop::parse(index, start, end)?);
            }
            DspCoefficients => {
                // used for decoding and encoding GC ADPCM streams

                let channels = u8::from(stream.channels);

                let mut dsp_coeffs = Vec::with_capacity(channels as usize);

                for _ in 0..channels {
                    let mut coeff = 0;

                    for _ in 0..16 {
                        coeff += reader
                            .be_i16()
                            .map_err(ChunkError::factory(index, ChunkErrorKind::DspCoefficients))?;
                    }

                    reader
                        .skip(14)
                        .map_err(ChunkError::factory(index, ChunkErrorKind::DspCoefficients))?;

                    dsp_coeffs.push(coeff);
                }

                stream.dsp_coeffs = Some(dsp_coeffs.into_boxed_slice());
            }
            VorbisSeekTable => {
                // Vorbis is a variable bitrate codec, so seek tables are used to seek to specific times.
                // This chunk starts with the CRC32 checksum of a Vorbis setup header.
                // When encoding this stream, the checksum is used to recover the original setup header.
                // The seek table is discarded because it isn't useful for stream decoding or encoding.

                stream.vorbis_crc32 = reader
                    .le_u32()
                    .map_err(ChunkError::factory(index, ChunkErrorKind::VorbisCrc32))?
                    .pipe(Some);
            }
            VorbisIntraLayers => {
                // Some Vorbis stream data is stored as multiple "layers" per channel.
                // For decoding and encoding purposes, layers simply mean that more channels are present.

                let layers = reader
                    .le_u32()
                    .map_err(ChunkError::factory(index, ChunkErrorKind::VorbisLayerCount))?;

                stream.channels = layers
                    .pipe(u8::try_from)
                    .map_err(|_| {
                        ChunkError::new(index, ChunkErrorKind::TooManyVorbisLayers { layers })
                    })?
                    .mul(u8::from(stream.channels))
                    .try_into()
                    .map_err(|_| ChunkError::new(index, ChunkErrorKind::ZeroVorbisLayers))?;
            }
            _ => {}
        }

        // make sure the entire chunk has been read before continuing
        reader
            .advance_to(start_position + chunk.size as usize)
            .map_err(ChunkError::factory(
                index,
                ChunkErrorKind::WrongChunkSize {
                    expected: chunk.size,
                    actual: reader.position() - start_position,
                },
            ))?;

        if !chunk.more_chunks {
            break;
        }
    }

    Ok(())
}

#[bitsize(32)]
#[derive(FromBits)]
struct RawStreamChunk {
    more_chunks: bool,
    size: u24,
    kind: u7,
}

struct StreamChunk {
    more_chunks: bool,
    size: u32,
    kind: StreamChunkKind,
}

enum StreamChunkKind {
    Channels,
    SampleRate,
    Loop,
    Comment,
    XmaSeekTable,
    DspCoefficients,
    Atrac9Config,
    XwmaConfig,
    VorbisSeekTable,
    PeakVolume,
    VorbisIntraLayers,
    OpusDataSize,
}

impl RawStreamChunk {
    fn parse(self, chunk_index: u32) -> Result<StreamChunk, ChunkError> {
        #[allow(clippy::enum_glob_use)]
        use StreamChunkKind::*;

        let kind = match self.kind().value() {
            1 => Ok(Channels),
            2 => Ok(SampleRate),
            3 => Ok(Loop),
            4 => Ok(Comment),
            6 => Ok(XmaSeekTable),
            7 => Ok(DspCoefficients),
            9 => Ok(Atrac9Config),
            10 => Ok(XwmaConfig),
            11 => Ok(VorbisSeekTable),
            13 => Ok(PeakVolume),
            14 => Ok(VorbisIntraLayers),
            15 => Ok(OpusDataSize),
            flag => Err(ChunkError::new(chunk_index, ChunkErrorKind::UnknownType { flag })),
        }?;

        Ok(StreamChunk {
            more_chunks: self.more_chunks(),
            size: self.size().value(),
            kind,
        })
    }
}

/// Loop information associated with a stream.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Loop {
    start: u32,
    len: NonZeroU32,
}

impl Loop {
    fn parse(index: u32, start: u32, end: u32) -> Result<Self, ChunkError> {
        let len = NonZeroU32::new(end - start)
            .ok_or_else(|| ChunkError::new(index, ChunkErrorKind::ZeroLengthLoop))?;

        Ok(Self { start, len })
    }

    /// Returns the starting position of the loop.
    /// This value refers to the offset, in bytes, from the start of the stream data.
    #[must_use]
    pub fn start(&self) -> u32 {
        self.start
    }

    /// Returns the ending position of the loop.
    /// This value refers to the offset, in bytes, from the start of the stream data.
    #[must_use]
    pub fn end(&self) -> NonZeroU32 {
        (self.start + u32::from(self.len))
            .try_into()
            .expect("the sum of u32 and NonZeroU32 must be NonZeroU32")
    }

    /// Returns the length of the loop, in bytes.
    #[must_use]
    pub fn len(&self) -> NonZeroU32 {
        self.len
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct StreamInfo {
    pub(crate) sample_rate: NonZeroU32,
    pub(crate) channels: NonZeroU8,
    pub(crate) num_samples: NonZeroU32,
    pub(crate) stream_loop: Option<Loop>,
    pub(crate) _dsp_coeffs: Option<Box<[i16]>>,
    pub(crate) vorbis_crc32: Option<u32>,
    pub(crate) size: NonZeroU32,
    pub(crate) name: Option<Box<str>>,
}

impl StreamHeader {
    fn with_stream_size(self, size: NonZeroU32) -> StreamInfo {
        // The stream name is read from the name table (if it exists), so its value is set to None for now.
        StreamInfo {
            sample_rate: self.sample_rate,
            channels: self.channels,
            num_samples: self.num_samples,
            stream_loop: self.stream_loop,
            _dsp_coeffs: self.dsp_coeffs,
            vorbis_crc32: self.vorbis_crc32,
            size,
            name: None,
        }
    }
}

fn read_stream_names<R: Read>(
    reader: &mut Reader<R>,
    name_offsets: &[u32],
    stream_info: &mut [StreamInfo],
) -> Result<(), NameError> {
    for (name_len, index) in name_offsets.windows(2).map(|window| window[1] - window[0]).zip(0..) {
        stream_info[index as usize].name = reader
            .take(name_len as usize)
            .map_err(NameError::read_factory(index, NameErrorKind::Name))?
            .pipe_as_ref(CStr::from_bytes_until_nul)
            .map_err(NameError::cstr_factory(index))?
            .to_str()
            .map_err(NameError::utf8_factory(index))?
            .pipe(Some)
            .map(Into::into)
    }

    Ok(())
}

#[cfg(test)]
mod test {
    #[allow(clippy::enum_glob_use)]
    use super::error::{ChunkErrorKind::*, HeaderErrorKind::*, StreamErrorKind::*};
    use super::{Header, RawStreamChunk, RawStreamHeader, StreamHeader, FSB5_MAGIC};
    use crate::read::Reader;
    use std::num::{NonZeroU32, NonZeroU8};

    #[test]
    fn read_magic() {
        let mut reader;

        reader = Reader::new(b"".as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Magic));

        reader = Reader::new(b"abcd".as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Magic));

        reader = Reader::new(FSB5_MAGIC.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Version));
    }

    #[test]
    fn read_version() {
        let mut reader;

        let data = b"FSB5\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Version));

        let data = b"FSB5\xFF\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(
            Header::parse(&mut reader).is_err_and(|e| e.kind() == UnknownVersion { version: 0xFF })
        );

        let data = b"FSB5\x00\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == StreamCount));
    }

    #[test]
    fn read_stream_count() {
        let mut reader;

        let data = b"FSB5\x01\x00\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == StreamCount));

        let data = b"FSB5\x01\x00\x00\x00\x00\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == ZeroStreams));

        let data = b"FSB5\x01\x00\x00\x00\x00\x00\xFF\xFF";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == StreamHeadersSize));
    }

    #[test]
    fn read_stream_headers_size() {
        let mut reader;

        let data = b"FSB5\x01\x00\x00\x000000\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == StreamHeadersSize));

        let data = b"FSB5\x01\x00\x00\x0000000000";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == NameTableSize));
    }

    #[test]
    fn read_name_table_size() {
        let mut reader;

        let data = b"FSB5\x01\x00\x00\x0000000000\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == NameTableSize));

        let data = b"FSB5\x01\x00\x00\x00000000000000";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == TotalStreamSize));
    }

    #[test]
    fn read_stream_data_size() {
        let mut reader;

        let data = b"FSB5\x01\x00\x00\x00000000000000\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == TotalStreamSize));

        let data = b"FSB5\x01\x00\x00\x000000000000000000";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == AudioFormat));
    }

    #[test]
    fn read_audio_format() {
        let mut reader;

        let data = b"FSB5\x01\x00\x00\x000000000000000000\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == AudioFormat));

        let data = b"FSB5\x01\x00\x00\x000000000000000000\x00\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(
            Header::parse(&mut reader).is_err_and(|e| e.kind() == UnknownAudioFormat { flag: 0 })
        );
    }

    #[test]
    fn read_encoding_flags() {
        let mut reader;

        let data = b"FSB5\x00\x00\x00\x000000000000000000\x01\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Metadata));

        let data = b"FSB5\x01\x00\x00\x000000000000000000\x01\x00\x00\x00";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == EncodingFlags));

        let data = b"FSB5\x01\x00\x00\x000000000000000000\x01\x00\x00\x00\x01";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == EncodingFlags));

        let data = b"FSB5\x01\x00\x00\x000000000000000000\x01\x00\x00\x0000000";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == EncodingFlags));

        let data = b"FSB5\x01\x00\x00\x000000000000000000\x01\x00\x00\x0000000000";
        reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Metadata));
    }

    #[test]
    fn read_metadata() {
        const V0_HEADER_BASE: [u8; 28] = *b"FSB5\x00\x00\x00\x000000000000000000\x01\x00\x00\x00";
        const V1_HEADER_BASE: [u8; 28] = *b"FSB5\x01\x00\x00\x000000000000000000\x01\x00\x00\x00";

        let mut reader;

        let incomplete_data = b"FSB5\x00\x00\x00\x000000000000000000\x01\x00\x00\x00\x00";
        reader = Reader::new(incomplete_data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Metadata));

        let err_v1_data = {
            let mut buf = Vec::from(V1_HEADER_BASE);
            buf.append(&mut vec![0; 28]);
            buf
        };
        reader = Reader::new(&err_v1_data);
        assert!(Header::parse(&mut reader).is_err_and(|e| e.kind() == Metadata));

        let ok_v0_data = {
            let mut buf = Vec::from(V0_HEADER_BASE);
            buf.append(&mut vec![0; 36]);
            buf
        };
        reader = Reader::new(&ok_v0_data);
        assert!(Header::parse(&mut reader).is_err_and(|e| e.is_stream_err_kind(StreamInfo)));

        let ok_v1_data = {
            let mut buf = Vec::from(V1_HEADER_BASE);
            buf.append(&mut vec![0; 32]);
            buf
        };
        reader = Reader::new(&ok_v1_data);
        assert!(Header::parse(&mut reader).is_err_and(|e| e.is_stream_err_kind(StreamInfo)));
    }

    #[test]
    fn read_stream_info() {
        let data = b"FSB5\x01\x00\x00\x00\x01\x00\x00\x00000000000000\x01\x00\x00\x00000000000000000000000000000000000000";
        let mut reader = Reader::new(data.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.is_stream_err_kind(StreamInfo)));
    }

    #[test]
    fn derived_stream_info_parsing_works() {
        #[allow(clippy::unusual_byte_groupings)]
        let data = 0b011010000101100111100000001011_111001101101001101000100110_11_1110_0;

        let mode = RawStreamHeader::from(data);

        let has_chunks = (data & 0x01) == 1;
        assert_eq!(mode.has_chunks(), has_chunks);

        let sample_rate_flag = (data >> 1) & 0x0F;
        assert_eq!(u64::from(mode.sample_rate()), sample_rate_flag);

        let channels_flag = (data >> 5) & 0x03;
        assert_eq!(u64::from(mode.channels()), channels_flag);

        let data_offset = ((data >> 7) & 0x07FF_FFFF) << 5;
        assert_eq!(u64::from(mode.data_offset()) * 32, data_offset);

        let num_samples = (data >> 34) & 0x3FFF_FFFF;
        assert_eq!(u64::from(mode.num_samples()), num_samples);
    }

    #[test]
    #[allow(clippy::unusual_byte_groupings)]
    fn parse_stream_info() {
        let data = 0b011010000101100111100000001011_111001101101001101000100110_11_1110_0;
        let mode = RawStreamHeader::from(data);
        assert!(mode
            .parse(0)
            .is_err_and(|e| e.kind() == UnknownSampleRate { flag: 0b1110 }));

        let data = 0b000000000000000000000000000000_111001101101001101000100110_11_0000_0;
        let mode = RawStreamHeader::from(data);
        assert!(mode.parse(0).is_err_and(|e| e.kind() == ZeroSamples));

        let data = 0b000000000000000000000000000001_000000000000000000000000001_01_1000_0;
        let mode = RawStreamHeader::from(data).parse(0).unwrap();
        assert_eq!(
            mode,
            StreamHeader {
                has_chunks: false,
                sample_rate: NonZeroU32::new(44100).unwrap(),
                channels: NonZeroU8::new(2).unwrap(),
                data_offset: 32,
                num_samples: NonZeroU32::new(1).unwrap(),
                stream_loop: None,
                dsp_coeffs: None,
                vorbis_crc32: None,
            }
        );
    }

    #[test]
    fn derived_stream_chunk_parsing_works() {
        #[allow(clippy::unusual_byte_groupings)]
        let data = 0b0001101_100001101110000000011001_0;

        let flags = RawStreamChunk::from(data);

        let more_chunks = (data & 0x01) == 1;
        assert_eq!(flags.more_chunks(), more_chunks);

        let size = (data >> 1) & 0x00FF_FFFF;
        assert_eq!(u32::from(flags.size()), size);

        let kind = (data >> 25) & 0x7F;
        assert_eq!(u32::from(flags.kind()), kind);
    }

    #[test]
    fn parse_stream_chunk() {
        const DATA: &[u8; 68] = b"FSB5\x01\x00\x00\x00\x01\x00\x00\x00000000000000\x01\x00\x00\x0000000000000000000000000000000000\x010000000";

        let mut reader;

        reader = Reader::new(DATA.as_slice());
        assert!(Header::parse(&mut reader).is_err_and(|e| e.is_chunk_err_kind(Flag)));

        #[allow(clippy::items_after_statements)]
        fn test_invalid_flag(kind: u8) {
            let flag = u32::from(kind).swap_bytes() << 1;
            assert!(RawStreamChunk::from(flag).parse(0).is_err());

            let full = {
                let mut buf = Vec::from(*DATA);
                buf.append(flag.to_le_bytes().to_vec().as_mut());
                buf
            };
            let mut reader = Reader::new(full.as_slice());
            assert!(Header::parse(&mut reader)
                .is_err_and(|e| e.is_chunk_err_kind(UnknownType { flag: kind })));
        }

        for flag in [0, 5, 8, 12] {
            test_invalid_flag(flag);
        }
        for flag in 16..128 {
            test_invalid_flag(flag);
        }
    }
}
