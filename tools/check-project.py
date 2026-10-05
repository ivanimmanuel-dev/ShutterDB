"""Check release versions, release notes and local Markdown links."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

repo = Path(__file__).resolve().parents[1]
errors = []
version = (repo / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+(?:-alpha\.\d+)?", version):
    errors.append(f"Invalid VERSION: {version}")
for filename, expected in [
    ("include/shutter/db.hpp", f'version = "{version}";'),
    ("CMakeLists.txt", f"project(ShutterDB VERSION {version.split('-')[0]} LANGUAGES CXX)"),
]:
    if expected not in (repo / filename).read_text(encoding="utf-8"):
        errors.append(f"{filename}: version disagrees with VERSION ({version})")
if not (repo / f"docs/releases/v{version}.md").is_file():
    errors.append(f"Missing release notes: docs/releases/v{version}.md")
if not re.search(rf"^## {re.escape(version)}(?:\s|$)",
                 (repo / "CHANGELOG.md").read_text(encoding="utf-8"), re.M):
    errors.append(f"CHANGELOG.md: missing entry for {version}")


def anchors(path):
    headings = re.findall(r"^#{1,6}\s+(.+)$", path.read_text(encoding="utf-8"), re.M)
    return {re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-") for title in headings}


documents = sorted(repo.glob("*.md")) + sorted((repo / "docs").rglob("*.md"))
links = 0
for path in documents:
    content = path.read_text(encoding="utf-8")
    for target in re.findall(r"\]\(([^\s)]+)\)", content):
        url = urlsplit(target)
        if url.scheme or url.netloc:
            continue
        destination = (path.parent / unquote(url.path)).resolve() if url.path else path
        links += 1
        if not destination.exists():
            errors.append(f"{path.relative_to(repo)}: missing link target {target}")
        elif url.fragment and destination.suffix == ".md" and unquote(url.fragment) not in anchors(destination):
            errors.append(f"{path.relative_to(repo)}: missing heading {target}")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Version {version}, release notes and {links} local links in {len(documents)} documents checked.")
