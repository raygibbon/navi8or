use nav_rs::{
    AppState, HttpAuth, HttpProvider, LocalProvider, Pane, Provider, profile::Profile, ui,
};
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
        StartupAction::Run { locations, profile } => (locations, profile),
        StartupAction::Help => {
            println!(
                "Usage: nav-rs [-i profile.toml] [left-location] [right-location]\n       nav-rs --version\nLocations may be local directories, HTTP/HTTPS URLs, or repo:NAME."
            );
            return Ok(());
        }
        StartupAction::Version => {
            println!("Navi8or Rust core {}", env!("CARGO_PKG_VERSION"));
            return Ok(());
        }
    };
    let (locations, profile_path) = locations;
    let profile =
        Profile::load(profile_path.as_deref()).map_err(|error| (2, format!("profile: {error}")))?;

    let current = env::current_dir().map_err(|error| (1, error.to_string()))?;
    let left = locations
        .first()
        .cloned()
        .unwrap_or_else(|| current.as_os_str().to_os_string());
    let right = locations
        .get(1)
        .cloned()
        .unwrap_or_else(|| current.into_os_string());
    let left_display = left.to_string_lossy().into_owned();
    let (left_provider, left) = provider_for(&left)?;
    let left = Pane::open(left_provider, left, profile.show_hidden)
        .map_err(|error| (1, format!("{left_display}: {error}")))?;
    let right_display = right.to_string_lossy().into_owned();
    let (right_provider, right) = provider_for(&right)?;
    let right = Pane::open(right_provider, right, profile.show_hidden)
        .map_err(|error| (1, format!("{right_display}: {error}")))?;
    let mut app = AppState::with_hidden([left, right], profile.show_hidden);
    ui::run(&mut app, profile).map_err(|error| (1, format!("terminal: {error}")))
}

fn provider_for(location: &OsString) -> Result<(Arc<dyn Provider>, OsString), (u8, String)> {
    let Some(text) = location.to_str() else {
        return Ok((Arc::new(LocalProvider::new()), location.clone()));
    };
    if let Some(name) = text.strip_prefix("repo:") {
        let (url, tls_verify) = configured_repository(name)?;
        let provider = HttpProvider::new(&url, HttpAuth::None, tls_verify)
            .map_err(|error| (2, format!("repository {name}: {error}")))?;
        return Ok((Arc::new(provider), OsString::from(url)));
    }
    if text.starts_with("http://") || text.starts_with("https://") {
        let provider = HttpProvider::new(text, HttpAuth::None, true)
            .map_err(|error| (2, format!("invalid repository URL: {error}")))?;
        Ok((Arc::new(provider), location.clone()))
    } else {
        Ok((Arc::new(LocalProvider::new()), location.clone()))
    }
}

fn configured_repository(name: &str) -> Result<(String, bool), (u8, String)> {
    let directory = if cfg!(windows) {
        std::env::var_os("APPDATA")
            .map(std::path::PathBuf::from)
            .map(|path| path.join("Navi8or"))
    } else {
        std::env::var_os("XDG_CONFIG_HOME")
            .filter(|value| !value.is_empty())
            .map(std::path::PathBuf::from)
            .or_else(|| {
                std::env::var_os("HOME").map(|home| std::path::PathBuf::from(home).join(".config"))
            })
            .map(|path| path.join("nav"))
    }
    .ok_or_else(|| (2, "configuration directory is unavailable".to_owned()))?;
    let path = directory.join("repositories.toml");
    let source = std::fs::read_to_string(&path)
        .map_err(|error| (2, format!("{}: {error}", path.display())))?;
    let document: toml::Value = source
        .parse()
        .map_err(|error| (2, format!("invalid repositories.toml: {error}")))?;
    let repositories = document
        .get("repositories")
        .and_then(toml::Value::as_array)
        .ok_or_else(|| (2, "repositories.toml has no repositories array".to_owned()))?;
    let repository = repositories
        .iter()
        .find(|item| {
            item.get("name")
                .and_then(toml::Value::as_str)
                .is_some_and(|candidate| candidate.eq_ignore_ascii_case(name))
        })
        .ok_or_else(|| (2, format!("repository {name} is not configured")))?;
    if repository
        .get("credential")
        .and_then(toml::Value::as_str)
        .is_some_and(|value| !value.is_empty())
    {
        return Err((
            2,
            format!(
                "repository {name} uses a vault credential; Rust vault integration is not yet available"
            ),
        ));
    }
    let url = repository
        .get("url")
        .and_then(toml::Value::as_str)
        .ok_or_else(|| (2, format!("repository {name} has no URL")))?;
    let tls_verify = repository
        .get("tls_verify")
        .and_then(toml::Value::as_bool)
        .unwrap_or(true);
    Ok((url.to_owned(), tls_verify))
}

enum StartupAction {
    Run {
        locations: Vec<OsString>,
        profile: Option<std::path::PathBuf>,
    },
    Help,
    Version,
}

fn parse_arguments(
    arguments: impl IntoIterator<Item = OsString>,
) -> Result<StartupAction, (u8, String)> {
    let mut locations = Vec::new();
    let mut profile = None;
    let mut options = true;
    let mut arguments = arguments.into_iter();
    while let Some(argument) = arguments.next() {
        if options && argument == "--" {
            options = false;
        } else if options && argument == "--help" {
            return Ok(StartupAction::Help);
        } else if options && argument == "--version" {
            return Ok(StartupAction::Version);
        } else if options && argument == "-i" {
            profile = Some(std::path::PathBuf::from(
                arguments
                    .next()
                    .ok_or_else(|| (2, "-i requires a profile path".into()))?,
            ));
        } else if options && argument.as_encoded_bytes().starts_with(b"-") {
            return Err((2, format!("unknown option: {}", argument.to_string_lossy())));
        } else {
            locations.push(argument);
        }
    }
    if locations.len() > 2 {
        return Err((2, "too many directories".into()));
    }

    Ok(StartupAction::Run { locations, profile })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn explicit_profile_argument_is_kept_separate_from_locations() {
        let StartupAction::Run { locations, profile } = parse_arguments([
            OsString::from("-i"),
            OsString::from("themes/classic-dos.toml"),
            OsString::from("."),
        ])
        .unwrap() else {
            panic!("expected run");
        };
        assert_eq!(
            profile.as_deref(),
            Some(std::path::Path::new("themes/classic-dos.toml"))
        );
        assert_eq!(locations, [OsString::from(".")]);
    }

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
        let StartupAction::Run { locations, .. } =
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
