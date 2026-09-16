//! Experimental Navi8or core.
//!
//! The C application remains the behavioral reference. This crate deliberately
//! contains no viewer implementation.

pub mod app;
pub mod local;
pub mod provider;
pub mod terminal;
pub mod viewer_bridge;

pub use app::{AppState, Command, Pane, SortMode, ViewerRequest};
pub use local::LocalProvider;
pub use provider::{
    Capabilities, Entry, EntryKind, ListOptions, Location, Provider, ResourceId, ResourceMetadata,
};
