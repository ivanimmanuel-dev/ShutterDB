"""Build outside the source tree, using either an install prefix or local FetchContent."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
group = parser.add_mutually_exclusive_group(required=True)
group.add_argument("--prefix", type=Path)
group.add_argument("--source", type=Path)
parser.add_argument("--config", default="Release")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="shutter-independent-consumer-") as directory:
    root = Path(directory)
    if args.source:
        dependency = ('include(FetchContent)\nFetchContent_Declare(ShutterDB SOURCE_DIR "' +
                      args.source.resolve().as_posix() + '")\nFetchContent_MakeAvailable(ShutterDB)\n' +
                      'if(TARGET shutter OR TARGET shutter_tests)\n'
                      'message(FATAL_ERROR "Subproject unexpectedly enabled CLI/tests")\nendif()\n')
    else:
        dependency = 'find_package(ShutterDB CONFIG REQUIRED)\n'
    (root / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.21)\nproject(IndependentConsumer LANGUAGES CXX)\n' +
        dependency + 'add_executable(consumer main.cpp)\n'
        'target_link_libraries(consumer PRIVATE ShutterDB::ShutterDB)\n', encoding="utf-8")
    (root / "main.cpp").write_text(r'''
#include <shutter/db.hpp>
#include <shutter/cache.hpp>
#include <string>
int main(int argc, char**) {
    const std::string binary("a\0b", 3);
    shutter::Cache cache("assets.shdb", {.max_bytes = 1024});
    if (argc == 1) {
        if (!cache.put("asset", binary)) return 5;
        cache.sync();
    } else if (cache.get_string("asset") != binary || cache.stats().storage.database_bytes > 1024) return 6;
    if (argc == 1) {
        shutter::DB db("consumer.shdb");
        db.put("text", "hello"); db.put("binary", binary); db.put("empty", "");
        if (db.get_string("text") != "hello") return 1;
        db.sync();
    } else {
        shutter::Options options;
        options.create_if_missing = false;
        shutter::DB db("consumer.shdb", options);
        if (db.get_string("binary") != binary || db.get_string("empty") != "") return 2;
        if (!db.remove("text") || db.contains("text")) return 3;
        db.compact();
        if (!db.verify().ok() || db.stats().keys != 2) return 4;
    }
    return 0;
}
''', encoding="utf-8")
    command = ["cmake", "-S", str(root), "-B", str(root / "build"),
               "-DCMAKE_BUILD_TYPE=" + args.config]
    if args.prefix:
        command.append("-DCMAKE_PREFIX_PATH=" + args.prefix.resolve().as_posix())
    subprocess.run(command, check=True)
    subprocess.run(["cmake", "--build", str(root / "build"), "--config", args.config, "--parallel", "2"], check=True)
    executable = root / "build" / "consumer"
    if sys.platform == "win32":
        executable = root / "build" / args.config / "consumer.exe"
    for extra in [[], ["reopen"]]:
        subprocess.run([str(executable), *extra], cwd=root, check=True, timeout=30)
print("Independent consumer passed: create, binary/empty values, process restart, delete, compact, verify")
