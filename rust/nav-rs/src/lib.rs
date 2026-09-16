//! Experimental Navi8or core.
//!
//! The C application remains the behavioral reference. This crate deliberately
//! contains no viewer implementation.

pub mod app;
pub mod local;
pub mod provider;
pub mod terminal;

pub use app::{AppState, Command, Pane};
pub use local::LocalProvider;
pub use provider::{Capabilities, Entry, EntryKind, Provider};
