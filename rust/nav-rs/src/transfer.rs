use std::io::{self, Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};

pub const TRANSFER_BUFFER_SIZE: usize = 128 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransferOutcome {
    Completed(u64),
    Cancelled(u64),
}

pub fn copy_stream(
    reader: &mut dyn Read,
    writer: &mut dyn Write,
    cancelled: &AtomicBool,
    mut progress: impl FnMut(u64),
) -> io::Result<TransferOutcome> {
    let mut buffer = vec![0; TRANSFER_BUFFER_SIZE];
    let mut completed = 0_u64;
    loop {
        if cancelled.load(Ordering::Relaxed) {
            return Ok(TransferOutcome::Cancelled(completed));
        }
        let count = reader.read(&mut buffer)?;
        if count == 0 {
            writer.flush()?;
            return Ok(TransferOutcome::Completed(completed));
        }
        writer.write_all(&buffer[..count])?;
        completed += count as u64;
        progress(completed);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn copies_files_larger_than_the_internal_buffer_with_monotonic_progress() {
        let input: Vec<_> = (0..TRANSFER_BUFFER_SIZE * 3 + 17)
            .map(|index| (index % 251) as u8)
            .collect();
        let mut reader = Cursor::new(&input);
        let mut output = Vec::new();
        let cancelled = AtomicBool::new(false);
        let mut updates = Vec::new();
        let outcome = copy_stream(&mut reader, &mut output, &cancelled, |done| {
            updates.push(done)
        })
        .unwrap();

        assert_eq!(outcome, TransferOutcome::Completed(input.len() as u64));
        assert_eq!(output, input);
        assert!(updates.windows(2).all(|pair| pair[0] < pair[1]));
        assert_eq!(updates.last(), Some(&(input.len() as u64)));
    }

    #[test]
    fn cancellation_is_checked_between_chunks() {
        let input = vec![7; TRANSFER_BUFFER_SIZE * 2];
        let mut reader = Cursor::new(input);
        let mut output = Vec::new();
        let cancelled = AtomicBool::new(false);
        let outcome = copy_stream(&mut reader, &mut output, &cancelled, |_| {
            cancelled.store(true, Ordering::Relaxed);
        })
        .unwrap();

        assert_eq!(
            outcome,
            TransferOutcome::Cancelled(TRANSFER_BUFFER_SIZE as u64)
        );
        assert_eq!(output.len(), TRANSFER_BUFFER_SIZE);
    }
}
