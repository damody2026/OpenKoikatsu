use crate::header::{error::HeaderError, AudioFormat, Header};
use crate::read::{ReadError, Reader};
use crate::stream::{LazyStream, Stream, StreamIntoIter};
use std::{
    error::Error,
    fmt::{Display, Formatter, Result as FmtResult},
    io::Read,
    num::NonZeroU32,
};
use tap::Pipe;

/// An FMOD sound bank.
///
/// The FMOD sound bank is a container format that can contain multiple streams/songs.
/// All streams have the same [`AudioFormat`].
/// Decoding and encoding is performed lazily.
///
/// # Examples
///
/// Reading from a slice of bytes:
///
/// ```
/// use fsbex::Bank;
/// use std::error::Error;
///
/// fn read_from_slice(bytes: &[u8]) -> Result<Bank<&[u8]>, Box<dyn Error>> {
///     let bank = Bank::new(bytes)?;
///     Ok(bank)
/// }
/// ```
///
/// Reading from a [`File`] using a [`Path`]:
///
/// ```
/// use fsbex::Bank;
/// use std::{error::Error, fs::File, io::BufReader, path::Path};
///
/// fn read_from_file<P: AsRef<Path>>(path: P) -> Result<Bank<BufReader<File>>, Box<dyn Error>> {
///     let file = File::open(path)?;
///     let reader = BufReader::new(file);
///     let bank = Bank::new(reader)?;
///     Ok(bank)
/// }
/// ```
///
/// [`AudioFormat`]: crate::header::AudioFormat
/// [`File`]: std::fs::File
/// [`Path`]: std::path::Path
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Bank<R: Read> {
    header: Header,
    read: Reader<R>,
}

impl<R: Read> Bank<R> {
    /// Creates a new [`Bank<R>`] by parsing from an I/O stream.
    ///
    /// Contents are parsed directly from the stream without being buffered in memory.
    /// When reading from a source where small, repeated read calls are inefficient, such as a [`File`],
    /// buffering with something like [`BufReader`] is recommended.
    ///
    /// # Errors
    ///
    /// This function returns an error if parsing of the sound bank's file header failed.
    /// See [`DecodeError`] for more information.
    ///
    /// [`File`]: std::fs::File
    /// [`BufReader`]: std::io::BufReader
    pub fn new(source: R) -> Result<Self, DecodeError> {
        let mut read = Reader::new(source);
        let header = Header::parse(&mut read)?;
        Ok(Self { header, read })
    }

    /// Returns the audio format of streams in the sound bank.
    ///
    /// See [`AudioFormat`] for the list of known formats.
    #[must_use]
    pub fn format(&self) -> AudioFormat {
        self.header.format
    }

    /// Returns the number of streams in the sound bank.
    #[must_use]
    pub fn num_streams(&self) -> NonZeroU32 {
        self.header
            .stream_info
            .len()
            .pipe(u32::try_from)
            .expect("stream count was already validated to be NonZeroU32")
            .try_into()
            .expect("stream count was already validated to be NonZeroU32")
    }

    /// Sequentially reads streams from the sound bank, consuming this [`Bank<R>`].
    /// Streams can be accessed within the function `f` as they are read.
    /// See [`LazyStream`] for more information.
    ///
    /// # Errors
    ///
    /// This function returns an error if:
    /// - an error was returned from `f`
    /// - the underlying reader failed to advance to the next stream
    ///
    /// See [`LazyStreamError`] for more information.
    pub fn read_streams<F, E>(mut self, f: F) -> Result<(), LazyStreamError<E>>
    where
        F: Fn(LazyStream<'_, R>) -> Result<(), E>,
    {
        for (info, index) in self.header.stream_info.iter().zip(0..) {
            let size = u32::from(info.size) as usize;
            let start_pos = self.read.position();

            f(LazyStream::new(
                index,
                self.header.format,
                self.header.flags,
                info,
                &mut self.read,
            ))
            .map_err(LazyStreamError::from_other(index))?;

            self.read
                .advance_to(start_pos + size)
                .map_err(LazyStreamError::from_read(index))?;
        }
        Ok(())
    }
}

impl<R: Read> From<Bank<R>> for StreamIntoIter<R> {
    fn from(value: Bank<R>) -> Self {
        Self::new(
            value.header.format,
            value.header.flags,
            value.header.stream_info,
            value.read,
        )
    }
}

impl<R: Read> IntoIterator for Bank<R> {
    type IntoIter = StreamIntoIter<R>;
    type Item = Stream;

    fn into_iter(self) -> Self::IntoIter {
        Self::IntoIter::from(self)
    }
}

/// Represents an error that can occur when parsing a sound bank.
///
/// This type is returned from [`Bank::new`] when file header parsing fails.
/// This can be caused by invalid data or the underlying reader encountering an I/O error.
#[derive(Debug)]
pub struct DecodeError {
    inner: Box<HeaderError>,
}

impl From<HeaderError> for DecodeError {
    fn from(value: HeaderError) -> Self {
        Self {
            inner: Box::new(value),
        }
    }
}

impl Display for DecodeError {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        self.inner.fmt(f)
    }
}

impl Error for DecodeError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        self.inner.source()
    }
}

/// Represents an error that can occur when reading sound bank streams with [`Bank::read_streams`].
#[derive(Debug)]
pub struct LazyStreamError<E> {
    index: u32,
    source: LazyStreamErrorSource<E>,
}

#[derive(Debug)]
enum LazyStreamErrorSource<E> {
    Read(ReadError),
    Other(E),
}

impl<E> LazyStreamError<E> {
    fn from_read(index: u32) -> impl FnOnce(ReadError) -> Self {
        move |source| Self {
            index,
            source: LazyStreamErrorSource::Read(source),
        }
    }

    fn from_other(index: u32) -> impl FnOnce(E) -> Self {
        move |source| Self {
            index,
            source: LazyStreamErrorSource::Other(source),
        }
    }

    /// Returns the index of the stream where the error occurred.
    pub fn index(&self) -> u32 {
        self.index
    }
}

impl<E> Display for LazyStreamError<E> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        f.write_fmt(format_args!("failed to process stream at index {}", self.index))
    }
}

impl<E: Error + 'static> Error for LazyStreamError<E> {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match &self.source {
            LazyStreamErrorSource::Read(e) => Some(e),
            LazyStreamErrorSource::Other(e) => Some(e),
        }
    }
}
