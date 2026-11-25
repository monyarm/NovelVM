#!/usr/bin/env python3
#
# Copyright (C) 2026 The NovelVM Authors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Audit ScummVM detection tables against the VNDB database.

The tool parses every engine's game id/title tables from an exported ScummVM
source tree (not from the fork's engine list, so removed upstream engines are
still audited), matches the titles against a VNDB database dump, and writes a
per-engine report. It can also emit the confirmed Director id set and rewrite
the Director detection tables from that set.

Typical use:

    curl -O https://dl.vndb.org/dump/vndb-db-latest.tar.zst
    tar --zstd -xf vndb-db-latest.tar.zst db/vn db/vn_titles db/releases \\
        db/releases_vn db/releases_platforms db/*.header
    git archive <rev> | tar -x -C source
    ./devtools/vndb-audit.py --dump dump --source source --report report.md \\
        --director-set director-set.tsv

The dump is read from ``--dump``; if it is missing or incomplete the tool falls
back to the VNDB HTTP API (https://api.vndb.org/kana/vn), with an on-disk cache
and rate limiting.

Matching is conservative and tiered:

  1. exact normalized full title;
  2. exact normalized main title (before " - " / ":") on either side;
  3. token-similarity candidates, never confirmed automatically.

Only tier 1 and explicitly reviewed ids enter the confirmed Director set. A
review file (``--reviews``, default ``<dump>/reviews.tsv``) records decisions
for tier 2/3 candidates, one per line::

    <scummvm id>\\t<vndb id>\\tconfirm|reject\\t<note>

Nothing in the source tree is modified unless ``--write-director-table`` is
given an explicit path.
"""

from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import sys
import time
import unicodedata
import urllib.request

PROG = "vndb-audit.py"

VNDB_API = "https://api.vndb.org/kana/vn"
VNDB_VN_URL = "https://vndb.org/{}"

# VNDB platform ids that count as Director targets.
DIRECTOR_PLATFORMS = {"win", "mac"}

# Support level names in the Phase 4 enum, most severe first.
STATE_SEVERITY = (
    ("ADGF_UNSUPPORTED", "Unsupported"),
    ("ADGF_WARNING", "Warning"),
    ("ADGF_UNSTABLE", "Unstable"),
    ("ADGF_TESTING", "Testing"),
)
DEFAULT_STATE = "Stable"


# ---------------------------------------------------------------------------
# Title normalization
# ---------------------------------------------------------------------------

# Parentheticals that only name a platform, an edition or a year are removed
# before comparison. Everything else is kept, to stay conservative.
PLATFORM_TOKENS = {
    "windows", "win", "win16", "win32", "win64", "mac", "macintosh", "macos",
    "osx", "dos", "msdos", "pc", "pc98", "pc88", "fmtowns", "amiga", "amigaos",
    "segacd", "megacd", "saturn", "dreamcast", "playstation", "ps1", "psx",
    "ps2", "ps3", "psp", "vita", "nes", "famicom", "snes", "superfamicom",
    "gameboy", "gb", "gbc", "gba", "nds", "ds", "3ds", "n64", "gamecube",
    "wii", "switch", "3do", "cdi", "x68000", "msx", "msx2", "pcengine", "pce",
    "turbografx", "tg16", "atari", "atarist", "appleii", "appleiigs", "linux",
    "ios", "android", "c64", "amstrad", "cpc", "zxspectrum", "electron",
    "fm7", "fmtown", "sharp", "x1", "pippin",
}
EDITION_TOKENS = {"demo", "shareware", "beta", "prototype", "preview", "trial"}

YEAR_RE = re.compile(r"^(?:19|20)\d\d$")
PAREN_RE = re.compile(r"\(([^()]*)\)")
MAIN_SPLIT_RE = re.compile(r"\s+-\s*|:\s+")


def strip_accents(text: str) -> str:
    decomposed = unicodedata.normalize("NFKD", text)
    return "".join(ch for ch in decomposed if not unicodedata.combining(ch))


def _is_platform_paren(content: str) -> bool:
    inner = strip_accents(content).casefold().strip()
    if not inner:
        return False
    if YEAR_RE.match(inner):
        return True
    words = re.findall(r"[a-z0-9]+", inner)
    if not words:
        return False
    if any(word in EDITION_TOKENS for word in words):
        return True
    return any(word in PLATFORM_TOKENS for word in words)


def normalize(title: str) -> str:
    """NFKD, strip accents and punctuation, drop platform parentheticals."""
    text = strip_accents(title).casefold()
    # Remove platform/edition/year parentheticals, repeatedly for nesting.
    for _ in range(3):
        new = PAREN_RE.sub(
            lambda m: " " if _is_platform_paren(m.group(1)) else m.group(0), text
        )
        if new == text:
            break
        text = new
    chars = [ch if (ch.isalnum() or ch.isspace()) else " " for ch in text]
    return " ".join("".join(chars).split())


def main_title(title: str) -> str:
    """Title before a subtitle separator, or the title itself."""
    parts = MAIN_SPLIT_RE.split(title, maxsplit=1)
    candidate = parts[0].strip()
    # Only accept a split that leaves something meaningful behind.
    if len(candidate) >= 3 and len(candidate.split()) >= 1:
        return candidate
    return title


def token_similarity(left: str, right: str) -> float:
    a = left.split()
    b = right.split()
    if not a or not b:
        return 0.0
    common = len(set(a) & set(b))
    return 2.0 * common / (len(a) + len(b))


# ---------------------------------------------------------------------------
# C++ table parsing
# ---------------------------------------------------------------------------

STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
ADGF_RE = re.compile(r"\bADGF_[A-Z0-9_]+\b")
PLATFORM_RE = re.compile(r"\bkPlatform[A-Za-z0-9]+\b")
SUPPORT_STATUS_DEFINE_RE = re.compile(r"#define\s+SUPPORT_STATUS\s+([A-Za-z0-9_]+)")

PGD_ARRAY_RE = re.compile(r"\bPlainGameDescriptor\s+(\w+)\s*\[\s*\]\s*=\s*\{")
PGD_SINGLE_RE = re.compile(r"\bPlainGameDescriptor\s+(\w+)\s*=\s*\{")
DESC_ARRAY_RE = re.compile(
    r"\b([A-Za-z_][A-Za-z0-9_]*)\s+(\w*(?:[Gg]ameDescription|[Gg]ameDescriptions|"
    r"adGameDescs?|GameDescs?)\w*)\s*\[\s*\]\s*=\s*\{"
)
DESC_NAME_RE = re.compile(r"(?i)(game_?descriptions?|adgame_?descs?|game_?descs?)")


def strip_comments(text: str) -> str:
    """Blank out C/C++ comments, preserving length and string literals."""
    out = []
    i = 0
    size = len(text)
    while i < size:
        ch = text[i]
        if ch == '"':
            j = i + 1
            while j < size:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            out.append(text[i:j])
            i = j
            continue
        if ch == "/" and i + 1 < size and text[i + 1] == "/":
            j = text.find("\n", i)
            if j < 0:
                j = size
            out.append(" " * (j - i))
            i = j
            continue
        if ch == "/" and i + 1 < size and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = size if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:j]))
            i = j
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def blank_strings(text: str) -> str:
    """Blank string literal contents so they cannot affect nesting."""
    out = []
    i = 0
    size = len(text)
    while i < size:
        ch = text[i]
        if ch == '"':
            j = i + 1
            while j < size:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            literal = text[i:j]
            if len(literal) >= 2:
                out.append(
                    '"'
                    + "".join(c if c == "\n" else " " for c in literal[1:-1])
                    + '"'
                )
            else:
                out.append(literal)
            i = j
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def match_brace(masked: str, start: int) -> int:
    depth = 0
    for i in range(start, len(masked)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def split_entries(masked: str, start: int, end: int):
    """Split a braced body into top-level entries; yields (start, end) spans."""
    depth = 0
    entry_start = start
    for i in range(start, end):
        ch = masked[i]
        if ch in "{[(":
            depth += 1
        elif ch in "}])":
            depth -= 1
        elif ch == "," and depth == 0:
            yield entry_start, i
            entry_start = i + 1
    if masked[entry_start:end].strip():
        yield entry_start, end


def unescape_c_string(text: str) -> str:
    result = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            mapping = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
            if nxt in mapping:
                result.append(mapping[nxt])
                i += 2
                continue
            if nxt == "x" and i + 3 < len(text):
                try:
                    result.append(chr(int(text[i + 2:i + 4], 16)))
                    i += 4
                    continue
                except ValueError:
                    pass
        result.append(ch)
        i += 1
    return "".join(result)


def string_literals(entry: str):
    return [unescape_c_string(m.group(1)) for m in STRING_RE.finditer(entry)]


def read_text(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def iter_source_files(source_root: str):
    engines = os.path.join(source_root, "engines")
    for root, _dirs, files in os.walk(engines):
        for name in sorted(files):
            if name.endswith((".cpp", ".h")):
                yield os.path.join(root, name)


def engine_of(path: str, source_root: str) -> str:
    rel = os.path.relpath(path, os.path.join(source_root, "engines"))
    return rel.split(os.sep)[0]


def parse_descriptors(source_root: str):
    """Return {engine: {id: {'title': str, 'file': str}}} for all engines."""
    games = {}
    for path in iter_source_files(source_root):
        text = read_text(path)
        if "PlainGameDescriptor" not in text:
            continue
        stripped = strip_comments(text)
        masked = blank_strings(stripped)
        engine = engine_of(path, source_root)
        engine_games = games.setdefault(engine, {})

        def add_entry(entry_text: str):
            literals = string_literals(entry_text)
            if len(literals) < 2:
                return
            game_id, title = literals[0], literals[1]
            if not game_id or not title:
                return
            if game_id in ("0", "nullptr") or game_id.startswith("AD_"):
                return
            # Later tables may repeat an id; first occurrence wins.
            engine_games.setdefault(
                game_id,
                {"title": title, "file": os.path.relpath(path, source_root)},
            )

        for match in PGD_ARRAY_RE.finditer(masked):
            body_start = match.end() - 1
            body_end = match_brace(masked, body_start)
            if body_end < 0:
                continue
            for entry_start, entry_end in split_entries(masked, body_start + 1, body_end):
                add_entry(stripped[entry_start:entry_end])
        for match in PGD_SINGLE_RE.finditer(masked):
            body_start = match.end() - 1
            body_end = match_brace(masked, body_start)
            if body_end < 0:
                continue
            add_entry(stripped[body_start:body_end + 1])
    return games


def parse_detection_entries(source_root: str):
    """Return {engine: {id: {'flags': set, 'platforms': set}}}.

    Flags and platforms are best effort: they are read from the entry text and,
    for Director, from the SUPPORT_STATUS macro active at that point.
    """
    entries = {}
    for path in iter_source_files(source_root):
        text = read_text(path)
        if "ADGF_" not in text and "GID_" not in text:
            continue
        stripped = strip_comments(text)
        masked = blank_strings(stripped)
        engine = engine_of(path, source_root)
        defines = [
            (m.start(), m.group(1))
            for m in SUPPORT_STATUS_DEFINE_RE.finditer(masked)
        ]

        def active_support_status(pos: int):
            active = None
            for define_pos, value in defines:
                if define_pos < pos:
                    active = value
                else:
                    break
            return active

        for match in DESC_ARRAY_RE.finditer(masked):
            var_name = match.group(2)
            if not DESC_NAME_RE.search(var_name):
                continue
            body_start = match.end() - 1
            body_end = match_brace(masked, body_start)
            if body_end < 0:
                continue
            engine_entries = entries.setdefault(engine, {})
            for entry_start, entry_end in split_entries(masked, body_start + 1, body_end):
                entry = stripped[entry_start:entry_end]
                literals = string_literals(entry)
                if not literals:
                    continue
                game_id = literals[0]
                if not game_id or game_id.startswith("AD_"):
                    continue
                record = engine_entries.setdefault(
                    game_id, {"flags": set(), "platforms": set()}
                )
                record["flags"].update(ADGF_RE.findall(entry))
                entry_start_text = entry.lstrip()
                if "SUPPORT_STATUS" in entry or (
                    defines and not entry_start_text.startswith("{")
                ):
                    status = active_support_status(entry_start)
                    if status and status.startswith("ADGF_"):
                        record["flags"].add(status)
                record["platforms"].update(PLATFORM_RE.findall(entry))
                if not entry_start_text.startswith("{"):
                    # Director-style macro entries name their platform only in
                    # the macro definition.
                    for prefix, platform in MACRO_PLATFORM_PREFIX.items():
                        if entry_start_text.startswith(prefix):
                            record["platforms"].add(platform)
                            break
    return entries


# ScummVM platform names that map to a VNDB platform id. Only platforms that
# are a meaningful signal are listed; desktop platforms appear everywhere and
# would confirm almost anything.
PLATFORM_NAME_MAP = {
    "SegaCD": {"scd"},
    "FMTowns": {"fmt"},
    "PC98": {"p98"},
    "PC88": {"p88"},
    "PCEngine": {"pce"},
    "MSX": {"msx"},
    "X68000": {"x68"},
    "FM7": {"fm7"},
    "Saturn": {"sat"},
    "Playstation": {"ps1"},
    "PS2": {"ps2"},
    "PS3": {"ps3"},
    "PS4": {"ps4"},
    "PSP": {"psp"},
    "PSVita": {"psv"},
    "3DO": {"n3d"},
    "Dreamcast": {"drc"},
    "NES": {"nes"},
    "SNES": {"sfc"},
    "GameBoyColor": {"gbc"},
    "GameBoyAdvance": {"gba"},
    "DS": {"nds"},
    "Wii": {"wii"},
    "WiiU": {"wiu"},
    "Switch": {"swi"},
    "Xbox": {"xbo"},
    "Xbox360": {"xb3"},
    "XboxOne": {"xb1"},
}
DESKTOP_PLATFORMS = {"win", "mac", "lin", "dos", "ios", "and", "web", "dvd", "mob", "oth"}

MACRO_PLATFORM_PREFIX = {
    "MAC": "kPlatformMacintosh",
    "WIN": "kPlatformWindows",
    "PIP": "kPlatformPippin",
    "FMT": "kPlatformFMTowns",
    "SHK": "kPlatformShockwave",
}


def vndb_platform_ids(platform_names):
    """Map kPlatform* names to VNDB platform ids (best effort)."""
    result = set()
    for name in platform_names:
        suffix = name[len("kPlatform"):] if name.startswith("kPlatform") else name
        if suffix in PLATFORM_NAME_MAP:
            result |= PLATFORM_NAME_MAP[suffix]
        elif suffix:
            result.add(suffix.casefold())
    return result


def derive_state(flags):
    for flag, state in STATE_SEVERITY:
        if flag in flags:
            return state
    return DEFAULT_STATE


# ---------------------------------------------------------------------------
# VNDB data
# ---------------------------------------------------------------------------

def read_tsv(path: str, header_path: str | None = None):
    """Yield dict rows keyed by the dump header, or by index if absent."""
    columns = None
    if header_path and os.path.exists(header_path):
        with open(header_path, encoding="utf-8") as handle:
            first = handle.readline().rstrip("\n")
        if "\t" in first:
            columns = first.split("\t")
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if columns:
                yield dict(zip(columns, parts))
            else:
                yield parts


def dump_file(dump_dir: str, name: str) -> str:
    return os.path.join(dump_dir, "db", name)


def load_dump(dump_dir: str):
    """Load VNs from an extracted VNDB dump. Returns None if incomplete."""
    required = ["vn", "vn_titles", "releases", "releases_vn", "releases_platforms"]
    for name in required:
        if not os.path.exists(dump_file(dump_dir, name)):
            return None

    vns = {}
    for row in read_tsv(dump_file(dump_dir, "vn"), dump_file(dump_dir, "vn.header")):
        if isinstance(row, list):
            continue
        vn_id = row.get("id")
        if not vn_id:
            continue
        aliases = [
            alias for alias in row.get("alias", "").split("\\n") if alias.strip()
        ]
        vns[vn_id] = {
            "id": vn_id,
            "titles": [],
            "aliases": aliases,
            "platforms": set(),
            "released": None,
            "devstatus": row.get("devstatus", ""),
        }

    for row in read_tsv(
        dump_file(dump_dir, "vn_titles"), dump_file(dump_dir, "vn_titles.header")
    ):
        if isinstance(row, list):
            continue
        vn = vns.get(row.get("id"))
        if not vn:
            continue
        title = row.get("title")
        if title:
            vn["titles"].append(title)
        latin = row.get("latin")
        if latin and latin != "\\N" and latin not in vn["titles"]:
            vn["titles"].append(latin)

    # Official release platforms and earliest release date per VN.
    release_vn = {}
    for row in read_tsv(
        dump_file(dump_dir, "releases_vn"), dump_file(dump_dir, "releases_vn.header")
    ):
        if isinstance(row, list):
            continue
        release_vn.setdefault(row.get("id"), []).append(row.get("vid"))

    release_platforms = {}
    for row in read_tsv(
        dump_file(dump_dir, "releases_platforms"),
        dump_file(dump_dir, "releases_platforms.header"),
    ):
        if isinstance(row, list):
            continue
        release_platforms.setdefault(row.get("id"), set()).add(row.get("platform"))

    for row in read_tsv(
        dump_file(dump_dir, "releases"), dump_file(dump_dir, "releases.header")
    ):
        if isinstance(row, list):
            continue
        if row.get("official", "t") != "t":
            continue
        release_id = row.get("id")
        released = row.get("released") or ""
        platforms = release_platforms.get(release_id, set())
        for vn_id in release_vn.get(release_id, []):
            vn = vns.get(vn_id)
            if not vn:
                continue
            vn["platforms"].update(platforms)
            if released and released != "tba":
                if vn["released"] is None or released < vn["released"]:
                    vn["released"] = released

    return list(vns.values())


class VndbApi:
    """Minimal VNDB HTTP API client with an on-disk cache."""

    def __init__(self, dump_dir: str):
        self.cache_path = os.path.join(dump_dir, "api-cache.json")
        self.cache = {}
        if os.path.exists(self.cache_path):
            try:
                with open(self.cache_path, encoding="utf-8") as handle:
                    self.cache = json.load(handle)
            except (OSError, ValueError):
                self.cache = {}
        self.last_request = 0.0

    def _request(self, payload):
        key = json.dumps(payload, sort_keys=True)
        if key in self.cache:
            return self.cache[key]
        wait = 0.6 - (time.monotonic() - self.last_request)
        if wait > 0:
            time.sleep(wait)
        request = urllib.request.Request(
            VNDB_API,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=30) as response:
            body = json.loads(response.read().decode("utf-8"))
        self.last_request = time.monotonic()
        self.cache[key] = body
        with open(self.cache_path, "w", encoding="utf-8") as handle:
            json.dump(self.cache, handle)
        return body

    def search(self, title: str):
        payload = {
            "filters": ["search", "=", title],
            "fields": "id,title,alttitle,titles{title,latin,lang,official},"
            "aliases,released,platforms,devstatus",
            "results": 25,
        }
        body = self._request(payload)
        results = []
        for item in body.get("results", []):
            titles = [item.get("title") or ""]
            for entry in item.get("titles", []):
                titles.append(entry.get("title") or "")
                if entry.get("latin"):
                    titles.append(entry["latin"])
            results.append(
                {
                    "id": item.get("id"),
                    "titles": [title for title in titles if title],
                    "aliases": item.get("aliases") or [],
                    "platforms": set(item.get("platforms") or []),
                    "released": item.get("released"),
                    "devstatus": item.get("devstatus"),
                }
            )
        return results


class ApiIndex:
    """TitleIndex-compatible facade over the VNDB search API.

    The API path is slow: one rate-limited request per game title. Use it only
    with --engine when the dump is unavailable.
    """

    def __init__(self, api):
        self.api = api
        self.vns = {}

    def _search(self, title):
        results = self.api.search(title)
        for vn in results:
            self.vns.setdefault(vn["id"], vn)
        return results

    def exact(self, title):
        results = self._search(title)
        full = normalize(title)
        main = normalize(main_title(title))
        tier1 = set()
        tier2 = set()
        for vn in results:
            for candidate in list(vn["titles"]) + list(vn["aliases"]):
                candidate_full = normalize(candidate)
                candidate_main = normalize(main_title(candidate))
                if candidate_full and candidate_full == full:
                    tier1.add(vn["id"])
                elif main and (
                    candidate_full == main
                    or candidate_main == full
                    or candidate_main == main
                ):
                    tier2.add(vn["id"])
        if tier1:
            return 1, tier1
        if tier2:
            return 2, tier2
        return 0, set()

    def fuzzy(self, title, limit: int = 8):
        results = self._search(title)
        full = normalize(title)
        main = normalize(main_title(title))
        scored = []
        for vn in results:
            best = 0.0
            for candidate in list(vn["titles"]) + list(vn["aliases"]):
                candidate_full = normalize(candidate)
                candidate_main = normalize(main_title(candidate))
                for left, right in (
                    (full, candidate_full),
                    (main, candidate_main),
                    (full, candidate_main),
                ):
                    if left and right:
                        best = max(best, token_similarity(left, right))
            if best >= 0.8:
                scored.append((best, vn["id"]))
        scored.sort(key=lambda item: (-item[0], item[1]))
        return scored[:limit]


# ---------------------------------------------------------------------------
# Matching
# ---------------------------------------------------------------------------

class TitleIndex:
    def __init__(self, vns):
        self.vns = {vn["id"]: vn for vn in vns}
        self.full = {}
        self.main = {}
        self.token_postings = {}
        self.variants = {}
        for vn in vns:
            vn_variants = []
            for title in list(vn["titles"]) + list(vn["aliases"]):
                vn_full = normalize(title)
                if not vn_full:
                    continue
                vn_main = normalize(main_title(title))
                vn_variants.append((vn_full, vn_main))
                for normalized, table in ((vn_full, self.full), (vn_main, self.main)):
                    if not normalized:
                        continue
                    table.setdefault(normalized, set()).add(vn["id"])
                    for token in set(normalized.split()):
                        if len(token) >= 4:
                            self.token_postings.setdefault(token, set()).add(vn["id"])
            self.variants[vn["id"]] = vn_variants
        self._fuzzy_cache = {}

    def exact(self, title: str):
        full = normalize(title)
        main = normalize(main_title(title))
        tier1 = set(self.full.get(full, ()))
        if tier1:
            return 1, tier1
        tier2 = set(self.main.get(full, ())) | set(self.full.get(main, ()))
        tier2 |= set(self.main.get(main, ()))
        tier2.discard("")
        if tier2:
            return 2, tier2
        return 0, set()

    def fuzzy(self, title: str, limit: int = 8):
        full = normalize(title)
        if len(full) < 6 or len(full.split()) < 2:
            # Single-token or very short titles are exact-match territory;
            # fuzzy matching them only produces noise.
            return []
        cached = self._fuzzy_cache.get(full)
        if cached is not None:
            return cached
        main = normalize(main_title(title))
        tokens = [token for token in full.split() if len(token) >= 4]
        tokens.sort(key=lambda token: len(self.token_postings.get(token, ())))
        candidates = set()
        for token in tokens[:3]:
            postings = self.token_postings.get(token, ())
            if len(postings) > 4000:
                continue
            candidates |= set(postings)
            if len(candidates) > 300:
                break
        prefiltered = []
        for vn_id in candidates:
            best = 0.0
            for vn_full, vn_main in self.variants[vn_id]:
                for left, right in ((full, vn_full), (main, vn_main), (full, vn_main)):
                    if not left or not right:
                        continue
                    score = token_similarity(left, right)
                    if score > best:
                        best = score
            if best >= 0.8:
                prefiltered.append((best, vn_id))
        prefiltered.sort(key=lambda item: (-item[0], item[1]))
        scored = []
        for score, vn_id in prefiltered[:16]:
            best_ratio = 0.0
            for vn_full, vn_main in self.variants[vn_id]:
                for left, right in ((full, vn_full), (main, vn_main), (full, vn_main)):
                    if not left or not right:
                        continue
                    ratio = difflib.SequenceMatcher(None, left, right).ratio()
                    if ratio > best_ratio:
                        best_ratio = ratio
            if best_ratio >= 0.75:
                scored.append(((score + best_ratio) / 2.0, vn_id))
        scored.sort(key=lambda item: (-item[0], item[1]))
        result = scored[:limit]
        self._fuzzy_cache[full] = result
        return result


def load_reviews(path: str | None):
    reviews = {}
    if not path or not os.path.exists(path):
        return reviews
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            game_id, vn_id, verdict = parts[0], parts[1], parts[2].strip().lower()
            note = parts[3] if len(parts) > 3 else ""
            if verdict not in ("confirm", "reject"):
                continue
            reviews[(game_id, vn_id)] = (verdict, note)
    return reviews


def distinctive(title: str) -> bool:
    """A single generic word is not enough to confirm a match."""
    return len(normalize(title).split()) >= 2


def match_games(games, entries, index, reviews, engine_filter=None):
    """Return per-engine match records."""
    results = {}
    for engine in sorted(games):
        if engine_filter and engine not in engine_filter:
            continue
        engine_records = []
        for game_id, info in sorted(games[engine].items()):
            title = info["title"]
            tier, vn_ids = index.exact(title) if index else (0, set())
            candidates = []
            if tier == 0:
                candidates = index.fuzzy(title)
            for vn_id in sorted(vn_ids):
                candidates.append((1.0, vn_id))
            if not candidates:
                continue

            chosen = None
            if tier in (1, 2) and vn_ids:
                ordered = sorted(vn_ids)
                chosen = ordered[0]
                if engine == "director" and index:
                    for vn_id in ordered:
                        vn = index.vns[vn_id]
                        if vn["platforms"] & DIRECTOR_PLATFORMS:
                            chosen = vn_id
                            break
            elif tier == 3 and candidates:
                chosen = candidates[0][1]

            record = {
                "engine": engine,
                "id": game_id,
                "title": title,
                "file": info["file"],
                "flags": sorted(entries.get(engine, {}).get(game_id, {}).get("flags", ())),
                "platforms": sorted(
                    entries.get(engine, {}).get(game_id, {}).get("platforms", ())
                ),
                "tier": tier,
                "candidates": candidates,
                "vn_id": None,
                "review": None,
                "excluded": None,
                "weak_exact": tier == 1 and not distinctive(title),
            }

            candidate_ids = [vn_id for _score, vn_id in candidates]
            confirmed_review = None
            rejected = set()
            for vn_id in candidate_ids:
                verdict = reviews.get((game_id, vn_id))
                if not verdict:
                    continue
                if verdict[0] == "confirm" and confirmed_review is None:
                    confirmed_review = vn_id
                elif verdict[0] == "reject":
                    rejected.add(vn_id)
            all_rejected = bool(candidate_ids) and all(
                vn_id in rejected for vn_id in candidate_ids
            )
            # A "<id>\t*\treject" entry rejects every candidate at once.
            wildcard = reviews.get((game_id, "*"))
            if wildcard and wildcard[0] == "reject":
                all_rejected = True

            if confirmed_review:
                record["vn_id"] = confirmed_review
                record["review"] = "confirm"
            elif all_rejected:
                record["review"] = "reject"
            elif tier == 1 and not record["weak_exact"]:
                record["vn_id"] = chosen
            elif tier == 1 and chosen and index:
                # A generic single-word title needs a strong platform signal.
                strong = vndb_platform_ids(record["platforms"]) - DESKTOP_PLATFORMS
                if strong and index.vns[chosen]["platforms"] & strong:
                    record["vn_id"] = chosen
                    record["review"] = "platform"
            if not record["vn_id"] and record["review"] is None:
                record["review"] = "unreviewed"

            if record["vn_id"] and engine == "director" and index:
                vn = index.vns[record["vn_id"]]
                if not (vn["platforms"] & DIRECTOR_PLATFORMS):
                    record["excluded"] = "no win/mac release"
                    record["vn_id"] = None
            engine_records.append(record)
        if engine_records:
            results[engine] = engine_records
    return results


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def vn_summary(vn):
    if not vn:
        return ("", "", "", "")
    title = vn["titles"][0] if vn["titles"] else ""
    released = vn["released"] or ""
    platforms = ",".join(sorted(vn["platforms"]))
    devstatus = {"0": "finished", "1": "in dev", "2": "cancelled"}.get(
        str(vn["devstatus"]), str(vn["devstatus"])
    )
    return title, released, platforms, devstatus


def confidence_label(record):
    if record["vn_id"]:
        if record["tier"] == 1 and not record["weak_exact"]:
            return "confirmed (exact)"
        if record["review"] == "confirm":
            return "confirmed (reviewed)"
        if record["review"] == "platform":
            return "confirmed (platform)"
        return "confirmed"
    if record["review"] == "reject":
        return "rejected (reviewed)"
    if record["excluded"]:
        return "rejected ({})".format(record["excluded"])
    if record["tier"] == 1:
        return "candidate (weak exact)"
    if record["tier"] == 2:
        return "candidate (main title)"
    if record["tier"] == 3:
        return "candidate (fuzzy)"
    return "unmatched"


def is_confirmed(record):
    return bool(record["vn_id"])


def write_report(path, results, index, meta):
    lines = []
    lines.append("# VNDB audit report")
    lines.append("")
    lines.append("- ScummVM source: `{}`".format(meta["source"]))
    if meta.get("rev"):
        lines.append("- Source revision: `{}`".format(meta["rev"]))
    lines.append("- VNDB dump: `{}`".format(meta.get("dump") or "HTTP API"))
    if meta.get("dump_timestamp"):
        lines.append("- Dump timestamp: `{}`".format(meta["dump_timestamp"]))
    if meta.get("reviews"):
        lines.append("- Review file: `{}`".format(meta["reviews"]))
    lines.append("- Engines scanned: {}".format(meta["engine_count"]))
    lines.append("- Games scanned: {}".format(meta["game_count"]))
    lines.append("")
    lines.append(
        "The source tree is the upstream export, so fork-only engines (`smt`, "
        "`bibleblack`, `koihime_doki`) are not part of this audit."
    )
    lines.append("")
    lines.append(
        "Confidence: `confirmed (exact)` is a normalized full-title match; "
        "`confirmed (reviewed)` is a main-title or fuzzy match approved in the "
        "review file; `candidate` rows are listed for manual review only and "
        "are never written to a table."
    )
    lines.append("")

    confirmed_by_engine = {}
    for engine, records in results.items():
        confirmed = [record for record in records if is_confirmed(record)]
        if confirmed:
            confirmed_by_engine[engine] = confirmed

    total_confirmed = sum(len(rows) for rows in confirmed_by_engine.values())
    lines.append("## Summary")
    lines.append("")
    lines.append(
        "{} confirmed matches across {} engines.".format(
            total_confirmed, len(confirmed_by_engine)
        )
    )
    lines.append("")
    lines.append("| engine | confirmed | total ids scanned |")
    lines.append("|---|---:|---:|")
    scanned_by_engine = meta.get("scanned", {})
    for engine in sorted(results):
        scanned = scanned_by_engine.get(engine, len(results[engine]))
        confirmed = len(confirmed_by_engine.get(engine, ()))
        lines.append("| {} | {} | {} |".format(engine, confirmed, scanned))
    lines.append("")

    focus = {"smt", "director", "snatcher"}
    others = sorted(engine for engine in confirmed_by_engine if engine not in focus)
    lines.append("### Engines outside `smt` / `director` / `snatcher`")
    lines.append("")
    if others:
        lines.append(
            "{} engine(s) have confirmed VN matches beyond the fork's existing "
            "VNDB engines:".format(len(others))
        )
        lines.append("")
        lines.append(
            "| engine | confirmed matches | distinctive titles (3+ words) | confidence |"
        )
        lines.append("|---|---:|---:|---|")
        for engine in others:
            rows = confirmed_by_engine[engine]
            labels = sorted({confidence_label(row) for row in rows})
            distinctive_count = sum(
                1 for row in rows if len(normalize(row["title"]).split()) >= 3
            )
            lines.append(
                "| {} | {} | {} | {} |".format(
                    engine, len(rows), distinctive_count, ", ".join(labels)
                )
            )
        lines.append("")
        lines.append(
            "Caveat: these are normalized title-string matches, not proof that "
            "the same work is meant. A distinctive title of three or more "
            "words is unlikely to be coincidental; a short generic phrase is "
            "weak evidence. `ags` is the notable case: a large part of its "
            "catalog is indie/OELVN content, so many of its matches are "
            "probably the same games. `glk` is an interactive-fiction catalog "
            "and most of its matches are collisions. Confirm individual ids "
            "against the game before adding an engine to `KEEP`."
        )
    else:
        lines.append(
            "None. No engine outside `smt`, `director` and `snatcher` has a "
            "confirmed VNDB match."
        )
    lines.append("")

    lines.append("## Confirmed matches per engine")
    lines.append("")
    if not confirmed_by_engine:
        lines.append("No confirmed matches.")
        lines.append("")
    for engine in sorted(confirmed_by_engine):
        rows = confirmed_by_engine[engine]
        lines.append("### {}".format(engine))
        lines.append("")
        lines.append(
            "| engine | game id | ScummVM title | VNDB id | VNDB title | confidence |"
        )
        lines.append("|---|---|---|---|---|---|")
        for row in sorted(rows, key=lambda item: item["id"]):
            vn = index.vns.get(row["vn_id"]) if index else None
            vn_title = vn_summary(vn)[0]
            lines.append(
                "| {} | {} | {} | {} | {} | {} |".format(
                    engine,
                    row["id"],
                    row["title"].replace("|", "\\|"),
                    row["vn_id"],
                    vn_title.replace("|", "\\|"),
                    confidence_label(row),
                )
            )
        lines.append("")

    review_rows = [
        row
        for records in results.values()
        for row in records
        if not is_confirmed(row) and row["tier"] > 0
    ]
    lines.append("## Candidates for manual review")
    lines.append("")
    if not review_rows:
        lines.append("None.")
        lines.append("")
    else:
        lines.append(
            "Tier 2 is an exact main-title match; tier 3 is token similarity. "
            "Neither is used unless confirmed in the review file."
        )
        lines.append("")
        lines.append(
            "| engine | game id | ScummVM title | tier | VNDB candidates | status |"
        )
        lines.append("|---|---|---|---|---|---|")
        for row in sorted(review_rows, key=lambda item: (item["engine"], item["id"])):
            parts = []
            for _score, vn_id in row["candidates"][:4]:
                vn = index.vns.get(vn_id) if index else None
                title = vn_summary(vn)[0] if vn else ""
                platforms = ",".join(sorted(vn["platforms"])) if vn else ""
                parts.append("{} {} [{}]".format(vn_id, title, platforms))
            lines.append(
                "| {} | {} | {} | {} | {} | {} |".format(
                    row["engine"],
                    row["id"],
                    row["title"].replace("|", "\\|"),
                    row["tier"],
                    "; ".join(parts).replace("|", "\\|") or "-",
                    confidence_label(row),
                )
            )
        lines.append("")

    director_rows = results.get("director", ())
    director_confirmed = [row for row in director_rows if is_confirmed(row)]
    director_review = [row for row in director_rows if not is_confirmed(row)]
    lines.append("## Director candidate set")
    lines.append("")
    lines.append(
        "Filtered to VNDB games with a Windows or Macintosh release: {} "
        "confirmed id(s).".format(len(director_confirmed))
    )
    lines.append("")
    if director_confirmed:
        lines.append("| game id | ScummVM title | VNDB id | VNDB title | platforms | state |")
        lines.append("|---|---|---|---|---|---|")
        for row in sorted(director_confirmed, key=lambda item: item["id"]):
            vn = index.vns.get(row["vn_id"]) if index else None
            _title, _released, platforms, _devstatus = vn_summary(vn)
            lines.append(
                "| {} | {} | {} | {} | {} | {} |".format(
                    row["id"],
                    row["title"].replace("|", "\\|"),
                    row["vn_id"],
                    vn_summary(vn)[0].replace("|", "\\|"),
                    platforms,
                    derive_state(set(row["flags"])),
                )
            )
    lines.append("")
    if director_review:
        lines.append("Excluded or unreviewed Director ids:")
        lines.append("")
        for row in sorted(director_review, key=lambda item: item["id"]):
            lines.append(
                "- `{}` ({}) - {}".format(
                    row["id"], row["title"].replace("`", "'"), confidence_label(row)
                )
            )
        lines.append("")

    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# Director set and table writer
# ---------------------------------------------------------------------------

def director_set_rows(results, index):
    rows = []
    for row in results.get("director", ()):
        if not is_confirmed(row):
            continue
        vn = index.vns.get(row["vn_id"]) if index else None
        if vn and not (vn["platforms"] & DIRECTOR_PLATFORMS):
            continue
        rows.append(
            {
                "id": row["id"],
                "title": row["title"],
                "vn_id": row["vn_id"],
                "url": VNDB_VN_URL.format(row["vn_id"]),
                "state": derive_state(set(row["flags"])),
            }
        )
    return sorted(rows, key=lambda item: item["id"])


def write_director_set(path, rows):
    with open(path, "w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(
                "{}\t{}\t{}\t{}\t{}\n".format(
                    row["id"], row["title"], row["vn_id"], row["url"], row["state"]
                )
            )


def read_director_set(path):
    rows = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 5:
                continue
            rows[parts[0]] = {
                "id": parts[0],
                "title": parts[1],
                "vn_id": parts[2],
                "url": parts[3],
                "state": parts[4],
            }
    return rows


def c_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def find_table(text, masked, name):
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*\[\s*\]\s*=\s*\{")
    match = pattern.search(masked)
    if not match:
        return None
    body_start = match.end() - 1
    body_end = match_brace(masked, body_start)
    if body_end < 0:
        return None
    return body_start, body_end


def write_director_table(path, rows, dry_run=False):
    """Rewrite directorGames[]/gameDescriptions[] from the confirmed set."""
    text = read_text(path)
    stripped = strip_comments(text)
    masked = blank_strings(stripped)
    games_span = find_table(text, masked, "directorGames")
    desc_span = find_table(text, masked, "gameDescriptions")
    if not games_span or not desc_span:
        raise SystemExit(
            "{}: could not find directorGames[] and gameDescriptions[]".format(path)
        )

    desc_start, desc_end = desc_span
    kept_desc = []
    detection_ids = {}
    for entry_start, entry_end in split_entries(masked, desc_start + 1, desc_end):
        entry = text[entry_start:entry_end]
        literals = string_literals(stripped[entry_start:entry_end])
        if not literals:
            continue
        game_id = literals[0]
        if not game_id or game_id.startswith("AD_"):
            continue
        detection_ids.setdefault(game_id, 0)
        detection_ids[game_id] += 1
        if game_id in rows:
            kept_desc.append(entry.strip("\n").rstrip())

    games_start, games_end = games_span
    games_out = [
        '\t{ "director", "Macromedia Director Game", kUnknownGame, "" },'
    ]
    for game_id in sorted(rows):
        if game_id not in detection_ids:
            # Not hash-detectable upstream; drop it.
            print(
                "warning: {} has no detection entry, dropped".format(game_id),
                file=sys.stderr,
            )
            continue
        row = rows[game_id]
        state = row["state"]
        games_out.append(
            '\t{{ "{}", "{}", k{}Game, "{}" }},'.format(
                c_escape(game_id),
                c_escape(row["title"]),
                state,
                row["url"],
            )
        )
    games_out.append("\t{ 0, 0 }")
    games_body = "\n" + "\n".join(games_out) + "\n\n"

    desc_out = list(kept_desc)
    desc_out.append("\t{ AD_TABLE_END_MARKER, GID_GENERIC, 0 }")
    desc_body = "\n" + ",\n".join(desc_out) + "\n\n"

    # Replace from the end so earlier offsets stay valid.
    new_text = (
        text[:games_start + 1]
        + games_body
        + text[games_end:desc_start + 1]
        + desc_body
        + text[desc_end:]
    )
    if dry_run:
        sys.stdout.write(new_text)
        return
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(new_text)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def dump_timestamp(dump_dir):
    for path in (os.path.join(dump_dir, "TIMESTAMP"), dump_file(dump_dir, "TIMESTAMP")):
        if os.path.exists(path):
            return read_text(path).strip()
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog=PROG,
        description=(
            "Audit ScummVM detection tables against VNDB. Parses an exported "
            "ScummVM source tree and a VNDB database dump (or the VNDB API)."
        ),
        epilog=(
            "Review file format (one decision per line):\n"
            "  <scummvm id>\\t<vndb id>\\tconfirm|reject\\t<note>\n\n"
            "Only tier 1 matches and confirmed review entries enter the "
            "Director set."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--dump",
        metavar="DIR",
        help="directory with the extracted VNDB dump (db/vn, db/vn_titles, ...)",
    )
    parser.add_argument(
        "--source",
        metavar="DIR",
        required=True,
        help="root of an exported ScummVM source tree (contains engines/)",
    )
    parser.add_argument(
        "--report",
        metavar="FILE",
        help="write the markdown report here (default: stdout)",
    )
    parser.add_argument(
        "--director-set",
        metavar="FILE",
        help="write the confirmed Director id set as TSV",
    )
    parser.add_argument(
        "--write-director-table",
        metavar="FILE",
        help="rewrite directorGames[]/gameDescriptions[] in this file",
    )
    parser.add_argument(
        "--reviews",
        metavar="FILE",
        help="review decisions for tier 2/3 candidates (default: <dump>/reviews.tsv)",
    )
    parser.add_argument(
        "--engine",
        action="append",
        metavar="NAME",
        help="limit matching to this engine (repeatable)",
    )
    parser.add_argument(
        "--api",
        action="store_true",
        help="use the VNDB HTTP API even when a dump is available",
    )
    parser.add_argument(
        "--dump-dir",
        metavar="DIR",
        help="directory for the API cache when no dump is used",
    )
    parser.add_argument(
        "--source-rev",
        metavar="REV",
        help="source revision label to record in the report",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="with --write-director-table, print the result instead of writing",
    )
    args = parser.parse_args(argv)

    if not os.path.isdir(os.path.join(args.source, "engines")):
        parser.error("--source does not contain an engines/ directory")

    index = None
    api = None
    used_dump = False
    dump_dir = args.dump
    if dump_dir and not args.api:
        vns = load_dump(dump_dir)
        if vns:
            index = TitleIndex(vns)
            used_dump = True
        else:
            print(
                "warning: {} has no usable dump, falling back to the API".format(dump_dir),
                file=sys.stderr,
            )
    if index is None:
        cache_dir = dump_dir or args.dump_dir or os.getcwd()
        os.makedirs(cache_dir, exist_ok=True)
        api = VndbApi(cache_dir)
        index = ApiIndex(api)
        print(
            "warning: using the VNDB API, one rate-limited request per game "
            "title; consider --engine to limit the audit",
            file=sys.stderr,
        )

    reviews_path = args.reviews
    if not reviews_path and dump_dir:
        candidate = os.path.join(dump_dir, "reviews.tsv")
        if os.path.exists(candidate):
            reviews_path = candidate
    reviews = load_reviews(reviews_path)

    games = parse_descriptors(args.source)
    entries = parse_detection_entries(args.source)
    if args.engine:
        wanted = set(args.engine)
        games = {engine: value for engine, value in games.items() if engine in wanted}
        entries = {
            engine: value for engine, value in entries.items() if engine in wanted
        }

    results = match_games(games, entries, index, reviews, set(args.engine or ()))

    game_count = sum(len(value) for value in games.values())
    meta = {
        "scanned": {engine: len(value) for engine, value in games.items()},
        "source": args.source,
        "rev": args.source_rev,
        "dump": dump_dir if used_dump else None,
        "dump_timestamp": dump_timestamp(dump_dir) if used_dump else None,
        "reviews": reviews_path,
        "engine_count": len(games),
        "game_count": game_count,
    }

    if args.report:
        write_report(args.report, results, index, meta)
    else:
        write_report("/dev/stdout", results, index, meta)

    if args.director_set:
        rows = director_set_rows(results, index)
        write_director_set(args.director_set, rows)
        print(
            "wrote {} Director ids to {}".format(len(rows), args.director_set),
            file=sys.stderr,
        )

    if args.write_director_table:
        if not args.director_set:
            parser.error("--write-director-table requires --director-set")
        rows = read_director_set(args.director_set)
        write_director_table(args.write_director_table, rows, dry_run=args.dry_run)

    return 0


if __name__ == "__main__":
    sys.exit(main())
