use crate::provider::{Capabilities, Provider, ResourceId, WriteOptions};
use crate::transfer::{TransferOutcome, copy_stream};
use std::io;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, mpsc};
use std::thread;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct JobId(u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum JobOperation {
    Copy,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum JobState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
}

#[derive(Clone, Debug)]
pub struct JobInfo {
    pub id: JobId,
    pub operation: JobOperation,
    pub source: ResourceId,
    pub destination: ResourceId,
    pub state: JobState,
    pub completed: u64,
    pub total: Option<u64>,
    pub current_item: String,
    pub error: Option<String>,
}

#[derive(Clone)]
pub struct CopyRequest {
    pub source_provider: Arc<dyn Provider>,
    pub source: ResourceId,
    pub destination_provider: Arc<dyn Provider>,
    pub destination: ResourceId,
    pub current_item: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum JobMessage {
    Started {
        id: JobId,
        total: Option<u64>,
    },
    Progress {
        id: JobId,
        completed: u64,
        total: Option<u64>,
    },
    Completed(JobId),
    Failed {
        id: JobId,
        error: String,
    },
    Cancelled(JobId),
}

struct ActiveJob {
    id: JobId,
    cancelled: Arc<AtomicBool>,
}

pub struct JobManager {
    next_id: u64,
    sender: mpsc::SyncSender<JobMessage>,
    receiver: mpsc::Receiver<JobMessage>,
    jobs: Vec<JobInfo>,
    active: Option<ActiveJob>,
}

impl Default for JobManager {
    fn default() -> Self {
        let (sender, receiver) = mpsc::sync_channel(64);
        Self {
            next_id: 1,
            sender,
            receiver,
            jobs: Vec::new(),
            active: None,
        }
    }
}

impl JobManager {
    pub fn start_copy(&mut self, request: CopyRequest) -> io::Result<JobId> {
        if self.active.is_some() {
            return Err(io::Error::new(
                io::ErrorKind::WouldBlock,
                "another transfer is already active",
            ));
        }
        require_capability(
            request.source_provider.as_ref(),
            Capabilities::READ,
            "source cannot be read",
        )?;
        require_capability(
            request.destination_provider.as_ref(),
            Capabilities::WRITE,
            "destination is read-only",
        )?;
        require_capability(
            request.destination_provider.as_ref(),
            Capabilities::DELETE,
            "destination cannot remove an incomplete transfer",
        )?;

        let id = JobId(self.next_id);
        self.next_id += 1;
        let cancelled = Arc::new(AtomicBool::new(false));
        self.jobs.push(JobInfo {
            id,
            operation: JobOperation::Copy,
            source: request.source.clone(),
            destination: request.destination.clone(),
            state: JobState::Pending,
            completed: 0,
            total: None,
            current_item: request.current_item.clone(),
            error: None,
        });
        self.active = Some(ActiveJob {
            id,
            cancelled: cancelled.clone(),
        });
        let sender = self.sender.clone();
        thread::Builder::new()
            .name(format!("nav-copy-{}", id.0))
            .spawn(move || run_copy(id, request, cancelled, sender))
            .inspect_err(|error| {
                self.active = None;
                if let Some(info) = self.jobs.iter_mut().find(|info| info.id == id) {
                    info.state = JobState::Failed;
                    info.error = Some(error.to_string());
                }
            })?;
        Ok(id)
    }

    pub fn cancel_active(&self) -> bool {
        if let Some(active) = &self.active {
            active.cancelled.store(true, Ordering::Relaxed);
            true
        } else {
            false
        }
    }

    pub fn active_id(&self) -> Option<JobId> {
        self.active.as_ref().map(|active| active.id)
    }

    pub fn jobs(&self) -> &[JobInfo] {
        &self.jobs
    }

    pub fn poll(&mut self) -> Vec<JobMessage> {
        let messages: Vec<_> = self.receiver.try_iter().collect();
        for message in &messages {
            self.apply(message);
        }
        messages
    }

    fn apply(&mut self, message: &JobMessage) {
        let id = match message {
            JobMessage::Started { id, .. }
            | JobMessage::Progress { id, .. }
            | JobMessage::Completed(id)
            | JobMessage::Failed { id, .. }
            | JobMessage::Cancelled(id) => *id,
        };
        let Some(info) = self.jobs.iter_mut().find(|info| info.id == id) else {
            return;
        };
        match message {
            JobMessage::Started { total, .. } => {
                info.state = JobState::Running;
                info.total = *total;
            }
            JobMessage::Progress {
                completed, total, ..
            } => {
                info.state = JobState::Running;
                info.completed = *completed;
                info.total = *total;
            }
            JobMessage::Completed(_) => info.state = JobState::Completed,
            JobMessage::Failed { error, .. } => {
                info.state = JobState::Failed;
                info.error = Some(error.clone());
            }
            JobMessage::Cancelled(_) => info.state = JobState::Cancelled,
        }
        if matches!(
            message,
            JobMessage::Completed(_) | JobMessage::Failed { .. } | JobMessage::Cancelled(_)
        ) {
            self.active = None;
        }
    }
}

fn require_capability(
    provider: &dyn Provider,
    capability: Capabilities,
    message: &str,
) -> io::Result<()> {
    if provider.capabilities().contains(capability) {
        Ok(())
    } else {
        Err(io::Error::new(io::ErrorKind::Unsupported, message))
    }
}

fn run_copy(
    id: JobId,
    request: CopyRequest,
    cancelled: Arc<AtomicBool>,
    sender: mpsc::SyncSender<JobMessage>,
) {
    let result = run_copy_inner(id, &request, &cancelled, &sender);
    let terminal = match result {
        Ok(TransferOutcome::Completed(_)) => JobMessage::Completed(id),
        Ok(TransferOutcome::Cancelled(_)) => match request
            .destination_provider
            .delete(&request.destination)
        {
            Ok(()) => JobMessage::Cancelled(id),
            Err(error) if error.kind() == io::ErrorKind::NotFound => JobMessage::Cancelled(id),
            Err(error) => JobMessage::Failed {
                id,
                error: format!("copy cancelled but partial destination removal failed: {error}"),
            },
        },
        Err((error, destination_created)) => {
            let cleanup = destination_created
                .then(|| request.destination_provider.delete(&request.destination))
                .transpose();
            JobMessage::Failed {
                id,
                error: match cleanup {
                    Ok(_) => error.to_string(),
                    Err(cleanup_error) => {
                        format!("{error}; partial destination removal failed: {cleanup_error}")
                    }
                },
            }
        }
    };
    let _ = sender.send(terminal);
}

fn run_copy_inner(
    id: JobId,
    request: &CopyRequest,
    cancelled: &AtomicBool,
    sender: &mpsc::SyncSender<JobMessage>,
) -> Result<TransferOutcome, (io::Error, bool)> {
    let total = request
        .source_provider
        .stat_target(&request.source)
        .ok()
        .and_then(|metadata| metadata.size);
    let _ = sender.send(JobMessage::Started { id, total });
    let mut reader = request
        .source_provider
        .open_read(&request.source)
        .map_err(|error| {
            (
                context(&format!("open source {}", request.source), error),
                false,
            )
        })?;
    let mut writer = request
        .destination_provider
        .open_write(
            &request.destination,
            WriteOptions {
                overwrite: false,
                total,
            },
        )
        .map_err(|error| {
            (
                context(
                    &format!("create destination {}", request.destination),
                    error,
                ),
                false,
            )
        })?;
    copy_stream(reader.as_mut(), writer.as_mut(), cancelled, |completed| {
        let _ = sender.send(JobMessage::Progress {
            id,
            completed,
            total,
        });
    })
    .map_err(|error| {
        (
            context(
                &format!("transfer {} to {}", request.source, request.destination),
                error,
            ),
            true,
        )
    })
}

fn context(operation: &str, error: io::Error) -> io::Error {
    io::Error::new(error.kind(), format!("{operation}: {error}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::provider::{
        Entry, EntryKind, ListOptions, Location, LocationInput, ResourceMetadata, ResourceName,
    };
    use crate::{LocalProvider, TRANSFER_BUFFER_SIZE};
    use std::fs;
    use std::io::{Cursor, Read};
    use std::path::PathBuf;
    use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

    fn fixture() -> PathBuf {
        let suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!("nav-rs-job-{}-{suffix}", std::process::id()));
        fs::create_dir(&path).unwrap();
        path
    }

    fn local_request(source: PathBuf, destination: PathBuf) -> CopyRequest {
        CopyRequest {
            source_provider: Arc::new(LocalProvider::new()),
            source: ResourceId::from_provider(source.into_os_string()),
            destination_provider: Arc::new(LocalProvider::new()),
            destination: ResourceId::from_provider(destination.into_os_string()),
            current_item: "fixture.bin".into(),
        }
    }

    fn wait_for_terminal(manager: &mut JobManager) -> Vec<JobMessage> {
        let deadline = Instant::now() + Duration::from_secs(3);
        let mut messages = Vec::new();
        while Instant::now() < deadline {
            let batch = manager.poll();
            let terminal = batch.iter().any(|message| {
                matches!(
                    message,
                    JobMessage::Completed(_) | JobMessage::Failed { .. } | JobMessage::Cancelled(_)
                )
            });
            messages.extend(batch);
            if terminal {
                return messages;
            }
            thread::sleep(Duration::from_millis(5));
        }
        panic!("job did not finish");
    }

    #[test]
    fn local_copy_reports_monotonic_progress_and_completion() {
        let root = fixture();
        let source = root.join("source.bin");
        let destination = root.join("destination.bin");
        let bytes: Vec<_> = (0..TRANSFER_BUFFER_SIZE * 2 + 31)
            .map(|index| (index % 239) as u8)
            .collect();
        fs::write(&source, &bytes).unwrap();
        let mut manager = JobManager::default();
        let id = manager
            .start_copy(local_request(source, destination.clone()))
            .unwrap();
        let messages = wait_for_terminal(&mut manager);
        let progress: Vec<_> = messages
            .iter()
            .filter_map(|message| match message {
                JobMessage::Progress { completed, .. } => Some(*completed),
                _ => None,
            })
            .collect();
        assert!(progress.windows(2).all(|pair| pair[0] < pair[1]));
        assert!(messages.contains(&JobMessage::Completed(id)));
        assert_eq!(fs::read(destination).unwrap(), bytes);
        assert_eq!(manager.jobs()[0].state, JobState::Completed);
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn existing_destination_is_reported_as_failure_and_is_not_removed() {
        let root = fixture();
        let source = root.join("source.bin");
        let destination = root.join("destination.bin");
        fs::write(&source, b"new").unwrap();
        fs::write(&destination, b"existing").unwrap();
        let mut manager = JobManager::default();
        manager
            .start_copy(local_request(source, destination.clone()))
            .unwrap();
        let messages = wait_for_terminal(&mut manager);
        assert!(
            messages
                .iter()
                .any(|message| matches!(message, JobMessage::Failed { .. }))
        );
        assert_eq!(fs::read(destination).unwrap(), b"existing");
        fs::remove_dir_all(root).unwrap();
    }

    struct SlowSource {
        bytes: Vec<u8>,
    }

    struct SlowReader {
        cursor: Cursor<Vec<u8>>,
    }

    impl Read for SlowReader {
        fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
            thread::sleep(Duration::from_millis(2));
            self.cursor.read(buffer)
        }
    }

    impl Provider for SlowSource {
        fn as_any(&self) -> &dyn std::any::Any {
            self
        }

        fn scheme(&self) -> &'static str {
            "slow"
        }

        fn display_name(&self) -> &'static str {
            "Slow test source"
        }

        fn capabilities(&self) -> Capabilities {
            Capabilities::READ.union(Capabilities::STAT)
        }

        fn resolve(&self, _input: &LocationInput) -> io::Result<Location> {
            Err(io::ErrorKind::Unsupported.into())
        }

        fn location(&self, _resource: &ResourceId) -> io::Result<Location> {
            Err(io::ErrorKind::Unsupported.into())
        }

        fn parent(&self, _location: &Location) -> io::Result<Option<Location>> {
            Ok(None)
        }

        fn child(&self, _location: &Location, _name: &ResourceName) -> io::Result<Location> {
            Err(io::ErrorKind::Unsupported.into())
        }

        fn list(&self, _location: &Location, _options: &ListOptions) -> io::Result<Vec<Entry>> {
            Ok(Vec::new())
        }

        fn stat(&self, _resource: &ResourceId) -> io::Result<ResourceMetadata> {
            Ok(ResourceMetadata {
                kind: EntryKind::File,
                size: Some(self.bytes.len() as u64),
                modified: None,
            })
        }

        fn open_read(&self, _resource: &ResourceId) -> io::Result<Box<dyn Read + Send>> {
            Ok(Box::new(SlowReader {
                cursor: Cursor::new(self.bytes.clone()),
            }))
        }
    }

    #[test]
    fn cancellation_removes_the_partial_destination() {
        let root = fixture();
        let destination = root.join("partial.bin");
        let source: Arc<dyn Provider> = Arc::new(SlowSource {
            bytes: vec![3; TRANSFER_BUFFER_SIZE * 20],
        });
        let mut manager = JobManager::default();
        manager
            .start_copy(CopyRequest {
                source_provider: source,
                source: ResourceId::from_provider("source"),
                destination_provider: Arc::new(LocalProvider::new()),
                destination: ResourceId::from_provider(destination.as_os_str()),
                current_item: "slow.bin".into(),
            })
            .unwrap();

        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            let messages = manager.poll();
            if messages
                .iter()
                .any(|message| matches!(message, JobMessage::Progress { .. }))
            {
                assert!(manager.cancel_active());
                break;
            }
            assert!(Instant::now() < deadline, "copy never reported progress");
            thread::sleep(Duration::from_millis(2));
        }
        let messages = wait_for_terminal(&mut manager);
        assert!(
            messages
                .iter()
                .any(|message| matches!(message, JobMessage::Cancelled(_)))
        );
        assert!(!destination.exists());
        assert_eq!(manager.jobs()[0].state, JobState::Cancelled);
        fs::remove_dir_all(root).unwrap();
    }
}
