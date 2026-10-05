"""Require a version-matched tag and successful CI for its commit."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("tag")
tag = parser.parse_args().tag
repo = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(repo / "tools/check-project.py")], check=True, stdout=sys.stderr)
version = (repo / "VERSION").read_text().strip()
if tag != "v" + version:
    parser.error("Tag and VERSION disagree")
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo).decode().strip()
tag_commit = subprocess.check_output(["git", "rev-parse", tag + "^{commit}"], cwd=repo).decode().strip()
if commit != tag_commit:
    parser.error("Checkout is not the requested tag")
runs = json.loads(subprocess.check_output([
    "gh", "run", "list", "--workflow", "ci.yml", "--commit", commit,
    "--json", "databaseId,status,conclusion,url", "--limit", "100"], cwd=repo))
passed = [run for run in runs if run["status"] == "completed" and run["conclusion"] == "success"]
if not passed:
    parser.error("No successful hosted CI run exists for this exact commit")
print(json.dumps({"tag": tag, "commit": commit, "ci": passed[0]}, indent=2))
