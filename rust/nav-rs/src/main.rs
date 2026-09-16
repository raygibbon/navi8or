use nav_rs::{AppState, LocalProvider, Pane, Provider, terminal};
use std::env;
use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::Arc;

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err((code, message)) => {
            eprintln!("nav-rs: {message}");
            ExitCode::from(code)
        }
    }
}

fn run() -> Result<(), (u8, String)> {
    let mut locations = Vec::new();
    let mut options = true;
    for argument in env::args().skip(1) {
        if options && argument == "--" {
            options = false;
        } else if options && argument == "--help" {
            println!(
                "Usage: nav-rs [left-directory] [right-directory]\n       nav-rs --version\nExperimental Rust local two-pane Navi8or core."
            );
            return Ok(());
        } else if options && argument == "--version" {
            println!("Navi8or Rust core {}", env!("CARGO_PKG_VERSION"));
            return Ok(());
        } else if options && argument.starts_with('-') {
            return Err((2, format!("unknown option: {argument}")));
        } else {
            locations.push(PathBuf::from(argument));
        }
    }
    if locations.len() > 2 {
        return Err((2, "too many directories".into()));
    }

    let current = env::current_dir().map_err(|error| (1, error.to_string()))?;
    let left = locations
        .first()
        .cloned()
        .unwrap_or_else(|| current.clone());
    let right = locations.get(1).cloned().unwrap_or_else(|| current.clone());
    let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
    let left = Pane::open(provider.clone(), &left, false)
        .map_err(|error| (1, format!("{}: {error}", left.display())))?;
    let right = Pane::open(provider, &right, false)
        .map_err(|error| (1, format!("{}: {error}", right.display())))?;
    let mut app = AppState::new([left, right]);
    terminal::run(&mut app).map_err(|error| (1, format!("terminal: {error}")))
}
