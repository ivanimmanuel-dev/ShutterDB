"""Require a version-matched tag and successful CI for its commit."""
import json
from pathlib import Path
import subprocess
import sys

tag = sys.argv[1]
repo = Path(__file__).resolve().parents[1]
version = (repo / "VERSION").read_text().strip()
assert tag == "v" + version, "Tag and VERSION disagree"
assert f'version = "{version}";' in (repo / "include/shutter/db.hpp").read_text()
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo).decode().strip()
tag_commit = subprocess.check_output(["git", "rev-parse", tag + "^{commit}"], cwd=repo).decode().strip()
assert commit == tag_commit, "Checkout is not the requested tag"
runs = json.loads(subprocess.check_output([
    "gh", "run", "list", "--workflow", "ci.yml", "--commit", commit,
    "--json", "databaseId,status,conclusion,url", "--limit", "100"], cwd=repo))
passed = [run for run in runs if run["status"] == "completed" and run["conclusion"] == "success"]
assert passed, "No successful hosted CI run exists for this exact commit"
print(json.dumps({"tag": tag, "commit": commit, "ci": passed[0]}, indent=2))
