use nav_rs::{AppState, LocalProvider, Pane, Provider, terminal};
use std::env;
use std::ffi::OsString;
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
    let locations = match parse_arguments(env::args_os().skip(1))? {
        StartupAction::Run(locations) => locations,
        StartupAction::Help => {
            println!(
                "Usage: nav-rs [left-directory] [right-directory]\n       nav-rs --version\nExperimental Rust local two-pane Navi8or core."
            );
            return Ok(());
        }
        StartupAction::Version => {
            println!("Navi8or Rust core {}", env!("CARGO_PKG_VERSION"));
            return Ok(());
        }
    };

    let current = env::current_dir().map_err(|error| (1, error.to_string()))?;
    let left = locations
        .first()
        .cloned()
        .unwrap_or_else(|| current.as_os_str().to_os_string());
    let right = locations
        .get(1)
        .cloned()
        .unwrap_or_else(|| current.into_os_string());
    let provider: Arc<dyn Provider> = Arc::new(LocalProvider::new());
    let left_display = left.to_string_lossy().into_owned();
    let left = Pane::open(provider.clone(), left.clone(), false)
        .map_err(|error| (1, format!("{left_display}: {error}")))?;
    let right_display = right.to_string_lossy().into_owned();
    let right = Pane::open(provider, right, false)
        .map_err(|error| (1, format!("{right_display}: {error}")))?;
    let mut app = AppState::new([left, right]);
    terminal::run(&mut app).map_err(|error| (1, format!("terminal: {error}")))
}

enum StartupAction {
    Run(Vec<OsString>),
    Help,
    Version,
}

fn parse_arguments(
    arguments: impl IntoIterator<Item = OsString>,
) -> Result<StartupAction, (u8, String)> {
    let mut locations = Vec::new();
    let mut options = true;
    for argument in arguments {
        if options && argument == "--" {
            options = false;
        } else if options && argument == "--help" {
            return Ok(StartupAction::Help);
        } else if options && argument == "--version" {
            return Ok(StartupAction::Version);
        } else if options && argument.as_encoded_bytes().starts_with(b"-") {
            return Err((2, format!("unknown option: {}", argument.to_string_lossy())));
        } else {
            locations.push(argument);
        }
    }
    if locations.len() > 2 {
        return Err((2, "too many directories".into()));
    }

    Ok(StartupAction::Run(locations))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(unix)]
    #[test]
    fn startup_arguments_preserve_non_utf8_paths() {
        use std::os::unix::ffi::OsStringExt;
        use std::time::{SystemTime, UNIX_EPOCH};

        let suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let root = std::env::temp_dir().join(format!("nav-rs-startup-{suffix}"));
        let path = root.join(OsString::from_vec(b"startup-\xff".to_vec()));
        std::fs::create_dir_all(&path).unwrap();
        let StartupAction::Run(locations) =
            parse_arguments([path.as_os_str().to_os_string()]).unwrap()
        else {
            panic!("expected locations");
        };
        assert_eq!(locations, [path.as_os_str()]);

        let pane = Pane::open(
            Arc::new(LocalProvider::new()),
            locations.into_iter().next().unwrap(),
            false,
        )
        .unwrap();
        assert_eq!(pane.location.resource.as_os_str(), path.as_os_str());
        std::fs::remove_dir_all(root).unwrap();
    }
}
