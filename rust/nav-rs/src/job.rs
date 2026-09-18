use crate::provider::{
    Capabilities, Entry, ListOptions, Location, Provider, ResourceId, WriteOptions,
};
use crate::transfer::{TransferOutcome, copy_stream};
use std::io;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, mpsc};
use std::thread::{self, JoinHandle};

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

#[derive(Clone)]
pub struct ListRequest {
    pub pane: usize,
    pub provider: Arc<dyn Provider>,
    pub location: Location,
    pub show_hidden: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum JobMessage {
    ListCompleted {
        id: JobId,
        pane: usize,
        location: Location,
        entries: Vec<Entry>,
    },
    ListFailed {
        id: JobId,
        pane: usize,
        error: String,
    },
    ListCancelled {
        id: JobId,
        pane: usize,
    },
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
    handle: JoinHandle<()>,
}

pub struct JobManager {
    next_id: u64,
    reliable_sender: mpsc::Sender<JobMessage>,
    reliable_receiver: mpsc::Receiver<JobMessage>,
    progress_sender: mpsc::SyncSender<JobMessage>,
    progress_receiver: mpsc::Receiver<JobMessage>,
    jobs: Vec<JobInfo>,
    active: Option<ActiveJob>,
    list_active: Vec<ActiveJob>,
}

impl Default for JobManager {
    fn default() -> Self {
        let (reliable_sender, reliable_receiver) = mpsc::channel();
        let (progress_sender, progress_receiver) = mpsc::sync_channel(1);
        Self {
            next_id: 1,
            reliable_sender,
            reliable_receiver,
            progress_sender,
            progress_receiver,
            jobs: Vec::new(),
            active: None,
            list_active: Vec::new(),
        }
    }
}

impl JobManager {
    pub fn start_list(&mut self, request: ListRequest) -> io::Result<JobId> {
        let id = JobId(self.next_id);
        self.next_id += 1;
        let cancelled = Arc::new(AtomicBool::new(false));
        let worker_cancelled = cancelled.clone();
        let sender = self.reliable_sender.clone();
        let handle = thread::Builder::new()
            .name(format!("nav-list-{}", id.0))
            .spawn(move || {
                let result = request.provider.list_cancellable(
                    &request.location,
                    &ListOptions {
                        show_hidden: request.show_hidden,
                    },
                    &worker_cancelled,
                );
                let message = if worker_cancelled.load(Ordering::Relaxed) {
                    JobMessage::ListCancelled {
                        id,
                        pane: request.pane,
                    }
                } else {
                    match result {
                        Ok(entries) => JobMessage::ListCompleted {
                            id,
                            pane: request.pane,
                            location: request.location,
                            entries,
                        },
                        Err(error) => JobMessage::ListFailed {
                            id,
                            pane: request.pane,
                            error: error.to_string(),
                        },
                    }
                };
                let _ = sender.send(message);
            })?;
        self.list_active.push(ActiveJob {
            id,
            cancelled,
            handle,
        });
        Ok(id)
    }

    pub fn cancel_list(&self, id: JobId) {
        if let Some(active) = self.list_active.iter().find(|active| active.id == id) {
            active.cancelled.store(true, Ordering::Relaxed);
        }
    }

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
        let reliable_sender = self.reliable_sender.clone();
        let progress_sender = self.progress_sender.clone();
        let handle = thread::Builder::new()
            .name(format!("nav-copy-{}", id.0))
            .spawn({
                let worker_cancelled = cancelled.clone();
                move || {
                    run_copy(
                        id,
                        request,
                        worker_cancelled,
                        reliable_sender,
                        progress_sender,
                    )
                }
            })
            .inspect_err(|error| {
                if let Some(info) = self.jobs.iter_mut().find(|info| info.id == id) {
                    info.state = JobState::Failed;
                    info.error = Some(error.to_string());
                }
            })?;
        self.active = Some(ActiveJob {
            id,
            cancelled,
            handle,
        });
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
        let mut messages = Vec::new();
        let pending: Vec<_> = self
            .progress_receiver
            .try_iter()
            .chain(self.reliable_receiver.try_iter())
            .collect();
        for message in pending {
            if message.is_list() || self.apply(&message) {
                if message.is_terminal() {
                    self.join_finished(message.id());
                }
                messages.push(message);
            }
        }
        messages
    }

    pub fn shutdown(&mut self) -> io::Result<()> {
        for active in &self.list_active {
            active.cancelled.store(true, Ordering::Relaxed);
        }
        let mut listing_panicked = false;
        for active in self.list_active.drain(..) {
            if active.handle.join().is_err() {
                listing_panicked = true;
            }
        }
        let Some(active) = self.active.take() else {
            return if listing_panicked {
                Err(io::Error::other("listing worker panicked during shutdown"))
            } else {
                Ok(())
            };
        };
        active.cancelled.store(true, Ordering::Relaxed);
        let id = active.id;
        let joined = active.handle.join();
        let pending: Vec<_> = self
            .progress_receiver
            .try_iter()
            .chain(self.reliable_receiver.try_iter())
            .collect();
        let mut terminal_received = false;
        for message in pending {
            terminal_received |= message.id() == id && message.is_terminal();
            self.apply(&message);
        }
        if joined.is_err() {
            let message = JobMessage::Failed {
                id,
                error: "copy worker panicked during shutdown".into(),
            };
            self.apply(&message);
            return Err(io::Error::other("copy worker panicked during shutdown"));
        }
        if !terminal_received {
            let message = JobMessage::Failed {
                id,
                error: "copy worker exited without a terminal state".into(),
            };
            self.apply(&message);
            return Err(io::Error::other(
                "copy worker exited without a terminal state",
            ));
        }
        if listing_panicked {
            Err(io::Error::other("listing worker panicked during shutdown"))
        } else {
            Ok(())
        }
    }

    fn join_finished(&mut self, id: JobId) {
        if let Some(index) = self.list_active.iter().position(|active| active.id == id) {
            let active = self.list_active.swap_remove(index);
            let _ = active.handle.join();
        }
        if self.active.as_ref().is_some_and(|active| active.id == id) {
            let active = self.active.take().expect("active job checked");
            let _ = active.handle.join();
        }
    }

    fn apply(&mut self, message: &JobMessage) -> bool {
        let id = message.id();
        let Some(info) = self.jobs.iter_mut().find(|info| info.id == id) else {
            return false;
        };
        if matches!(message, JobMessage::Progress { .. })
            && matches!(
                info.state,
                JobState::Completed | JobState::Failed | JobState::Cancelled
            )
        {
            return false;
        }
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
            JobMessage::Completed(_) => {
                info.state = JobState::Completed;
                if let Some(total) = info.total {
                    info.completed = total;
                }
            }
            JobMessage::Failed { error, .. } => {
                info.state = JobState::Failed;
                info.error = Some(error.clone());
            }
            JobMessage::Cancelled(_) => info.state = JobState::Cancelled,
            JobMessage::ListCompleted { .. }
            | JobMessage::ListFailed { .. }
            | JobMessage::ListCancelled { .. } => {
                unreachable!("listing messages bypass copy job state")
            }
        }
        true
    }
}

impl Drop for JobManager {
    fn drop(&mut self) {
        let _ = self.shutdown();
    }
}

impl JobMessage {
    fn is_list(&self) -> bool {
        matches!(
            self,
            Self::ListCompleted { .. } | Self::ListFailed { .. } | Self::ListCancelled { .. }
        )
    }
    fn id(&self) -> JobId {
        match self {
            Self::ListCompleted { id, .. }
            | Self::ListFailed { id, .. }
            | Self::ListCancelled { id, .. } => *id,
            Self::Started { id, .. }
            | Self::Progress { id, .. }
            | Self::Completed(id)
            | Self::Failed { id, .. }
            | Self::Cancelled(id) => *id,
        }
    }

    fn is_terminal(&self) -> bool {
        matches!(
            self,
            Self::Completed(_)
                | Self::Failed { .. }
                | Self::Cancelled(_)
                | Self::ListCompleted { .. }
                | Self::ListFailed { .. }
                | Self::ListCancelled { .. }
        )
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
    reliable_sender: mpsc::Sender<JobMessage>,
    progress_sender: mpsc::SyncSender<JobMessage>,
) {
    let terminal = run_copy_inner(id, &request, &cancelled, &reliable_sender, &progress_sender);
    let _ = reliable_sender.send(terminal);
}

fn run_copy_inner(
    id: JobId,
    request: &CopyRequest,
    cancelled: &AtomicBool,
    reliable_sender: &mpsc::Sender<JobMessage>,
    progress_sender: &mpsc::SyncSender<JobMessage>,
) -> JobMessage {
    let total = request
        .source_provider
        .stat_target(&request.source)
        .ok()
        .and_then(|metadata| metadata.size);
    let _ = reliable_sender.send(JobMessage::Started { id, total });
    let mut reader = match request.source_provider.open_read(&request.source) {
        Ok(reader) => reader,
        Err(error) => {
            return JobMessage::Failed {
                id,
                error: context(&format!("open source {}", request.source), error).to_string(),
            };
        }
    };
    let mut session = match request.destination_provider.open_write(
        &request.destination,
        WriteOptions {
            overwrite: false,
            total,
        },
    ) {
        Ok(session) => session,
        Err(error) => {
            return JobMessage::Failed {
                id,
                error: context(
                    &format!("create destination {}", request.destination),
                    error,
                )
                .to_string(),
            };
        }
    };
    let outcome = copy_stream(reader.as_mut(), session.as_mut(), cancelled, |completed| {
        let _ = progress_sender.try_send(JobMessage::Progress {
            id,
            completed,
            total,
        });
    });
    match outcome {
        Ok(TransferOutcome::Completed(_)) => match session.finish() {
            crate::provider::FinishOutcome::Committed => JobMessage::Completed(id),
            crate::provider::FinishOutcome::NotCommitted(error) => failure_after_abort(
                id,
                context(
                    &format!("finish destination {}", request.destination),
                    error,
                ),
                session.abort(),
            ),
            crate::provider::FinishOutcome::CommitUnknown(error) => JobMessage::Failed {
                id,
                error: format!(
                    "finish destination {}: {error}; remote commit state is unknown",
                    request.destination
                ),
            },
        },
        Ok(TransferOutcome::Cancelled(_)) => match session.abort() {
            Ok(()) => JobMessage::Cancelled(id),
            Err(error) => JobMessage::Failed {
                id,
                error: format!("copy cancelled but destination abort failed: {error}"),
            },
        },
        Err(error) => failure_after_abort(
            id,
            context(
                &format!("transfer {} to {}", request.source, request.destination),
                error,
            ),
            session.abort(),
        ),
    }
}

fn failure_after_abort(id: JobId, error: io::Error, abort: io::Result<()>) -> JobMessage {
    JobMessage::Failed {
        id,
        error: match abort {
            Ok(()) => error.to_string(),
            Err(abort_error) => format!("{error}; destination abort failed: {abort_error}"),
        },
    }
}

fn context(operation: &str, error: io::Error) -> io::Error {
    io::Error::new(error.kind(), format!("{operation}: {error}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::provider::{
        Entry, EntryKind, ListOptions, Location, LocationInput, ResourceMetadata, ResourceName,
        WriteSession,
    };
    use crate::{AppState, Command, LocalProvider, Pane, TRANSFER_BUFFER_SIZE};
    use std::fs;
    use std::io::{Cursor, Read, Write};
    use std::path::PathBuf;
    use std::sync::atomic::AtomicBool;
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

    struct RecordingDestination {
        finished: Arc<AtomicBool>,
        aborted: Arc<AtomicBool>,
        fail_write: bool,
        finish_unknown: bool,
    }

    struct RecordingSession {
        finished: Arc<AtomicBool>,
        aborted: Arc<AtomicBool>,
        fail_write: bool,
        finish_unknown: bool,
    }

    impl Write for RecordingSession {
        fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
            if self.fail_write {
                Err(io::Error::new(io::ErrorKind::WriteZero, "injected failure"))
            } else {
                Ok(buffer.len())
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    impl WriteSession for RecordingSession {
        fn finish(&mut self) -> crate::provider::FinishOutcome {
            self.finished.store(true, Ordering::Relaxed);
            if self.finish_unknown {
                crate::provider::FinishOutcome::CommitUnknown(io::Error::other("lost response"))
            } else {
                crate::provider::FinishOutcome::Committed
            }
        }

        fn abort(&mut self) -> io::Result<()> {
            self.aborted.store(true, Ordering::Relaxed);
            Ok(())
        }
    }

    impl Provider for RecordingDestination {
        fn as_any(&self) -> &dyn std::any::Any {
            self
        }

        fn scheme(&self) -> &'static str {
            "recording"
        }

        fn display_name(&self) -> &'static str {
            "Recording destination"
        }

        fn capabilities(&self) -> Capabilities {
            Capabilities::WRITE
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

        fn open_write(
            &self,
            _resource: &ResourceId,
            _options: WriteOptions,
        ) -> io::Result<Box<dyn WriteSession>> {
            Ok(Box::new(RecordingSession {
                finished: self.finished.clone(),
                aborted: self.aborted.clone(),
                fail_write: self.fail_write,
                finish_unknown: self.finish_unknown,
            }))
        }
    }

    fn recording_request(
        bytes: usize,
        fail_write: bool,
    ) -> (CopyRequest, Arc<AtomicBool>, Arc<AtomicBool>) {
        let finished = Arc::new(AtomicBool::new(false));
        let aborted = Arc::new(AtomicBool::new(false));
        (
            CopyRequest {
                source_provider: Arc::new(SlowSource {
                    bytes: vec![5; bytes],
                }),
                source: ResourceId::from_provider("source"),
                destination_provider: Arc::new(RecordingDestination {
                    finished: finished.clone(),
                    aborted: aborted.clone(),
                    fail_write,
                    finish_unknown: false,
                }),
                destination: ResourceId::from_provider("destination"),
                current_item: "recording.bin".into(),
            },
            finished,
            aborted,
        )
    }

    #[test]
    fn saturated_progress_queue_never_blocks_or_fails_the_worker() {
        let (request, finished, aborted) = recording_request(TRANSFER_BUFFER_SIZE * 100, false);
        let mut manager = JobManager::default();
        let id = manager.start_copy(request).unwrap();
        let deadline = Instant::now() + Duration::from_secs(3);
        while !manager
            .active
            .as_ref()
            .is_some_and(|active| active.handle.is_finished())
        {
            assert!(Instant::now() < deadline, "progress queue blocked worker");
            thread::sleep(Duration::from_millis(2));
        }

        let messages = manager.poll();
        assert!(messages.contains(&JobMessage::Completed(id)));
        assert!(
            messages
                .iter()
                .filter(|message| matches!(message, JobMessage::Progress { .. }))
                .count()
                <= 1
        );
        assert!(finished.load(Ordering::Relaxed));
        assert!(!aborted.load(Ordering::Relaxed));
        assert_eq!(manager.jobs()[0].state, JobState::Completed);
    }

    #[test]
    fn failed_copy_aborts_without_finishing() {
        let (request, finished, aborted) = recording_request(TRANSFER_BUFFER_SIZE, true);
        let mut manager = JobManager::default();
        manager.start_copy(request).unwrap();
        let messages = wait_for_terminal(&mut manager);
        assert!(
            messages
                .iter()
                .any(|message| matches!(message, JobMessage::Failed { .. }))
        );
        assert!(!finished.load(Ordering::Relaxed));
        assert!(aborted.load(Ordering::Relaxed));
    }

    #[test]
    fn unknown_commit_never_calls_abort() {
        let (mut request, finished, aborted) = recording_request(128, false);
        request.destination_provider = Arc::new(RecordingDestination {
            finished: finished.clone(),
            aborted: aborted.clone(),
            fail_write: false,
            finish_unknown: true,
        });
        let mut manager = JobManager::default();
        manager.start_copy(request).unwrap();
        let messages = wait_for_terminal(&mut manager);
        assert!(messages.iter().any(|message| matches!(message, JobMessage::Failed { error, .. } if error.contains("commit state is unknown"))));
        assert!(finished.load(Ordering::Relaxed));
        assert!(!aborted.load(Ordering::Relaxed));
    }

    #[test]
    fn shutdown_cancels_and_joins_the_active_worker() {
        let root = fixture();
        let destination = root.join("shutdown.bin");
        let mut manager = JobManager::default();
        manager
            .start_copy(CopyRequest {
                source_provider: Arc::new(SlowSource {
                    bytes: vec![9; TRANSFER_BUFFER_SIZE * 100],
                }),
                source: ResourceId::from_provider("source"),
                destination_provider: Arc::new(LocalProvider::new()),
                destination: ResourceId::from_provider(destination.as_os_str()),
                current_item: "shutdown.bin".into(),
            })
            .unwrap();
        thread::sleep(Duration::from_millis(5));

        manager.shutdown().unwrap();

        assert!(manager.active.is_none());
        assert_eq!(manager.jobs()[0].state, JobState::Cancelled);
        assert!(!destination.exists());
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn quitting_the_app_cancels_and_joins_a_copy() {
        let root = fixture();
        let destination = root.join("quitting.bin");
        let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
        let left = Pane::open(provider.clone(), root.clone().into_os_string(), false).unwrap();
        let right = Pane::open(provider, root.clone().into_os_string(), false).unwrap();
        let mut app = AppState::new([left, right]);
        app.jobs
            .start_copy(CopyRequest {
                source_provider: Arc::new(SlowSource {
                    bytes: vec![7; TRANSFER_BUFFER_SIZE * 100],
                }),
                source: ResourceId::from_provider("source"),
                destination_provider: Arc::new(LocalProvider::new()),
                destination: ResourceId::from_provider(destination.as_os_str()),
                current_item: "quitting.bin".into(),
            })
            .unwrap();

        app.dispatch(Command::Quit);

        assert!(!app.running);
        assert!(app.jobs.active_id().is_none());
        assert_eq!(app.jobs.jobs()[0].state, JobState::Cancelled);
        assert!(!destination.exists());
        fs::remove_dir_all(root).unwrap();
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
