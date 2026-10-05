"""Build source and binary release archives with checksums."""
import argparse
import datetime
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--ref", default="HEAD")
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--install", type=Path)
parser.add_argument("--platform", choices=["linux-x64", "windows-x64"])
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]


def git(*arguments):
    return subprocess.check_output(["git", "-C", str(repo), *arguments])


commit = git("rev-parse", args.ref + "^{commit}").decode().strip()
version = git("show", commit + ":VERSION").decode().strip()
assert re.fullmatch(r"\d+\.\d+\.\d+(?:-alpha\.\d+)?", version), version
header = git("show", commit + ":include/shutter/db.hpp").decode()
assert f'version = "{version}";' in header, "Public API version disagrees with VERSION"
cmake = git("show", commit + ":CMakeLists.txt").decode()
assert f"project(ShutterDB VERSION {version.split('-')[0]} LANGUAGES CXX)" in cmake
epoch = int(git("show", "-s", "--format=%ct", commit))
stamp = datetime.datetime.fromtimestamp(max(epoch, 315532800), datetime.timezone.utc).timetuple()[:6]
args.output.mkdir(parents=True, exist_ok=True)
prefix = f"ShutterDB-{version}"
manifest = {"version": version, "commit": commit, "source_date_epoch": epoch,
            "experimental": True, "source_files": []}


def add(archive, name, data, mode=0o644, compression=zipfile.ZIP_DEFLATED):
    entry = zipfile.ZipInfo(name, stamp)
    entry.create_system = 3
    entry.external_attr = (0o100000 | mode) << 16
    archive.writestr(entry, data, compress_type=compression, compresslevel=9)


source = args.output / f"{prefix}-source.zip"
tar_bytes = git("archive", "--format=tar", commit)
with tarfile.open(fileobj=io.BytesIO(tar_bytes)) as tree, zipfile.ZipFile(source, "w") as archive:
    for member in sorted(tree.getmembers(), key=lambda item: item.name):
        if not member.isfile():
            assert member.isdir(), "Release source may not contain symlinks or special files"
            continue
        data = tree.extractfile(member).read()
        # Stored source entries reproduce byte-for-byte across OS and zlib versions.
        add(archive, f"shutterdb-{version}/{member.name}", data, member.mode, zipfile.ZIP_STORED)
        manifest["source_files"].append({"path": member.name, "bytes": len(data),
                                          "sha256": hashlib.sha256(data).hexdigest()})
artifacts = [source]
if args.install:
    assert args.platform, "--install requires --platform"
    executable = args.install / "bin" / ("shutter.exe" if args.platform == "windows-x64" else "shutter")
    reported = subprocess.check_output([str(executable.resolve()), "version"]).decode().strip()
    assert reported == f"ShutterDB {version}", reported
    binary = args.output / f"{prefix}-{args.platform}.zip"
    with zipfile.ZipFile(binary, "w") as archive:
        for path in sorted(args.install.rglob("*")):
            if path.is_file():
                relative = path.relative_to(args.install).as_posix()
                mode = 0o755 if relative.startswith("bin/") else 0o644
                add(archive, f"shutterdb-{version}-{args.platform}/{relative}", path.read_bytes(), mode)
        runtime = {
            "windows-x64": "x64 MSVC Release; requires the Microsoft Visual C++ runtime.",
            "linux-x64": "x64 Ubuntu 24.04; requires compatible glibc and libstdc++.",
        }[args.platform]
        notice = (f"ShutterDB {version} ({args.platform}, experimental)\nCommit: {commit}\n\n"
                  f"CLI: bin/{executable.name}\n"
                  "Use this directory as CMAKE_PREFIX_PATH and link ShutterDB::ShutterDB.\n"
                  f"Runtime: {runtime}\n"
                  "Build applications with a compatible C++ compiler, ABI and runtime.\n"
                  "Documentation: https://github.com/ivanimmanuel-dev/ShutterDB#documentation\n")
        add(archive, f"shutterdb-{version}-{args.platform}/README.txt", notice.encode())
    artifacts.append(binary)
    manifest["binary_platform"] = args.platform
manifest["artifacts"] = [{"file": path.name, "bytes": path.stat().st_size,
                           "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in artifacts]
suffix = args.platform or "source"
metadata = args.output / f"{prefix}-{suffix}-manifest.json"
metadata.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
artifacts.append(metadata)
sums = args.output / f"SHA256SUMS-{suffix}.txt"
sums.write_text("".join(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n"
                        for path in artifacts), encoding="utf-8", newline="\n")
print(f"Packaged {version} at {commit}: " + ", ".join(path.name for path in artifacts))
