"""Exercise release archives and version checks in an isolated Git repository."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

source = Path(__file__).resolve().parents[1]


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="shutter-release-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ["tools/package-release.py", "VERSION", "include/shutter/db.hpp", "CMakeLists.txt", "LICENSE"]:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, target)
        self.git("init", "-q")
        self.commit()

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.STDOUT)

    def commit(self):
        self.git("add", ".")
        self.git("-c", "user.name=Release test", "-c", "user.email=release@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "Fixture")

    def package(self, directory, *args, optimized=False):
        return subprocess.run([sys.executable, *(["-O"] if optimized else []),
                               str(self.root / "tools/package-release.py"), "--output",
                               str(self.root / directory), *args], capture_output=True, text=True)

    def test_reproducible_source_and_hashes(self):
        for directory in ["first", "second"]:
            result = self.package(directory)
            self.assertEqual(result.returncode, 0, result.stderr)
        first, second = self.root / "first", self.root / "second"
        for path in first.iterdir():
            self.assertEqual(path.read_bytes(), (second / path.name).read_bytes())
        manifest = json.loads(next(first.glob("*-manifest.json")).read_text())
        self.assertEqual(manifest["commit"], self.git("rev-parse", "HEAD").decode().strip())
        with zipfile.ZipFile(next(first.glob("*-source.zip"))) as archive:
            self.assertEqual(len(archive.namelist()), len(manifest["source_files"]))
            for record in manifest["source_files"]:
                content = archive.read(f"shutterdb-{manifest['version']}/{record['path']}")
                self.assertEqual(len(content), record["bytes"])
                self.assertEqual(hashlib.sha256(content).hexdigest(), record["sha256"])
        for line in next(first.glob("SHA256SUMS-*.txt")).read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((first / name).read_bytes()).hexdigest(), digest)

    def test_version_mismatch_with_optimized_python(self):
        (self.root / "VERSION").write_text("99.0.0\n")
        self.commit()
        result = self.package("out", optimized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Public API version disagrees", result.stderr)
        self.assertFalse((self.root / "out").exists())

    def test_cmake_version_mismatch(self):
        (self.root / "CMakeLists.txt").write_text("project(ShutterDB VERSION 99.0.0 LANGUAGES CXX)\n")
        self.commit()
        result = self.package("out")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CMake project version disagrees", result.stderr)

    def test_binary_requires_runtime_metadata(self):
        result = self.package("out", "--install", str(self.root / "stage"), "--platform", "linux-x64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--install requires --platform and --runtime", result.stderr)


if __name__ == "__main__":
    unittest.main()
