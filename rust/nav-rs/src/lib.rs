//! Experimental Navi8or core.
//!
//! The C application remains the behavioral reference. This crate deliberately
//! contains no viewer implementation.

pub mod app;
pub mod http;
pub mod job;
pub mod keys;
pub mod local;
pub mod profile;
pub mod provider;
pub mod terminal;
pub mod transfer;
pub mod ui;
pub mod viewer_bridge;

pub use app::{AppState, Command, Pane, SortMode, ViewerRequest};
pub use http::{HttpAuth, HttpProvider};
pub use job::{CopyRequest, JobId, JobInfo, JobManager, JobMessage, JobOperation, JobState};
pub use local::LocalProvider;
pub use provider::{
    Capabilities, Entry, EntryKind, FinishOutcome, ListOptions, Location, LocationInput, Provider,
    ResourceId, ResourceMetadata, ResourceName, WriteOptions, WriteSession,
};
pub use transfer::{TRANSFER_BUFFER_SIZE, TransferOutcome, copy_stream};
