#!/usr/bin/env python3
"""Exercise repository credential selection with real PTY input and encrypted Vaults."""
import os
import sys
import shutil
import tempfile
from resize_test import (spawn_nav, stop_nav, wait_for_exit, write,
                         open_repositories_menu, UP, DOWN)
from terminal_screen import TerminalScreen


def run(executable, style):
    with tempfile.TemporaryDirectory(prefix="nav-picker-") as root:
        os.mkdir(os.path.join(root, "nav"))
        with open(os.path.join(root, "nav", "nav.toml"), "w") as stream:
            stream.write('[menu]\nremember_position = false\n')
            if style == "classic":
                stream.write('[theme]\nname = "classic-dos"\n')
        if style == "classic":
            os.mkdir(os.path.join(root, "nav", "themes"))
            shutil.copyfile(os.path.join(os.path.dirname(executable), "themes", "classic-dos.toml"),
                            os.path.join(root, "nav", "themes", "classic-dos.toml"))
        screen = TerminalScreen(100, 30)
        pid, fd = spawn_nav(executable, root, root, root, screen)
        exited = False
        output = bytearray()

        def send(*values):
            for value in values:
                # Allow redraw and encrypted-vault transitions before the next scripted key.
                output.extend(write(fd, value, 0.4, screen=screen))

        def expect(text):
            assert text in screen.text(), (text, screen.text())

        def menu(command):
            screen.feed(open_repositories_menu(fd))
            send(command)

        def edit():
            menu(b"e")
            send(b"\r", b"\r", b"\r")  # first repository, name, URL
            expect("Repository Credential")

        def save():
            send(*([b"\r"] * 5))
            with open(os.path.join(root, "nav", "repositories.toml")) as stream:
                config = stream.read()
            for secret in ("picker-master", "testpass", "test-token"):
                assert secret not in config
            assert "Picker repository" in config
            return config

        try:
            menu(b"a")
            send(b"Picker repository", b"\r", b"https://localhost:8444/", b"\r")
            expect("<none>")
            send(DOWN, b"\r")
            expect("New master password")
            send(b"\x1b")  # cancel Vault creation without losing repository
            expect("Repository Credential")
            send(DOWN, b"\r", b"picker-master", b"\r", b"picker-master", b"\r")
            expect("Credential type")
            send(b"\x1b")  # cancel type selection
            expect("Repository Credential")
            send(DOWN, b"\r", b"\r", b"inline-basic", b"\r", b"navi8or", b"\r")
            send(b"testpass", b"\r", b"different", b"\r")
            expect("do not match")
            send(b"\r", b"\r", b"\r", b"testpass", b"\r", b"testpass", b"\r")
            assert 'credential = "inline-basic"' in save()

            edit()
            expect("Basic  navi8or")
            send(UP, UP, b"\r", b"\r", b"inline-basic", b"\r", b"navi8or", b"\r")
            send(b"testpass", b"\r", b"testpass", b"\r")
            expect("already exists")
            # Switch a failed Basic creation to Bearer; username must not carry over.
            send(DOWN, b"\r", b"\x7f" * len("inline-basic"), b"inline-bearer", b"\r")
            send(b"test-token", b"\r", b"test-token", b"\r")
            assert 'credential = "inline-bearer"' in save()

            edit()
            expect("Bearer")
            send(DOWN, b"\r")  # select existing Basic metadata row
            assert 'credential = "inline-basic"' in save()

            menu(b"v")
            send(b"l", b"\x1b")
            edit()
            expect("Vault locked")
            send(b"\r")  # retain reference without unlock
            assert 'credential = "inline-basic"' in save()

            edit()
            send(DOWN, b"\r", b"wrong-master", b"\r")
            expect("Unable to unlock vault")
            send(DOWN, b"\r", b"picker-master", b"\r")
            expect("Basic  navi8or")
            send(b"\x1b")
            assert 'credential = "inline-basic"' in save()

            # Standalone deletion leaves the repository reference intact.
            menu(b"v")
            send(b"\x1b[19~", b"y", b"\x1b")
            edit()
            expect("inline-basic [missing]")
            send(b"\r")
            assert 'credential = "inline-basic"' in save()
            edit()
            send(UP, b"\r")
            assert "credential =" not in save()
            for secret in (b"picker-master", b"testpass", b"test-token", b"wrong-master"):
                assert secret not in output, "masked field leaked a secret"
            send(b"\x1b[21~")
            wait_for_exit(pid, "credential picker")
            exited = True
            print(f"credential picker {style}: ok")
        finally:
            stop_nav(pid, fd, exited)


if __name__ == "__main__":
    executable = os.path.abspath(sys.argv[1])
    for style in ("modern", "classic"):
        run(executable, style)
