use super::pcm::PcmError;
use super::vorbis::VorbisError;
use crate::header::AudioFormat;
use std::{
    error::Error,
    fmt::{Display, Formatter, Result as FmtResult},
};

/// Represents an error that can occur when encoding a sound bank stream.
#[derive(Debug)]
#[non_exhaustive]
pub enum EncodeError {
    /// Encoding is not implemented for this audio format yet.
    UnsupportedFormat {
        /// The audio format of streams in the sound bank.
        format: AudioFormat,
    },
    /// Failed to encode a PCM stream.
    /// See [`PcmError`] for more information.
    Pcm(PcmError),
    /// Failed to encode a Vorbis stream.
    /// See [`VorbisError`] for more information.
    Vorbis(VorbisError),
}

impl From<PcmError> for EncodeError {
    fn from(value: PcmError) -> Self {
        Self::Pcm(value)
    }
}

impl From<VorbisError> for EncodeError {
    fn from(value: VorbisError) -> Self {
        Self::Vorbis(value)
    }
}

impl Display for EncodeError {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self {
            Self::UnsupportedFormat { format } => {
                f.write_fmt(format_args!("encoding for {format} streams is not supported"))
            }
            Self::Pcm(_) => f.write_str("failed to encode PCM stream"),
            Self::Vorbis(_) => f.write_str("failed to encode Vorbis stream"),
        }
    }
}

impl Error for EncodeError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::UnsupportedFormat { format: _ } => None,
            Self::Pcm(e) => Some(e),
            Self::Vorbis(e) => Some(e),
        }
    }
}
