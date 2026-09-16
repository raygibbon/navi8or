use crate::{EntryKind, LocalProvider, Provider, ResourceId, ViewerRequest};
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitStatus};

const HELPER_NAME: &str = "nav-viewer-c";

pub fn launch(request: &ViewerRequest) -> io::Result<ExitStatus> {
    let helper = helper_path()?;
    command_for_resource(
        request.provider.as_ref(),
        &request.entry.resource,
        request.entry.kind,
        &helper,
    )?
    .status()
}

fn helper_path() -> io::Result<PathBuf> {
    if let Some(configured) = std::env::var_os("NAV_VIEWER_HELPER") {
        return Ok(PathBuf::from(configured));
    }
    let executable = std::env::current_exe()?;
    let filename = if cfg!(windows) {
        format!("{HELPER_NAME}.exe")
    } else {
        HELPER_NAME.into()
    };
    let adjacent = executable.with_file_name(&filename);
    Ok(if adjacent.exists() {
        adjacent
    } else {
        PathBuf::from(filename)
    })
}

fn command_for_resource(
    provider: &dyn Provider,
    resource: &ResourceId,
    kind: EntryKind,
    helper: &Path,
) -> io::Result<Command> {
    if kind != EntryKind::File {
        return Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "the C Viewer bridge currently supports regular local files only",
        ));
    }
    let _local = provider
        .as_any()
        .downcast_ref::<LocalProvider>()
        .ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::Unsupported,
                "the C Viewer bridge currently supports local resources only",
            )
        })?;
    let path = LocalProvider::path_for_resource(resource);
    let mut command = Command::new(helper);
    command.arg(path.into_os_string());
    Ok(command)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Entry, LocalProvider};
    use std::ffi::OsString;
    use std::sync::Arc;

    fn request(resource: ResourceId) -> ViewerRequest {
        ViewerRequest {
            provider: Arc::new(LocalProvider::new()),
            entry: Entry {
                name: "shown-name.txt".into(),
                resource,
                kind: EntryKind::File,
                size: None,
                modified: None,
            },
        }
    }

    #[test]
    fn viewer_command_uses_resource_identity_not_display_name() {
        let resource = ResourceId::from_provider(OsString::from("actual-resource.txt"));
        let command = command_for_resource(
            request(resource.clone()).provider.as_ref(),
            &resource,
            EntryKind::File,
            Path::new("viewer-helper"),
        )
        .unwrap();
        assert_eq!(command.get_program(), "viewer-helper");
        assert_eq!(
            command.get_args().collect::<Vec<_>>(),
            [resource.as_os_str()]
        );
    }

    #[cfg(unix)]
    #[test]
    fn viewer_command_preserves_non_utf8_resource() {
        use std::os::unix::ffi::OsStringExt;

        let identity = OsString::from_vec(b"viewer-\xff.txt".to_vec());
        let resource = ResourceId::from_provider(identity.clone());
        let request = request(resource);
        let command = command_for_resource(
            request.provider.as_ref(),
            &request.entry.resource,
            request.entry.kind,
            Path::new("viewer-helper"),
        )
        .unwrap();
        assert_eq!(command.get_args().next(), Some(identity.as_os_str()));
    }
}
