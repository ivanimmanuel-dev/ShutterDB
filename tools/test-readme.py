"""Compile and execute the README's actual first C++ example against an installation."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("usage: python tools/test-readme.py INSTALL_PREFIX")
readme = (Path(__file__).resolve().parents[1] / "README.md").read_text(encoding="utf-8")
example = re.search(r"```cpp\n(.*?)```", readme, re.S).group(1)
prefix = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="shutter-readme-") as directory:
    root = Path(directory)
    (root / "main.cpp").write_text(example, encoding="utf-8")
    (root / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(ReadmeExample LANGUAGES CXX)\n"
        "find_package(ShutterDB CONFIG REQUIRED)\n"
        "add_executable(readme main.cpp)\n"
        "target_link_libraries(readme PRIVATE ShutterDB::ShutterDB)\n", encoding="utf-8")
    subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build"),
                    f"-DCMAKE_PREFIX_PATH={prefix}", "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(root / "build"), "--config", "Release"], check=True)
    executable = root / "build" / "readme"
    if sys.platform == "win32":
        executable = root / "build" / "Release" / "readme.exe"
    output = subprocess.check_output([str(executable)], cwd=root)
    assert output.strip() == b"world", output
print("The README C++ example compiled and ran against the installed package.")
