use std::{
    cmp::min,
    error::Error,
    fmt::{Display, Formatter, Result as FmtResult},
    io::{BufRead, Error as IoError, ErrorKind, Read},
    num::NonZeroUsize,
};

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Reader<R: Read> {
    inner: R,
    position: usize,
}

impl<R: Read> Reader<R> {
    pub(crate) fn new(reader: R) -> Self {
        Self {
            inner: reader,
            position: 0,
        }
    }

    fn read_to_array<const LEN: usize>(&mut self, buf: &mut [u8; LEN]) -> ReadResult<()> {
        match self.inner.read(buf) {
            Ok(n) => {
                self.position += n;

                if n == LEN {
                    Ok(())
                } else {
                    Err(self.to_error(ReadErrorKind::Incomplete(Needed::Size(
                        NonZeroUsize::new(LEN - n).expect("n is guaranteed to not equal LEN"),
                    ))))
                }
            }
            Err(e) => match e.kind() {
                // this I/O error is non-fatal, so reading is retried
                ErrorKind::Interrupted => self.read_to_array(buf),
                ErrorKind::UnexpectedEof => {
                    Err(self.to_error(ReadErrorKind::Incomplete(Needed::Unknown)))
                }
                _ => Err(self.to_error_with_source(ReadErrorKind::Failure, e)),
            },
        }
    }

    fn read_to_slice(&mut self, buf: &mut [u8]) -> ReadResult<()> {
        match self.inner.read(buf) {
            Ok(n) => {
                self.position += n;
                let buf_len = buf.len();

                if n == buf_len {
                    Ok(())
                } else {
                    Err(self.to_error(ReadErrorKind::Incomplete(Needed::Size(
                        NonZeroUsize::new(buf_len - n)
                            .expect("n is guaranteed to not equal buf_len"),
                    ))))
                }
            }
            Err(e) => match e.kind() {
                // this I/O error is non-fatal, so reading is retried
                ErrorKind::Interrupted => self.read_to_slice(buf),
                ErrorKind::UnexpectedEof => {
                    Err(self.to_error(ReadErrorKind::Incomplete(Needed::Unknown)))
                }
                _ => Err(self.to_error_with_source(ReadErrorKind::Failure, e)),
            },
        }
    }

    pub(crate) fn position(&self) -> usize {
        self.position
    }

    pub(crate) fn take_const<const LEN: usize>(&mut self) -> ReadResult<[u8; LEN]> {
        let mut buf = [0; LEN];
        Self::read_to_array(self, &mut buf)?;
        Ok(buf)
    }

    pub(crate) fn take(&mut self, len: usize) -> ReadResult<Vec<u8>> {
        let mut buf = vec![0; len];
        Self::read_to_slice(self, &mut buf)?;
        Ok(buf)
    }

    pub(crate) fn skip(&mut self, amount: usize) -> ReadResult<()> {
        let mut buf = vec![0; amount];
        Self::read_to_slice(self, buf.as_mut_slice())
    }

    pub(crate) fn advance_to(&mut self, position: usize) -> ReadResult<()> {
        self.skip(position - self.position)
    }

    // `std::io::Take` isn't used here because constructing it requires taking ownership of the reader
    pub(crate) fn limit(&mut self, limit: usize) -> CappedReader<'_, R> {
        CappedReader {
            reader: self,
            limit,
        }
    }

    pub(crate) fn u8(&mut self) -> ReadResult<u8> {
        let mut buf = [0; 1];
        Self::read_to_array(self, &mut buf)?;
        Ok(buf[0])
    }

    pub(crate) fn le_u16(&mut self) -> ReadResult<u16> {
        let mut buf = [0; 2];
        Self::read_to_array(self, &mut buf)?;
        Ok(u16::from_le_bytes(buf))
    }

    pub(crate) fn le_u32(&mut self) -> ReadResult<u32> {
        let mut buf = [0; 4];
        Self::read_to_array(self, &mut buf)?;
        Ok(u32::from_le_bytes(buf))
    }

    pub(crate) fn le_u64(&mut self) -> ReadResult<u64> {
        let mut buf = [0; 8];
        Self::read_to_array(self, &mut buf)?;
        Ok(u64::from_le_bytes(buf))
    }

    pub(crate) fn be_i16(&mut self) -> ReadResult<i16> {
        let mut buf = [0; 2];
        Self::read_to_array(self, &mut buf)?;
        Ok(i16::from_be_bytes(buf))
    }
}

// essentially `std::io::Take` but with a mutable reference to a reader instead of owning it
pub(crate) struct CappedReader<'reader, R: Read> {
    reader: &'reader mut Reader<R>,
    limit: usize,
}

impl<'reader, R: Read> Read for CappedReader<'reader, R> {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, IoError> {
        if self.limit == 0 {
            return Ok(0);
        }

        let max = min(buf.len(), self.limit);
        let n = self.reader.inner.read(&mut buf[..max])?;
        self.reader.position += n;
        self.limit -= n;
        Ok(n)
    }
}

impl<'reader, R: BufRead> BufRead for CappedReader<'reader, R> {
    fn fill_buf(&mut self) -> Result<&[u8], IoError> {
        if self.limit == 0 {
            return Ok(&[]);
        }

        let buf = self.reader.inner.fill_buf()?;
        let cap = min(buf.len(), self.limit);
        Ok(&buf[..cap])
    }

    fn consume(&mut self, amt: usize) {
        let amt = min(amt, self.limit);
        self.limit -= amt;
        self.reader.inner.consume(amt);
    }
}

type ReadResult<T> = Result<T, ReadError>;

#[derive(Debug)]
pub(crate) struct ReadError {
    position: usize,
    kind: ReadErrorKind,
    source: Option<IoError>,
}

#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Copy, PartialEq, Eq))]
pub(crate) enum ReadErrorKind {
    Failure,
    Incomplete(Needed),
}

#[derive(Debug)]
#[cfg_attr(test, derive(Clone, Copy, PartialEq, Eq))]
pub(crate) enum Needed {
    Size(NonZeroUsize),
    Unknown,
}

impl<R: Read> Reader<R> {
    fn to_error(&self, kind: ReadErrorKind) -> ReadError {
        ReadError {
            position: self.position,
            kind,
            source: None,
        }
    }

    fn to_error_with_source(&self, kind: ReadErrorKind, source: IoError) -> ReadError {
        ReadError {
            position: self.position,
            kind,
            source: Some(source),
        }
    }
}

#[cfg(test)]
impl ReadError {
    fn is_kind(&self, kind: ReadErrorKind) -> bool {
        self.kind == kind
    }
}

impl Display for ReadError {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match &self.kind {
            ReadErrorKind::Failure => f.write_str("failed to read data due to I/O error"),
            ReadErrorKind::Incomplete(needed) => match needed {
                Needed::Size(size) => {
                    f.write_fmt(format_args!("incomplete data: needed {size} more bytes to read"))
                }
                Needed::Unknown => f.write_str("incomplete data"),
            },
        }?;

        f.write_fmt(format_args!(" - byte position {}", self.position))
    }
}

impl Error for ReadError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match &self.source {
            Some(e) => Some(e),
            None => None,
        }
    }
}

#[cfg(test)]
mod test {
    use super::{Needed, ReadErrorKind, ReadResult, Reader};
    use std::{
        io::{Error as IoError, ErrorKind, Read, Result as IoResult},
        num::NonZeroUsize,
    };

    #[test]
    fn take_bytes() {
        let data = b"abc123";
        let mut reader = Reader::new(data.as_slice());

        assert_eq!(reader.take_const().unwrap(), [97]);
        assert_eq!(reader.take_const().unwrap(), [98, 99]);
        assert_eq!(reader.take_const().unwrap(), [49, 50, 51]);
        assert_eq!(reader.take_const().unwrap(), []);
        assert!(reader
            .take_const::<1>()
            .is_err_and(|e| e
                .is_kind(ReadErrorKind::Incomplete(Needed::Size(NonZeroUsize::new(1).unwrap())))));
    }

    #[test]
    fn skip_bytes() {
        let data = b"abc123";
        let mut reader = Reader::new(data.as_slice());

        assert!(reader.skip(1).is_ok());
        assert!(reader.skip(2).is_ok());
        assert!(reader.skip(3).is_ok());
        assert!(reader.skip(0).is_ok());
        assert!(reader
            .skip(1)
            .is_err_and(|e| e
                .is_kind(ReadErrorKind::Incomplete(Needed::Size(NonZeroUsize::new(1).unwrap())))));
    }

    #[test]
    fn advance_to_position() {
        let data = b"abc123";
        let mut reader = Reader::new(data.as_slice());

        assert!(reader.advance_to(0).is_ok());
        assert_eq!(reader.position(), 0);

        assert!(reader.advance_to(2).is_ok());
        assert_eq!(reader.position(), 2);

        assert!(reader.advance_to(6).is_ok());
        assert_eq!(reader.position(), 6);

        assert!(reader
            .advance_to(10)
            .is_err_and(|e| e
                .is_kind(ReadErrorKind::Incomplete(Needed::Size(NonZeroUsize::new(4).unwrap())))));
    }

    #[test]
    fn parse_single_number() {
        let data = b"\x00\x00\x00\x00\x00\x00";
        let mut reader = Reader::new(data.as_slice());

        assert_eq!(reader.le_u32().unwrap(), 0);
    }

    #[test]
    fn parse_multiple_numbers() {
        let data = b"\x11\x00\x00\x00\x34\x12\x00\x00\x66\x66\x66\x66\xFF\xFF\xFF\xFF";
        let mut reader = Reader::new(data.as_slice());

        assert_eq!(reader.le_u32().unwrap(), 17);
        assert_eq!(reader.le_u32().unwrap(), 4660);
        assert_eq!(reader.le_u32().unwrap(), 1_717_986_918);
        assert_eq!(reader.le_u32().unwrap(), u32::MAX);
    }

    #[test]
    fn parse_multiple_number_types() {
        let data = b"\x11\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x22";
        let mut reader = Reader::new(data.as_slice());

        assert_eq!(reader.le_u32().unwrap(), 17);
        assert_eq!(reader.u8().unwrap(), 0);
        assert_eq!(reader.le_u64().unwrap(), 1);
        assert_eq!(reader.u8().unwrap(), 34);
    }

    #[test]
    fn handle_incomplete_data() {
        let data = b"\x00\x00";
        let mut reader = Reader::new(data.as_slice());

        assert!(reader
            .le_u32()
            .is_err_and(|e| e
                .is_kind(ReadErrorKind::Incomplete(Needed::Size(NonZeroUsize::new(2).unwrap())))));
    }

    impl<R: Read> Reader<R> {
        fn unit(&mut self) -> ReadResult<()> {
            let mut buf = [0; 0];
            Self::read_to_array(self, &mut buf)
        }
    }

    struct InterruptReader(usize);

    impl Read for InterruptReader {
        fn read(&mut self, _buf: &mut [u8]) -> IoResult<usize> {
            if self.0 < 3 {
                self.0 += 1;
                Err(IoError::from(ErrorKind::Interrupted))
            } else {
                Ok(0)
            }
        }
    }

    #[test]
    fn interrupt_reader_works() {
        let mut buf = [0; 0];
        let mut reader = InterruptReader(0);

        assert!(reader
            .read(&mut buf)
            .is_err_and(|e| e.kind() == ErrorKind::Interrupted));
        assert!(reader
            .read(&mut buf)
            .is_err_and(|e| e.kind() == ErrorKind::Interrupted));
        assert!(reader
            .read(&mut buf)
            .is_err_and(|e| e.kind() == ErrorKind::Interrupted));
        assert!(matches!(reader.read(&mut buf), Ok(0)));
    }

    #[test]
    fn handle_interrupted_io() {
        let mut reader = Reader::new(InterruptReader(0));

        assert!(reader.unit().is_ok());
    }

    struct EofReader;

    impl Read for EofReader {
        fn read(&mut self, _buf: &mut [u8]) -> IoResult<usize> {
            Err(IoError::from(ErrorKind::UnexpectedEof))
        }
    }

    #[test]
    fn eof_reader_works() {
        let mut buf = [0; 0];
        let mut reader = EofReader;

        assert!(reader
            .read(&mut buf)
            .is_err_and(|e| e.kind() == ErrorKind::UnexpectedEof));
    }

    #[test]
    fn handle_unexpected_eof() {
        let mut reader = Reader::new(EofReader);

        assert!(reader
            .unit()
            .is_err_and(|e| e.is_kind(ReadErrorKind::Incomplete(Needed::Unknown))));
    }

    struct UnsupportedReader;

    impl Read for UnsupportedReader {
        fn read(&mut self, _buf: &mut [u8]) -> IoResult<usize> {
            Err(IoError::from(ErrorKind::Unsupported))
        }
    }

    #[test]
    fn unsupported_reader_works() {
        let mut buf = [0; 0];
        let mut reader = UnsupportedReader;

        assert!(reader
            .read(&mut buf)
            .is_err_and(|e| e.kind() == ErrorKind::Unsupported));
    }

    #[test]
    fn handle_misc_io_error() {
        let mut reader = Reader::new(UnsupportedReader);

        assert!(reader.unit().is_err_and(|e| e.is_kind(ReadErrorKind::Failure)));
    }

    #[test]
    fn capped_reader_works() {
        let data = b"abcd1234";
        let mut reader = Reader::new(data.as_slice());
        let mut reader = reader.limit(6);

        assert!(reader.read_exact(&mut []).is_ok());
        assert!(reader.read_exact(&mut [0]).is_ok());
        assert!(reader.read_exact(&mut [0, 0]).is_ok());
        assert!(reader.read_exact(&mut [0, 0, 0]).is_ok());
        assert!(reader.read_exact(&mut []).is_ok());
        assert!(reader
            .read_exact(&mut [0])
            .is_err_and(|e| e.kind() == ErrorKind::UnexpectedEof));
    }
}
