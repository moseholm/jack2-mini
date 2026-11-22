#!/usr/bin/env python3
"""Check the jack2-mini documentation for source and link drift.

The checker is intentionally stdlib-only. It validates:

* ``<!-- fact: path:line pattern="regex" -->`` anchors against the source tree
* generated directory-tree regions
* relative Markdown links and heading anchors
* Mermaid fences and their ``Source:`` captions
* per-index coverage of documentation files
* a small list of known-stale claims outside ``docs/archive``

With ``--update`` it rewrites only generated regions. Prose is never changed.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"

TREE_START = "<!-- generated:tree:start -->"
TREE_END = "<!-- generated:tree:end -->"
TREE_DOC = DOCS / "developer" / "architecture" / "03-building-blocks.md"
MAX_TREE_DEPTH = 4
EXCLUDED_DIRS = {".git", ".vscode", "__pycache__", "s10", "spa-shim"}

FACT_RE = re.compile(
    r'<!--\s*fact:\s*(?P<path>[^:\s]+):(?P<line>\d+)\s+pattern="(?P<pattern>[^"]*)"\s*-->'
)
LINK_RE = re.compile(r'\]\((?P<target>[^)\s]+)(?:\s+"[^"]*")?\)')
HEADING_RE = re.compile(r"^(?P<hashes>#{1,6})\s+(?P<title>.+?)\s*#*$")
FENCE_RE = re.compile(r"^\s*```")
MERMAID_START_RE = re.compile(r"^\s*```mermaid\s*$", re.IGNORECASE)

STALE_ALLOW_START = "<!-- docs-check: allow-stale -->"
STALE_ALLOW_END = "<!-- docs-check: end-allow-stale -->"
STALE_PATTERNS: list[tuple[str, str]] = [
    (r"protocol\s+(?:version\s+)?12|kProtocolVersion\s*=\s*12", "protocol version 12"),
    (r"--secondary[^\n]{0,100}(?<!not )\brepeatable\b", "repeatable --secondary"),
    (r"--exclude-card[^\n]{0,100}(?<!not )\brepeatable\b", "repeatable --exclude-card"),
    (r"\bcheck\.sh\b", "check.sh"),
    (r"\b15\s+(?:unit\s+)?suites?\b", "15 unit suites"),
    (r"\b(?:client_open|client_close|activate_client|deactivate_client|register_port|audio_buffer|midi_buffer|get_sample_rate|set_buffer_size)\b", "snake_case client API"),
    (r"unless the card is `optional`", "optional unknown resampler"),
]

INDEX_TARGETS: dict[str, list[str]] = {
    "docs/user/README.md": ["docs/user/*.md"],
    "docs/developer/README.md": [
        "docs/developer/architecture/*.md",
        "docs/developer/style.md",
        "docs/developer/testing.md",
        "docs/developer/decisions/README.md",
    ],
    "docs/developer/decisions/README.md": ["docs/developer/decisions/0*.md"],
    "docs/archive/README.md": ["docs/archive/*.md"],
    "docs/measurements/README.md": [
        "docs/measurements/*.md",
        "docs/measurements/perf-baseline/README.md",
    ],
}


class Checker:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.docs: list[Path] = []
        self.fact_count = 0
        self.diagram_count = 0
        self.link_count = 0

    def error(self, message: str) -> None:
        self.errors.append(message)

    def rel(self, path: Path) -> str:
        try:
            return str(path.relative_to(ROOT))
        except ValueError:
            return str(path)

    def collect_docs(self) -> None:
        self.docs = sorted(DOCS.rglob("*.md"))
        if not self.docs:
            self.error(f"no Markdown files found under {self.rel(DOCS)}")

    def check_facts(self, text: str, doc: Path) -> None:
        cache: dict[Path, list[str]] = {}
        for number, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("<!--") and "fact:" in stripped and not FACT_RE.search(stripped):
                self.error(f"{self.rel(doc)}:{number}: malformed fact anchor: {stripped}")
        for match in FACT_RE.finditer(text):
            self.fact_count += 1
            rel_path = match.group("path")
            line_no = int(match.group("line"))
            pattern = match.group("pattern").replace("\\\\", "\\")
            source = ROOT / rel_path
            if not source.is_file():
                self.error(f"{self.rel(doc)}: fact source missing: {rel_path}")
                continue
            if source not in cache:
                cache[source] = source.read_text(encoding="utf-8", errors="replace").splitlines()
            lines = cache[source]
            if line_no < 1 or line_no > len(lines):
                self.error(
                    f"{self.rel(doc)}: fact line {rel_path}:{line_no} is outside the file"
                )
                continue
            try:
                found = re.search(pattern, lines[line_no - 1]) is not None
            except re.error as exc:
                self.error(f"{self.rel(doc)}: invalid fact regex {pattern!r}: {exc}")
                continue
            if not found:
                self.error(
                    f"{self.rel(doc)}: fact anchor {rel_path}:{line_no} does not match "
                    f"{pattern!r}; line is {lines[line_no - 1].strip()!r}"
                )

    @staticmethod
    def slugify(title: str) -> str:
        slug = title.strip().lower()
        slug = re.sub(r"[`*_]", "", slug)
        slug = re.sub(r"[^\w\-\s]", "", slug, flags=re.UNICODE)
        return re.sub(r"\s+", "-", slug).strip("-")

    def headings(self, text: str) -> set[str]:
        result: set[str] = set()
        seen: dict[str, int] = {}
        for line in text.splitlines():
            match = HEADING_RE.match(line)
            if not match:
                continue
            base = self.slugify(match.group("title"))
            count = seen.get(base, 0)
            seen[base] = count + 1
            result.add(base if count == 0 else f"{base}-{count}")
        return result

    @staticmethod
    def in_fence(text: str) -> set[int]:
        inside: set[int] = set()
        open_line: int | None = None
        for number, line in enumerate(text.splitlines(), 1):
            if FENCE_RE.match(line):
                if open_line is None:
                    open_line = number
                else:
                    inside.update(range(open_line, number + 1))
                    open_line = None
        return inside

    def check_links(self, text: str, doc: Path) -> None:
        heading_cache: dict[Path, set[str]] = {}
        fenced = self.in_fence(text)
        for match in LINK_RE.finditer(text):
            line_no = text.count("\n", 0, match.start()) + 1
            if line_no in fenced:
                continue
            target = match.group("target").strip("<>")
            if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                continue
            self.link_count += 1
            path_part, _, fragment = target.partition("#")
            resolved = (doc.parent / path_part).resolve() if path_part else doc.resolve()
            if not resolved.exists():
                self.error(f"{self.rel(doc)}:{line_no}: broken link {target!r}")
                continue
            line_range = re.fullmatch(r"L(\d+)(?:-L(\d+))?", fragment)
            if line_range:
                self.check_source_line(doc, target, resolved, line_range)
                continue
            if not fragment:
                continue
            if resolved.suffix == ".md":
                if resolved not in heading_cache:
                    heading_cache[resolved] = self.headings(
                        resolved.read_text(encoding="utf-8", errors="replace")
                    )
                if fragment not in heading_cache[resolved]:
                    self.error(
                        f"{self.rel(doc)}:{line_no}: broken heading anchor {target!r}"
                    )

    def check_source_line(
        self,
        doc: Path,
        target: str,
        resolved: Path,
        line_range: re.Match[str],
    ) -> None:
        start = int(line_range.group(1))
        end = int(line_range.group(2) or start)
        if end < start:
            self.error(f"{self.rel(doc)}: inverted line range in {target!r}")
            return
        if resolved.suffix == ".md":
            return
        try:
            lines = resolved.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            return
        if end > len(lines):
            self.error(
                f"{self.rel(doc)}: source line out of range in {target!r} "
                f"(file has {len(lines)} lines)"
            )
            return
        for number in range(start, end + 1):
            if not lines[number - 1].strip():
                self.error(f"{self.rel(doc)}: source line is blank in {target!r}")

    def check_mermaid(self, text: str, doc: Path) -> None:
        lines = text.splitlines()
        if sum(1 for line in lines if FENCE_RE.match(line)) % 2:
            self.error(f"{self.rel(doc)}: unbalanced Markdown fence")
        index = 0
        while index < len(lines):
            if not MERMAID_START_RE.match(lines[index]):
                index += 1
                continue
            start = index
            index += 1
            while index < len(lines) and not FENCE_RE.match(lines[index]):
                index += 1
            if index >= len(lines):
                self.error(f"{self.rel(doc)}:{start + 1}: unterminated Mermaid block")
                break
            self.diagram_count += 1
            body = [line.strip() for line in lines[start + 1 : index] if line.strip()]
            if not body:
                self.error(f"{self.rel(doc)}:{start + 1}: empty Mermaid block")
            elif not re.match(
                r"^(flowchart|graph|sequenceDiagram|stateDiagram|classDiagram|erDiagram|journey)\b",
                body[0],
            ):
                self.error(
                    f"{self.rel(doc)}:{start + 1}: unrecognised Mermaid diagram {body[0]!r}"
                )
            after = "\n".join(lines[index + 1 : index + 9])
            if "Source:" not in after and "<!-- source:" not in after:
                self.error(
                    f"{self.rel(doc)}:{start + 1}: Mermaid block has no Source: caption"
                )
            index += 1

    def check_stale(self, text: str, doc: Path) -> None:
        if doc.relative_to(DOCS).parts[0] == "archive":
            return
        fenced = self.in_fence(text)
        allowed = False
        for number, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()
            if stripped == STALE_ALLOW_START:
                allowed = True
                continue
            if stripped == STALE_ALLOW_END:
                allowed = False
                continue
            if allowed or number in fenced or stripped.startswith("<!--"):
                continue
            for pattern, label in STALE_PATTERNS:
                if re.search(pattern, line, re.IGNORECASE):
                    self.error(f"{self.rel(doc)}:{number}: stale claim ({label}): {stripped}")

    def check_docs(self) -> None:
        self.collect_docs()
        for doc in self.docs:
            text = doc.read_text(encoding="utf-8", errors="replace")
            self.check_facts(text, doc)
            self.check_links(text, doc)
            self.check_mermaid(text, doc)
            self.check_stale(text, doc)

    def check_tree(self) -> None:
        marker_count = TREE_DOC.read_text(encoding="utf-8").count(TREE_START)
        if marker_count != 1:
            self.error(f"{self.rel(TREE_DOC)}: expected one generated tree marker")
        actual = generate_tree()
        text = TREE_DOC.read_text(encoding="utf-8")
        match = re.search(
            re.escape(TREE_START) + r".*?" + re.escape(TREE_END), text, re.DOTALL
        )
        if not match:
            self.error(f"{self.rel(TREE_DOC)}: generated tree region is incomplete")
        elif match.group(0) != f"{TREE_START}\n```text\n{actual}```\n{TREE_END}":
            self.error(
                f"{self.rel(TREE_DOC)}: generated tree is stale; run with --update"
            )

    def check_indexes(self) -> None:
        link_map: dict[Path, set[Path]] = {}
        for doc in self.docs:
            text = doc.read_text(encoding="utf-8", errors="replace")
            targets: set[Path] = set()
            for match in LINK_RE.finditer(text):
                target = match.group("target").split("#", 1)[0].strip("<>")
                if not target or re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                    continue
                targets.add((doc.parent / target).resolve())
            link_map[doc.resolve()] = targets
        for index_rel, patterns in INDEX_TARGETS.items():
            index = ROOT / index_rel
            if not index.is_file():
                self.error(f"missing index: {index_rel}")
                continue
            targets: set[Path] = set()
            for pattern in patterns:
                targets.update(path.resolve() for path in ROOT.glob(pattern))
            targets.discard(index.resolve())
            missing = sorted(
                self.rel(path) for path in targets if path not in link_map[index.resolve()]
            )
            for path in missing:
                self.error(f"{index_rel}: does not link to {path}")

    def check_commit(self) -> None:
        text = (DOCS / "README.md").read_text(encoding="utf-8", errors="replace")
        match = re.search(r"verified against commit `([0-9a-f]{7,40})`", text)
        if not match:
            self.error("docs/README.md: no recorded verification commit")
            return
        commit = match.group(1)
        result = subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e", f"{commit}^{{commit}}"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            self.error(f"docs/README.md: verification commit {commit} is not in this repository")

    def run(self, check_commit: bool) -> int:
        self.check_docs()
        self.check_tree()
        self.check_indexes()
        if check_commit:
            self.check_commit()
        if self.errors:
            for error in self.errors:
                print(f"ERROR {error}")
            print(
                f"docs check failed: {len(self.errors)} error(s); "
                f"{len(self.docs)} docs, {self.fact_count} facts, "
                f"{self.diagram_count} diagrams, {self.link_count} links checked"
            )
            return 1
        print(
            f"docs check passed: {len(self.docs)} docs, {self.fact_count} facts, "
            f"{self.diagram_count} diagrams, {self.link_count} links checked"
        )
        return 0


def collect_dir(path: Path, level: int) -> list[str]:
    if level >= MAX_TREE_DEPTH:
        return []
    names: list[str] = []
    for entry in path.iterdir():
        if not entry.is_dir() or entry.name in EXCLUDED_DIRS:
            continue
        if entry.name.startswith("build") or entry.name.startswith("."):
            continue
        names.append(entry.name)
    return sorted(names)


def render_dir(path: Path, prefix: str, level: int, lines: list[str]) -> None:
    children = collect_dir(path, level)
    for index, name in enumerate(children):
        last = index == len(children) - 1
        lines.append(f"{prefix}{'└── ' if last else '├── '}{name}/")
        next_prefix = prefix + ("    " if last else "│   ")
        render_dir(path / name, next_prefix, level + 1, lines)


def generate_tree() -> str:
    lines = ["jack2-mini/"]
    render_dir(ROOT, "", 0, lines)
    return "\n".join(lines) + "\n"


def update_tree() -> int:
    text = TREE_DOC.read_text(encoding="utf-8")
    replacement = f"{TREE_START}\n```text\n{generate_tree()}```\n{TREE_END}"
    updated, count = re.subn(
        re.escape(TREE_START) + r".*?" + re.escape(TREE_END),
        lambda _: replacement,
        text,
        count=1,
        flags=re.DOTALL,
    )
    if count != 1:
        print(f"ERROR {TREE_DOC}: generated tree region not found", file=sys.stderr)
        return 1
    if updated != text:
        TREE_DOC.write_text(updated, encoding="utf-8")
        print(f"updated generated tree in {TREE_DOC.relative_to(ROOT)}")
    else:
        print("generated tree already current")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate documentation")
    parser.add_argument(
        "--update", action="store_true", help="refresh generated regions (tree only)"
    )
    parser.add_argument(
        "--check-commit",
        action="store_true",
        help="verify the recorded source commit exists",
    )
    args = parser.parse_args()
    if not args.check and not args.update:
        parser.print_help()
        return 2
    if args.update:
        status = update_tree()
        if status:
            return status
    if args.check:
        return Checker().run(args.check_commit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
