#!/usr/bin/env python3
"""PTY resize smoke test for persistent and transient Navi8or controls."""
import errno
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import tempfile
import time
import termios
from terminal_screen import TerminalScreen

GEOMETRIES = [(120, 40), (80, 25), (60, 15), (140, 45), (40, 10),
              (12, 5), (100, 30)]
UP = b"\x1bOA"
DOWN = b"\x1bOB"
LEFT = b"\x1bOD"
RIGHT = b"\x1bOC"
HOME = b"\x1bOH"
END = b"\x1bOF"

def resize(fd, columns, rows):
    fcntl.ioctl(fd, termios.TIOCSWINSZ,
                struct.pack("HHHH", rows, columns, 0, 0))

def write(fd, data, delay=0.12, screen=None):
    os.write(fd, data)
    time.sleep(delay)
    output = drain(fd)
    if screen:
        screen.feed(output)
    return output

def read_pty(fd, size=65536):
    try:
        return os.read(fd, size)
    except OSError as error:
        # Linux PTY masters may return EIO after slave close; treat that as EOF.
        if error.errno == errno.EIO:
            return b""
        raise


def drain(fd):
    output = bytearray()
    os.set_blocking(fd, False)
    try:
        while True:
            data = read_pty(fd)
            if not data:
                break
            output.extend(data)
    except BlockingIOError:
        pass
    finally:
        os.set_blocking(fd, True)
    return bytes(output)


def read_until_quiet(fd, timeout=1.0, quiet=0.03):
    output = bytearray()
    deadline = time.monotonic() + timeout
    quiet_deadline = None
    while time.monotonic() < deadline:
        current_deadline = quiet_deadline if quiet_deadline else deadline
        wait = max(0, min(current_deadline, deadline) - time.monotonic())
        ready, _, _ = select.select([fd], [], [], wait)
        if not ready:
            break
        data = read_pty(fd)
        if not data:
            break
        output.extend(data)
        quiet_deadline = time.monotonic() + quiet
    return bytes(output)

def alive(pid):
    child, status = os.waitpid(pid, os.WNOHANG)
    if child:
        raise RuntimeError(
            f"Navi8or exited during resize: {os.waitstatus_to_exitcode(status)}")

def resize_series(pid, fd):
    for columns, rows in GEOMETRIES:
        resize(fd, columns, rows)
        time.sleep(0.05)
        drain(fd)
        alive(pid)

def spawn_nav(executable, left, right, config_home, screen=None,
              columns=100, rows=30):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["XDG_CONFIG_HOME"] = config_home
        os.execv(executable, [executable, left, right])
    resize(fd, columns, rows)
    time.sleep(0.2)
    output = drain(fd)
    if screen:
        screen.feed(output)
    return pid, fd


def run_modern_screen_hierarchy_check(executable, left, right, config_home):
    columns, rows = 140, 30
    divider = columns // 2
    screen = TerminalScreen(columns, rows)
    pid, fd = spawn_nav(executable, left, right, config_home, screen,
                        columns, rows)
    exited = False
    try:
        lines = screen.text().splitlines()
        if "[L]" in lines[1] or "[R]" in lines[1]:
            raise RuntimeError("Modern pane titles retained side badges")
        if lines[1].count("Local Filesystem") != 2:
            raise RuntimeError("Modern pane titles are not aligned")
        for row in range(1, 28):
            if lines[row][divider] != "│":
                raise RuntimeError(f"Modern divider missing on row {row}")
        pane_ranges = [(0, divider), (divider + 1, columns)]
        for start, end in pane_ranges:
            header = lines[3][start:end]
            body = next((line[start:end] for line in lines[4:27]
                         if "tiny.txt" in line[start:end]), "")
            name_column = header.find("Name")
            size_column = header.find("Size")
            modified_column = header.find("Modified")
            size_value = body.find("266 B")
            modified_value = body.find(time.strftime("%Y-"))
            if not (name_column == 1 < size_column < modified_column):
                raise RuntimeError("Full headings are not ordered within a pane")
            if size_column + len("Size") != size_value + len("266 B") or \
                    modified_column != modified_value:
                raise RuntimeError("Full headings and body columns do not align")
        if lines[28].strip() != "Ready":
            raise RuntimeError("Modern status is not isolated on its row")
        if "F1 Help" not in lines[29] or "F10 Quit" not in lines[29]:
            raise RuntimeError("Modern keybar is not visible")
        tokens = ("F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F10")
        positions = [lines[29].index(token) for token in tokens]
        if positions != sorted(positions) or len(set(positions)) != len(tokens):
            raise RuntimeError("Modern keybar token positions overlap or are unordered")
        key_style = screen.cells[29][positions[0]][1]
        label_style = screen.cells[29][positions[0] + len(tokens[0])][1]
        for token, column in zip(tokens, positions):
            available = token in ("F1", "F2", "F7", "F10")
            if available and (screen.cells[29][column][1] != key_style or
                              screen.cells[29][column + len(token)][1] != label_style):
                raise RuntimeError(f"Available {token} keybar roles are inconsistent")
            if not available and (screen.cells[29][column][1] == key_style or
                                  screen.cells[29][column][1] !=
                                  screen.cells[29][column + len(token)][1]):
                raise RuntimeError(f"Unavailable {token} is not uniformly disabled")
        if screen.cells[1][1][1] == screen.cells[1][divider + 2][1]:
            raise RuntimeError("Active and inactive pane titles look identical")
        if screen.cells[4][0][1] == screen.cells[4][divider + 1][1]:
            raise RuntimeError("Active and inactive selections look identical")
        key_column = lines[29].index("F3")
        if screen.cells[29][key_column][1] != screen.cells[29][key_column + 3][1]:
            raise RuntimeError("Unavailable F3 is still accented")
        body_before = tuple(tuple(cell[0] for cell in row)
                            for row in screen.cells[4:27])
        write(fd, DOWN, screen=screen)
        body_after = tuple(tuple(cell[0] for cell in row)
                           for row in screen.cells[4:27])
        if body_after != body_before:
            raise RuntimeError("Selection styling shifted pane geometry")
        if screen.cells[29][key_column][1] == screen.cells[29][key_column + 3][1]:
            raise RuntimeError("Available F3 is not accented")
        lines = screen.text().splitlines()
        for token in tokens:
            column = lines[29].index(token)
            if screen.cells[29][column][1] != key_style or \
                    screen.cells[29][column + len(token)][1] != label_style:
                raise RuntimeError(f"Selected-file {token} keybar roles are inconsistent")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "Modern screen hierarchy")
        exited = True
        print("behavior Modern screen hierarchy cells: ok")
    finally:
        stop_nav(pid, fd, exited)


def run_long_path_check(executable, left, right, config_home):
    columns, rows = 60, 15
    divider = columns // 2
    screen = TerminalScreen(columns, rows)
    pid, fd = spawn_nav(executable, left, right, config_home, screen,
                        columns, rows)
    exited = False
    try:
        path_row = screen.text().splitlines()[2]
        left_path = path_row[:divider]
        if "..." not in left_path or not left_path.rstrip().endswith(
                "/src/providers/http"):
            raise RuntimeError(f"Long path did not preserve its tail: {left_path!r}")
        if path_row[divider] != "│":
            raise RuntimeError("Long path overwrote the center divider")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "Long path")
        exited = True
        print("behavior Long path tail: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_initial_screen_check(executable, left, right, config_home, name,
                             expected, absent=()):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["XDG_CONFIG_HOME"] = config_home
        os.execv(executable, [executable, left, right])
    exited = False
    try:
        resize(fd, 140, 40)
        time.sleep(0.3)
        output = drain(fd)
        for value in expected:
            if value not in output:
                raise RuntimeError(f"{name}: missing initial screen text {value!r}")
        for value in absent:
            if value in output:
                raise RuntimeError(f"{name}: unexpected screen text {value!r}")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"behavior {name}: ok")
    finally:
        stop_nav(pid, fd, exited)

def screen_differences(expected, actual):
    differences = []
    for row, (expected_row, actual_row) in enumerate(zip(expected, actual)):
        for column, (expected_cell, actual_cell) in enumerate(
                zip(expected_row, actual_row)):
            if expected_cell != actual_cell:
                differences.append(
                    f"({column},{row}) {expected_cell!r} -> {actual_cell!r}")
                if len(differences) == 8:
                    return differences
    return differences


def run_menu_close_restore_check(executable, left, right, config_home, name):
    screen = TerminalScreen(100, 30)
    pid, fd = spawn_nav(executable, left, right, config_home, screen)
    exited = False
    try:
        baseline = screen.snapshot()
        baseline_text = screen.text()
        for value in ["File", "Filesystem", "sample.txt", "F10", "Quit"]:
            if value not in baseline_text:
                raise RuntimeError(f"{name}: initial screen is missing {value!r}")
        for cycle in range(4):
            write(fd, b"\x1bOQ", screen=screen)
            for value in [RIGHT, RIGHT, LEFT, LEFT, DOWN]:
                write(fd, value, screen=screen)
            write(fd, b"\x1b", screen=screen)
            restored = screen.snapshot()
            if restored != baseline:
                details = "; ".join(screen_differences(baseline, restored))
                raise RuntimeError(
                    f"{name}: final screen differs after menu cycle {cycle + 1}: "
                    f"{details}")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"behavior {name}: ok")
    finally:
        stop_nav(pid, fd, exited)


def run_menu_resize_restore_check(executable, left, right, config_home, name):
    screen = TerminalScreen(100, 30)
    pid, fd = spawn_nav(executable, left, right, config_home, screen)
    exited = False
    try:
        baseline = screen.snapshot()
        for value in [b"\x1bOQ", RIGHT, DOWN]:
            write(fd, value, screen=screen)
        resize(fd, 74, 18)
        screen.feed(read_until_quiet(fd))
        alive(pid)
        resize(fd, 100, 30)
        screen.feed(read_until_quiet(fd))
        alive(pid)
        write(fd, b"\x1b", screen=screen)
        restored = screen.snapshot()
        if restored != baseline:
            details = "; ".join(screen_differences(baseline, restored))
            raise RuntimeError(f"{name}: final screen differs: {details}")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"behavior {name}: ok")
    finally:
        stop_nav(pid, fd, exited)


def run_filter_summary_check(executable, left, right, config_home):
    screen = TerminalScreen(100, 30)
    pid, fd = spawn_nav(executable, left, right, config_home, screen)
    exited = False
    try:
        for value in [b"/", b"sample", b"\r"]:
            write(fd, value, screen=screen)
        if "Filter: sample" not in screen.text().splitlines()[27]:
            raise RuntimeError("filter summary is not visible in its pane")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "filter summary")
        exited = True
        print("behavior filter summary: ok")
    finally:
        stop_nav(pid, fd, exited)

def wait_for_exit(pid, name):
    deadline = time.time() + 4
    while time.time() < deadline:
        child, status = os.waitpid(pid, os.WNOHANG)
        if child:
            if os.waitstatus_to_exitcode(status) != 0:
                raise RuntimeError(f"{name}: Navi8or returned an error")
            return
        time.sleep(0.05)
    raise RuntimeError(f"{name}: Navi8or did not exit")

def stop_nav(pid, fd, exited):
    if not exited:
        try:
            os.kill(pid, signal.SIGTERM)
            os.waitpid(pid, 0)
        except ProcessLookupError:
            pass
    os.close(fd)

def run_state(executable, left, right, config_home, name, enter, leave=b"\x1b",
              last_enter_delay=0.12):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        for index, keys in enumerate(enter):
            delay = last_enter_delay if index == len(enter) - 1 else 0.12
            write(fd, keys, delay)
        resize_series(pid, fd)
        if leave:
            if isinstance(leave, list):
                for keys in leave:
                    write(fd, keys)
            else:
                write(fd, leave)
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"resize {name}: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_screen_check(executable, left, right, config_home, name, keys,
                     expected, leave):
    screen = TerminalScreen(100, 30)
    pid, fd = spawn_nav(executable, left, right, config_home, screen=screen)
    exited = False
    output = bytearray()
    try:
        for value in keys:
            output.extend(write(fd, value, 0.25, screen=screen))
        if expected.decode("utf-8") not in screen.text():
            raise RuntimeError(
                f"{name}: expected screen text {expected!r}; "
                f"terminal tail was {bytes(output[-2000:])!r}")
        for value in leave:
            write(fd, value)
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"behavior {name}: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_brief_column_check(executable, left, right, config_home):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        resize(fd, 120, 20)
        time.sleep(0.2)
        drain(fd)
        first = write(fd, DOWN, 0.25)
        if b"build/" not in first or b"build                 /" in first:
            raise RuntimeError("Brief directory decoration is not adjacent")
        second = write(fd, RIGHT, 0.25)
        if b"file012" not in second:
            raise RuntimeError("Brief Right did not move to column two")
        edge = write(fd, RIGHT, 0.25)
        if b"file025" not in edge and b"\x1b[6;37H25" not in edge:
            raise RuntimeError("Brief viewport did not scroll one column")
        reverse = write(fd, LEFT, 0.25)
        if b"\x1b[6;7H12" not in reverse and b"file012" not in reverse:
            raise RuntimeError("Brief Left did not reverse column movement")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "Brief columns")
        exited = True
        print("behavior Brief columns: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_full_view_check(executable, left, right, config_home):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        resize(fd, 120, 20)
        time.sleep(0.2)
        drain(fd)
        write(fd, DOWN, 0.25)
        if write(fd, RIGHT, 0.25):
            raise RuntimeError("Full view treated Right as Brief column movement")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "Full view")
        exited = True
        print("behavior Full view: ok")
    finally:
        stop_nav(pid, fd, exited)

def open_repositories_menu(fd):
    output = bytearray(write(fd, b"\x1c"))
    for _ in range(3):
        output.extend(write(fd, b"\x1b[1;5C"))
    return bytes(output)

def run_repository_menu_check(executable, left, right, config_home):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    repositories_path = os.path.join(config_home, "nav", "repositories.toml")
    try:
        menu = open_repositories_menu(fd)
        if b"Open" not in menu or b"Add" not in menu or b"Repository" not in menu:
            raise RuntimeError(
                f"Repositories menu does not expose working commands: {menu[-2000:]!r}")
        write(fd, DOWN)
        write(fd, b"\r")
        write(fd, b"PTY Repository")
        write(fd, b"\r")
        write(fd, b"https://127.0.0.1:9/test")
        write(fd, b"\r")
        credential_prompt = write(fd, b"\r", 0.3)  # anonymous credential reference
        if b"Credential" not in credential_prompt and b"Verify TLS" not in credential_prompt:
            raise RuntimeError("repository editor did not expose Credential")
        write(fd, b"\r", 0.3)  # keep the default Verify TLS = on
        write(fd, b"\r", 0.3)  # keep Writable PUT = off
        write(fd, b"\r", 0.3)  # keep MkDir = off
        write(fd, b"\r", 0.3)  # keep Delete = off
        write(fd, b"\r", 0.3)  # keep Rename = off
        with open(repositories_path, encoding="utf-8") as stream:
            saved = stream.read()
        if ("PTY Repository" not in saved or "tls_verify = true" not in saved or
                "writable = false" not in saved or "mkdir = false" not in saved or
                "delete = false" not in saved or "rename = false" not in saved):
            raise RuntimeError("Add Repository was not persisted")

        open_repositories_menu(fd)
        write(fd, DOWN)
        write(fd, DOWN)
        write(fd, b"\r")
        write(fd, b"\r")  # choose the first repository
        write(fd, b" Edited")
        write(fd, b"\r")
        write(fd, b"\r")  # retain URL
        write(fd, DOWN)  # Add credential; create Vault inline
        write(fd, b"\r")
        for value in [b"inline-master", b"\r", b"inline-master", b"\r",
                      b"\r", b"local-basic", b"\r", b"navi8or", b"\r",
                      b"testpass", b"\r", b"testpass", b"\r"]:
            write(fd, value, 0.35)
        write(fd, b"\r", 0.3)  # retain Verify TLS
        write(fd, b"\x7f" * 3)
        write(fd, b"on")
        write(fd, b"\r", 0.3)  # enable Writable PUT
        write(fd, b"\r", 0.3)  # retain MkDir
        write(fd, b"\r", 0.3)  # retain Delete
        write(fd, b"\r", 0.3)  # retain Rename
        with open(repositories_path, encoding="utf-8") as stream:
            saved = stream.read()
        if ("PTY Repository Edited" not in saved or "writable = true" not in saved or
                'credential = "local-basic"' not in saved):
            raise RuntimeError("Edit Repository was not persisted")

        open_repositories_menu(fd)
        write(fd, DOWN)
        write(fd, DOWN)
        write(fd, b"\r")
        write(fd, b"\r")  # choose the first repository
        write(fd, b"\r")  # retain name
        write(fd, b"\r")  # retain URL
        write(fd, UP)  # none
        write(fd, UP)  # Add credential (wrap)
        write(fd, b"\r")
        for value in [DOWN, b"\r", b"local-bearer", b"\r",
                      b"test-token", b"\r", b"test-token", b"\r"]:
            write(fd, value, 0.35)
        for _ in range(5):
            write(fd, b"\r", 0.2)
        with open(repositories_path, encoding="utf-8") as stream:
            saved = stream.read()
        if 'credential = "local-bearer"' not in saved:
            raise RuntimeError("Bearer credential reference was not persisted")

        open_repositories_menu(fd)
        write(fd, DOWN)
        write(fd, DOWN)
        write(fd, DOWN)
        write(fd, b"\r")
        write(fd, b"\r")  # choose the first repository
        write(fd, b"y", 0.3)
        with open(repositories_path, encoding="utf-8") as stream:
            saved = stream.read()
        if "[[repositories]]" in saved:
            raise RuntimeError("Remove Repository was not persisted")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "repository menu CRUD")
        exited = True
        print("behavior repository menu CRUD: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_vault_ui_check(executable, left, right, config_home):
    screen = TerminalScreen(120, 30)
    pid, fd = spawn_nav(executable, left, right, config_home, screen, 120, 30)
    exited = False
    visible_output = bytearray()
    secret = b"vault-screen-secret"
    credential_secret = b"credential-password"
    bearer_secret = b"credential-token"
    try:
        for value in [b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", b"\x1b[1;5C", b"v"]:
            visible_output.extend(write(fd, value, screen=screen))
        if "No vault yet" not in screen.text():
            raise RuntimeError("Vault unavailable screen was not rendered")
        visible_output.extend(write(fd, b"\x1b[18~", screen=screen))
        visible_output.extend(write(fd, secret, screen=screen))
        visible_output.extend(write(fd, b"\r", screen=screen))
        visible_output.extend(write(fd, secret, screen=screen))
        visible_output.extend(write(fd, b"\r", 0.7, screen=screen))
        if "No credentials" not in screen.text():
            raise RuntimeError("Empty unlocked Vault was not rendered")
        visible_output.extend(write(fd, b"\x1b[18~", screen=screen))
        for value in [b"\r", b"ui-basic", b"\r", b"screen-user", b"\r",
                      credential_secret, b"\r", credential_secret, b"\r"]:
            visible_output.extend(write(fd, value, 0.35 if value == b"\r" else 0.12,
                                        screen=screen))
        text = screen.text()
        if "ui-basic" not in text or "Basic" not in text or "screen-user" not in text:
            raise RuntimeError("Basic credential metadata was not listed")
        visible_output.extend(write(fd, b"\x1b[18~", screen=screen))
        for value in [DOWN, b"\r", b"ui-bearer", b"\r",
                      bearer_secret, b"\r", bearer_secret, b"\r"]:
            visible_output.extend(write(fd, value, 0.35 if value == b"\r" else 0.12,
                                        screen=screen))
        if "ui-bearer" not in screen.text() or "Bearer" not in screen.text():
            raise RuntimeError("Bearer credential metadata was not listed")
        visible_output.extend(write(fd, b"\x1bOS", screen=screen))
        visible_output.extend(write(fd, b"\x7f" * len("screen-user"), screen=screen))
        visible_output.extend(write(fd, b"edited-user", screen=screen))
        visible_output.extend(write(fd, b"\r", screen=screen))
        visible_output.extend(write(fd, b"\r", 0.7, screen=screen))
        if "edited-user" not in screen.text():
            raise RuntimeError("Blank-secret Vault edit did not update metadata")
        if secret in visible_output or credential_secret in visible_output or \
                bearer_secret in visible_output:
            raise RuntimeError("A Vault secret was echoed by a masked field")
        visible_output.extend(write(fd, b"l", screen=screen))
        if "Vault locked" not in screen.text() or "ui-basic" in screen.text():
            raise RuntimeError("Vault lock did not hide credential metadata")
        visible_output.extend(write(fd, b"\r", screen=screen))
        visible_output.extend(write(fd, b"wrong-password", screen=screen))
        visible_output.extend(write(fd, b"\r", 0.7, screen=screen))
        if "Unable to unlock vault" not in screen.text():
            raise RuntimeError("Wrong Vault password did not fail safely")
        visible_output.extend(write(fd, b"\r", screen=screen))
        visible_output.extend(write(fd, secret, screen=screen))
        visible_output.extend(write(fd, b"\r", 0.7, screen=screen))
        if "ui-basic" not in screen.text():
            raise RuntimeError("Vault credential did not persist after lock/unlock")
        visible_output.extend(write(fd, b"\x1b[19~", screen=screen))
        visible_output.extend(write(fd, b"y", 0.7, screen=screen))
        if "ui-basic" in screen.text() or "ui-bearer" not in screen.text():
            raise RuntimeError("Vault delete confirmation did not remove credential")
        write(fd, b"\x1b[21~", screen=screen)
        write(fd, b"\x1b[21~", screen=screen)
        wait_for_exit(pid, "Vault UI")
        exited = True
        print("behavior Vault masked CRUD and lock: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_vault_classic_check(executable, left, right, config_home):
    screen = TerminalScreen(100, 25)
    pid, fd = spawn_nav(executable, left, right, config_home, screen, 100, 25)
    exited = False
    try:
        for value in [b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", b"\x1b[1;5C", b"v"]:
            write(fd, value, screen=screen)
        if "Credential Vault" not in screen.text() or "No vault yet" not in screen.text():
            raise RuntimeError("Classic Vault screen did not render")
        write(fd, b"\x1b[21~", screen=screen)
        write(fd, b"\x1b[21~", screen=screen)
        wait_for_exit(pid, "Classic Vault UI")
        exited = True
        print("behavior Classic Vault screen: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_view_menu_check(executable, left, right, config_home):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        resize(fd, 120, 20); time.sleep(0.2); drain(fd); write(fd, DOWN)
        for value in [b"\x1c", b"\x1b[1;5C", DOWN, DOWN, DOWN, b"\r"]:
            write(fd, value)
        if write(fd, RIGHT, 0.2):
            raise RuntimeError("View -> Full did not disable column movement")
        for value in [b"\x1c", b"\x1b[1;5C", DOWN, DOWN, b"\r"]:
            write(fd, value)
        brief_move = write(fd, RIGHT, 0.25)
        if b"file012" not in brief_move and b"\x1b[6;37H12" not in brief_move:
            raise RuntimeError("View -> Brief did not restore column movement")
        write(fd, b"\x1b[21~"); wait_for_exit(pid, "View modes"); exited = True
        print("behavior View Brief/Full: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_third_command_check(executable, left, right, config_home, marker):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        for value in [DOWN, b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", DOWN, b"\r"]:
            write(fd, value, 0.25)
        deadline = time.time() + 2
        while time.time() < deadline and not os.path.exists(marker):
            time.sleep(0.05)
        if not os.path.exists(marker):
            raise RuntimeError("Command menu Edit was not dispatched")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "menu third item")
        exited = True
        print("behavior Command menu Edit: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_disabled_check(executable, left, right, config_home):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        for value in [b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C",
                      DOWN, DOWN, DOWN, b"\r", b"\x1b[21~"]:
            write(fd, value, 0.25)
        alive(pid)  # F10 is ignored because disabled Enter kept the menu open.
        write(fd, b"\x1b")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, "disabled menu enter")
        exited = True
        print("behavior disabled menu enter: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_path_check(executable, left, right, config_home, name, keys, expected):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        for value in keys:
            write(fd, value, 0.25)
        deadline = time.time() + 2
        while time.time() < deadline and not os.path.exists(expected):
            time.sleep(0.05)
        if not os.path.exists(expected):
            raise RuntimeError(f"{name}: expected path was not created")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"behavior {name}: ok")
    finally:
        stop_nav(pid, fd, exited)

def run_absent_path_check(executable, left, right, config_home, name, keys, expected):
    pid, fd = spawn_nav(executable, left, right, config_home)
    exited = False
    try:
        for value in keys:
            write(fd, value, 0.25)
        deadline = time.time() + 2
        while time.time() < deadline and os.path.exists(expected):
            time.sleep(0.05)
        if os.path.exists(expected):
            raise RuntimeError(f"{name}: expected path was not removed")
        write(fd, b"\x1b[21~")
        wait_for_exit(pid, name)
        exited = True
        print(f"behavior {name}: ok")
    finally:
        stop_nav(pid, fd, exited)

def main(executable):
    with tempfile.TemporaryDirectory(prefix="nav-resize-") as root:
        left, right = os.path.join(root, "left"), os.path.join(root, "right")
        behavior_left = os.path.join(root, "behavior-left")
        behavior_right = os.path.join(root, "behavior-right")
        full_visual_left = os.path.join(root, "full-visual-left")
        full_visual_right = os.path.join(root, "full-visual-right")
        long_left = os.path.join(root, "deep-location-for-truncation",
                                 "project", "src", "providers", "http")
        mkdir_left = os.path.join(root, "mkdir-left")
        mkdir_right = os.path.join(root, "mkdir-right")
        move_left = os.path.join(root, "move-left")
        move_right = os.path.join(root, "move-right")
        tab_left = os.path.join(root, "tab-left")
        tab_right = os.path.join(root, "tab-right")
        delete_left = os.path.join(root, "delete-left")
        delete_right = os.path.join(root, "delete-right")
        brief_left = os.path.join(root, "brief-left")
        brief_right = os.path.join(root, "brief-right")
        config_home = os.path.join(root, "config")
        full_config_home = os.path.join(root, "full-config")
        default_config_home = os.path.join(root, "default-config")
        classic_config_home = os.path.join(root, "classic-config")
        light_config_home = os.path.join(root, "light-config")
        paper_config_home = os.path.join(root, "paper-config")
        repository_config_home = os.path.join(root, "repository-config")
        vault_config_home = os.path.join(root, "vault-config")
        marker = os.path.join(root, "editor-ran")
        os.mkdir(left)
        os.mkdir(right)
        os.mkdir(behavior_left)
        os.mkdir(behavior_right)
        os.mkdir(full_visual_left)
        os.mkdir(full_visual_right)
        os.makedirs(long_left)
        os.mkdir(mkdir_left)
        os.mkdir(mkdir_right)
        os.mkdir(move_left)
        os.mkdir(move_right)
        os.mkdir(tab_left)
        os.mkdir(tab_right)
        os.mkdir(delete_left)
        os.mkdir(delete_right)
        os.mkdir(brief_left)
        os.mkdir(brief_right)
        os.makedirs(os.path.join(config_home, "nav"))
        os.makedirs(os.path.join(full_config_home, "nav"))
        os.mkdir(default_config_home)
        os.makedirs(os.path.join(classic_config_home, "nav", "themes"))
        os.makedirs(os.path.join(light_config_home, "nav", "themes"))
        os.makedirs(os.path.join(paper_config_home, "nav", "themes"))
        os.makedirs(os.path.join(repository_config_home, "nav"))
        os.makedirs(os.path.join(vault_config_home, "nav"))
        with open(os.path.join(config_home, "nav", "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[nav]\nconfig_version = 1\n\n"
                         "[panels]\nview = \"brief\"\n\n"
                         "[editor]\ncommand = \"/usr/bin/touch\"\n"
                         f"args = [\"{marker}\"]\nwait = true\n\n"
                         "[menu]\nremember_position = false\n")
        with open(os.path.join(full_config_home, "nav", "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[panels]\nview = \"full\"\n")
        with open(os.path.join(repository_config_home, "nav", "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[menu]\nremember_position = false\n")
        with open(os.path.join(classic_config_home, "nav", "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[theme]\nname = \"classic-dos\"\n")
        source_theme = os.path.join(os.path.dirname(executable), "themes",
                                    "classic-dos.toml")
        with open(source_theme, "r", encoding="utf-8") as source:
            with open(os.path.join(classic_config_home, "nav", "themes",
                                   "classic-dos.toml"), "w",
                      encoding="utf-8") as destination:
                destination.write(source.read())
        with open(os.path.join(light_config_home, "nav", "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[theme]\nname = \"solar-light\"\n")
        source_theme = os.path.join(os.path.dirname(executable), "themes",
                                    "solar-light.toml")
        with open(source_theme, "r", encoding="utf-8") as source:
            with open(os.path.join(light_config_home, "nav", "themes",
                                   "solar-light.toml"), "w",
                      encoding="utf-8") as destination:
                destination.write(source.read())
        with open(os.path.join(paper_config_home, "nav", "nav.toml"), "w",
                  encoding="utf-8") as stream:
            stream.write("[theme]\nname = \"paper\"\n")
        source_theme = os.path.join(os.path.dirname(executable), "themes",
                                    "paper.toml")
        with open(source_theme, "r", encoding="utf-8") as source:
            with open(os.path.join(paper_config_home, "nav", "themes",
                                   "paper.toml"), "w",
                      encoding="utf-8") as destination:
                destination.write(source.read())
        with open(os.path.join(behavior_left, "sample.txt"), "w",
                  encoding="utf-8") as stream:
            stream.write("alpha\nbeta\nomega\n")
        with open(os.path.join(full_visual_left, "one-kib.bin"), "wb") as stream:
            stream.truncate(1024)
        with open(os.path.join(full_visual_left, "one-and-half-mib.bin"), "wb") as stream:
            stream.truncate(1536 * 1024)
        with open(os.path.join(full_visual_left, "tiny.txt"), "wb") as stream:
            stream.truncate(266)
        with open(os.path.join(full_visual_right, "tiny.txt"), "wb") as stream:
            stream.truncate(266)
        with open(os.path.join(full_visual_left, "medium.bin"), "wb") as stream:
            stream.truncate(999 * 1024)
        with open(os.path.join(full_visual_left, "large.tar.gz"), "wb") as stream:
            stream.truncate(17 * 1024 * 1024 // 10)
        with open(os.path.join(full_visual_left,
                               "this-is-a-very-long-name-that-must-not-overwrite-metadata.bin"),
                  "wb") as stream:
            stream.truncate(1024)
        with open(os.path.join(move_left, "move.txt"), "w",
                  encoding="utf-8") as stream:
            stream.write("move me\n")
        with open(os.path.join(tab_left, "left.txt"), "w",
                  encoding="utf-8") as stream:
            stream.write("LEFT_PANEL_MARKER\n")
        with open(os.path.join(tab_right, "right.txt"), "w",
                  encoding="utf-8") as stream:
            stream.write("RIGHT_PANEL_MARKER\n")
        delete_target = os.path.join(delete_left, "remove.txt")
        with open(delete_target, "w", encoding="utf-8") as stream:
            stream.write("remove me\n")
        os.mkdir(os.path.join(brief_left, "build"))
        for index in range(75):
            with open(os.path.join(brief_left, f"file{index:03}"), "w", encoding="utf-8"):
                pass
        with open(os.path.join(left, "sample.txt"), "w", encoding="utf-8") as stream:
            stream.write("alpha\n" + "x" * 20000 + " timeout\nomega\n")
        with open(os.path.join(left, "many.log"), "w", encoding="utf-8") as stream:
            for line in range(20000):
                stream.write(f"2026-09-05 INFO generated line {line}\n")
        with open(os.path.join(left, "large.bin"), "wb") as stream:
            stream.truncate(8 * 1024 * 1024)

        run_initial_screen_check(
            executable, full_visual_left, full_visual_right, default_config_home,
            "Full screen hierarchy",
            [b"Local Filesystem", full_visual_left.encode(), full_visual_right.encode(),
             b"Name", b"Size", b"Modified", b"266 B", b"999 KB", b"1.7 MB",
             b"1.0 KB", b"1.5 MB",
             b"F1", b"Help", b"F2", b"Menu",
             b"F10", b"Quit"], absent=["─".encode(), b"[L]", b"[R]"])
        run_modern_screen_hierarchy_check(
            executable, full_visual_left, full_visual_right,
            default_config_home)
        run_long_path_check(executable, long_left, full_visual_right,
                            default_config_home)
        run_initial_screen_check(
            executable, full_visual_left, full_visual_right,
            classic_config_home, "Classic screen hierarchy",
            [b"Local Filesystem", b"Name", b"Size", b"Modified",
             "─".encode(), b"[L]", b"[R]", b"F1", b"Help", b"F10", b"Quit"])
        run_initial_screen_check(
            executable, full_visual_left, full_visual_right,
            light_config_home, "Light modern screen hierarchy",
            [b"Local Filesystem", b"Name", b"Size", b"Modified",
             b"F1", b"Help", b"F10", b"Quit"],
            absent=["─".encode(), b"[L]", b"[R]"])
        run_initial_screen_check(
            executable, full_visual_left, full_visual_right,
            paper_config_home, "Paper modern screen hierarchy",
            [b"Local Filesystem", b"Name", b"Size", b"Modified",
             b"F1", b"Help", b"F10", b"Quit"],
            absent=["─".encode(), b"[L]", b"[R]"])
        run_menu_close_restore_check(executable, behavior_left, behavior_right,
                                     full_config_home, "Modern menu restore")
        run_menu_close_restore_check(executable, behavior_left, behavior_right,
                                     classic_config_home, "Classic menu restore")
        run_menu_close_restore_check(executable, behavior_left, behavior_right,
                                     light_config_home, "Light menu restore")
        run_menu_close_restore_check(executable, behavior_left, behavior_right,
                                     paper_config_home, "Paper menu restore")
        run_menu_resize_restore_check(executable, behavior_left, behavior_right,
                                      full_config_home, "Modern menu resize restore")
        run_menu_resize_restore_check(executable, behavior_left, behavior_right,
                                      classic_config_home, "Classic menu resize restore")
        run_screen_check(executable, behavior_left, behavior_right,
                         classic_config_home, "Classic Viewer",
                         [DOWN, b"\x1bOR"], b"Esc Back", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right,
                         light_config_home, "Light dialog",
                         [DOWN, b"\x1c", DOWN, b"\r"],
                         b"File Properties", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right,
                         light_config_home, "Light Viewer",
                         [DOWN, b"\x1bOR"], b"Esc Back", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right,
                         paper_config_home, "Paper dialog",
                         [DOWN, b"\x1c", DOWN, b"\r"],
                         b"File Properties", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right,
                         paper_config_home, "Paper Viewer",
                         [DOWN, b"\x1bOR"], b"Esc Back", [b"\x1b"])

        run_screen_check(executable, behavior_left, behavior_right, config_home,
                         "menu View enter", [DOWN, b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", b"\r"],
                         b"Esc Back", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right, config_home,
                         "menu accelerator", [DOWN, b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", b"v"],
                         b"Esc Back", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right, config_home,
                         "menu Properties enter",
                         [DOWN, b"\x1c", DOWN, b"\r"],
                         b"File Properties", [b"\x1b"])
        run_filter_summary_check(executable, behavior_left, behavior_right,
                                 config_home)
        run_third_command_check(executable, behavior_left, behavior_right,
                                config_home, marker)
        run_screen_check(executable, behavior_left, behavior_right, config_home,
                         "F2 menu", [b"\x1bOQ"], b"Open Location", [b"\x1b"])
        run_screen_check(executable, behavior_left, behavior_right, config_home,
                         "viewer menu enter",
                         [DOWN, b"\x1bOR", b"\x1c", b"\r"],
                         b"File Properties", [b"\x1b", b"\x1b"])
        run_disabled_check(executable, behavior_left, behavior_right, config_home)
        run_path_check(executable, mkdir_left, mkdir_right, config_home,
                       "mkdir Enter", [b"\x1b[18~", b"created", b"\r"],
                       os.path.join(mkdir_left, "created"))
        run_path_check(executable, move_left, move_right, config_home,
                       "move Enter", [DOWN, b"\x1b[17~", b"\r"],
                       os.path.join(move_right, "move.txt"))
        run_screen_check(executable, tab_left, tab_right, config_home,
                         "Tab panel switch", [DOWN, b"\t", DOWN, b"\x1bOR"],
                         b"RIGHT_PANEL_MARKER", [b"\x1b"])
        run_absent_path_check(executable, delete_left, delete_right, config_home,
                              "delete Enter confirmation", [DOWN, b"\x1b[19~", b"\r"],
                              delete_target)
        run_brief_column_check(executable, brief_left, brief_right, config_home)
        run_full_view_check(executable, brief_left, brief_right, full_config_home)
        run_repository_menu_check(executable, behavior_left, behavior_right,
                                  repository_config_home)
        run_vault_ui_check(executable, behavior_left, behavior_right,
                           vault_config_home)
        run_vault_classic_check(executable, behavior_left, behavior_right,
                                classic_config_home)
        run_view_menu_check(executable, brief_left, brief_right, config_home)

        run_state(executable, left, right, config_home, "commander", [], leave=b"")
        run_state(executable, left, right, config_home, "panel focus",
              [b"\x1b[B", b"\x1b[B", b"\t", b"\x1b[B", b"\x1b[B", b"\t", b"\x1b[A"])
        run_state(executable, left, right, config_home, "menu",
                  [b"\x1c", b"\x1b[1;5C", b"\x1b[1;5C", b"\x1b[1;5D"],
              leave=[b"\x1c", b"\x1b[1;5C", b"\x1b", b"\t", b"\x1b[B"])
        menu_cycles = []
        for _ in range(20):
            menu_cycles.extend([b"\x1c", b"\x1b[1;5C", b"\x1b[1;5D", b"\x1b"])
        run_state(executable, left, right, config_home, "menu restore cycles", menu_cycles,
                  leave=[b"\t", b"\x1b[B", b"\t"])
        run_state(executable, left, right, config_home, "help",
                  [b"\x1bOP", b"\x1b[6~", b"\x1b[F", b"\x1b[H"])
        run_state(executable, left, right, config_home, "filter",
                  [b"/", b"no-match", b"\x1b[D", b"\x1b[D", b"X"],
                  leave=[b"\x1b[D", b"\x1b[3~", b"\x1b[H", b"\x1b[F", b"\x1b"])
        run_state(executable, left, right, config_home, "filter accept",
                  [b"/", b"sample", b"\r"], leave=b"")
        run_state(executable, left, right, config_home, "mkdir", [b"\x1b[18~"], leave=[b"\x1b", b"\t", b"\x1b[B"])
        run_state(executable, left, right, config_home, "copy progress",
                  [b"\x1b[B", b"\x1b[15~", b"\r"], leave=b"",
                  last_enter_delay=0)
        run_state(executable, left, right, config_home, "move", [b"\x1b[B", b"\x1b[17~"])
        run_state(executable, left, right, config_home, "delete", [b"\x1b[B", b"\x1b[19~"])
        run_state(executable, left, right, config_home, "viewer",
                  [b"\x1b[B", b"\x1b[B", b"\x1bOR", b"\x1b[B",
                   b"\x1b[6~", b"\x1b[H", b"\x1b[F", b"\x1b[D",
                   b"\x1b[1;5C", b"w", b"l"])
        run_state(executable, left, right, config_home, "viewer menu focus",
              [b"\x1b[B", b"\x1bOR", b"\x1c", b"\x1b[1;5C"],
              leave=[b"\x1b", b"\x1b[B", b"\x1b", b"\t", b"\x1b[B"])
        run_state(executable, left, right, config_home, "viewer go to",
                  [b"\x1b[B", b"\x1b[B", b"\x1bOR", b"g",
                   b"\x1b[H", b"\x1b[3~", b"2"],
                  leave=[b"\r", b"\x1b"])
        run_state(executable, left, right, config_home, "large viewer",
                  [b"\x1b[B", b"\x1bOR", b"\x1b[6~", b"\x1b[F"])
        run_state(executable, left, right, config_home, "find",
                  [b"\x1b[B", b"\x1b[B", b"\x1bOR", b"/"] ,
                  leave=[b"\x1b", b"\x1b"])
        run_state(executable, left, right, config_home, "find repeat",
                  [b"\x1b[B", b"\x1b[B", b"\x1bOR", b"/", b"timeout", b"\r"],
                  leave=[b"\x1b[15~", b"\x1b[17~", b"\x1b"])
    return 0

if __name__ == "__main__":
    raise SystemExit(main(os.path.abspath(sys.argv[1])))
