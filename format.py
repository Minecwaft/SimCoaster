#!/usr/bin/env python3
"""Apply or check the project's private clang-format style."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
import difflib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / ".clang-format"
FORMATTER = Path("C:/msys64/ucrt64/bin/clang-format.exe")
SOURCE_ROOTS = ("src", "include", "cmake")
EXTENSIONS = {".cpp", ".hpp", ".h", ".inc"}
EXCLUDED = {
    "fixtures", "compiler-repros", "generated", "build", "build-gcc17",
    "third_party", "third-party", "vendor", "external",
}


class FormatError(Exception):
    pass


@dataclass(frozen=True)
class Preview:
    path: Path
    original: bytes
    formatted: bytes


def run_formatter(arguments: list[str], source: bytes = b"") -> bytes:
    result = subprocess.run(
        [str(FORMATTER), *arguments], input=source, capture_output=True, timeout=120,
    )
    if result.returncode:
        raise FormatError(result.stderr.decode("utf-8", errors="replace").strip())
    return result.stdout


def validate_formatter() -> str:
    version = run_formatter(["--version"]).decode("utf-8").strip()
    revision = re.search(r"PrivateGCC17 revision (\d+)", version)
    if revision is None or int(revision[1]) < 4:
        raise FormatError("PrivateGCC17 clang-format revision 4 or newer is required.")
    if not CONFIG.is_file():
        raise FormatError(f"Missing configuration: {CONFIG}")
    run_formatter([f"--style=file:{CONFIG.as_posix()}", "--dump-config", "--Werror"])
    return version


def tracked_sources() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", *SOURCE_ROOTS], cwd=ROOT,
        capture_output=True, check=True,
    )
    paths = []
    for name in result.stdout.decode("utf-8").split("\0"):
        relative = Path(name)
        if not name or relative.suffix.lower() not in EXTENSIONS or EXCLUDED.intersection(relative.parts):
            continue
        path = ROOT / relative
        if not path.is_file():
            raise FormatError(f"Tracked source is missing: {relative}")
        if path.is_symlink() or not any(path.resolve().is_relative_to(ROOT / folder) for folder in SOURCE_ROOTS):
            raise FormatError(f"Source resolves outside the selected trees: {relative}")
        paths.append(path)
    if not paths:
        raise FormatError("No tracked project source files found.")
    return paths


def preview(path: Path) -> Preview:
    original = path.read_bytes()
    arguments = [
        f"--style=file:{CONFIG.as_posix()}", f"--assume-filename={path.as_posix()}",
        "--fail-on-incomplete-format", "--Werror",
    ]
    try:
        formatted = run_formatter(arguments, original)
        second = run_formatter(arguments, formatted)
    except FormatError as error:
        raise FormatError(f"{path.relative_to(ROOT)}: {error}") from error
    if second != formatted:
        difference = difflib.unified_diff(
            formatted.decode("utf-8", errors="replace").splitlines(),
            second.decode("utf-8", errors="replace").splitlines(),
            fromfile="first pass", tofile="second pass", n=2,
        )
        excerpt = "\n".join(line for _, line in zip(range(40), difference))
        raise FormatError(f"{path.relative_to(ROOT)}: second pass changes output.\n{excerpt}")
    return Preview(path, original, formatted)


def replace_source(item: Preview) -> None:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=item.path.parent, prefix=f".{item.path.name}.", suffix=".format-tmp", delete=False,
        ) as output:
            temporary = Path(output.name)
            output.write(item.formatted)
        os.chmod(temporary, stat.S_IMODE(item.path.stat().st_mode))
        os.replace(temporary, item.path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report required changes without writing files")
    options = parser.parse_args()
    print(validate_formatter(), flush=True)
    paths = tracked_sources()
    previews = []
    pool = ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 1))
    try:
        pending = [pool.submit(preview, path) for path in paths]
        for count, completed in enumerate(as_completed(pending), 1):
            previews.append(completed.result())
            if count % 100 == 0 or count == len(paths):
                print(f"Checked {count}/{len(paths)} files", flush=True)
    finally:
        pool.shutdown(wait=True, cancel_futures=True)
    changed = sorted(
        (item for item in previews if item.original != item.formatted), key=lambda item: item.path,
    )
    if options.check:
        for item in changed:
            print(f"Needs formatting: {item.path.relative_to(ROOT)}")
        print(f"{len(paths)} files checked; {len(changed)} need formatting. All previews are stable.")
        return 1 if changed else 0

    # Check every input again before the first write, preserving concurrent edits.
    for item in previews:
        if item.path.read_bytes() != item.original:
            raise FormatError(f"Source changed during preview; no files written: {item.path.relative_to(ROOT)}")
    for item in changed:
        replace_source(item)
    print(f"Formatted {len(changed)} of {len(paths)} files. All previews were accepted and stable.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (FormatError, OSError, subprocess.SubprocessError) as error:
        print(f"Formatting failed: {error}", file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        print("Formatting cancelled.", file=sys.stderr)
        sys.exit(2)
