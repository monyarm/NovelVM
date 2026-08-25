#!/usr/bin/env python3
# Deterministically rewrite Git paths containing 'scummvm' (any casing) to
# 'novelvm' between two refs, using git fast-export/fast-import.
#
# Modes:
#   rewrite <src-range> <dst-ref>   Generate/replace <dst-ref> from <src-range>.
#                                   <src-range> must be "<base>..<tip>" so the
#                                   rewritten history grafts onto <base>.
#   validate <tree-ish>             Walk a tree and verify the path-rename rule
#                                   produces no collisions.
#
# The transformation applies to path components only, never file contents.
# Collisions abort with a nonzero exit status; nothing is overwritten.

import subprocess
import sys

OLD = "scummvm"
NEW = "novelvm"


def die(msg):
    sys.stderr.write("fatal: %s\n" % msg)
    sys.exit(1)


def map_component(comp):
    """Rename one path component, preserving per-character casing."""
    out = []
    idx = 0
    cl = comp.lower()
    while True:
        pos = cl.find(OLD, idx)
        if pos < 0:
            break
        out.append(comp[idx:pos])
        out.append("".join(
            n.upper() if c.isupper() else n
            for c, n in zip(comp[pos:pos + len(OLD)], NEW)))
        idx = pos + len(OLD)
    out.append(comp[idx:])
    return "".join(out)


def map_path(path):
    return "/".join(map_component(c) if OLD in c.lower() else c
                    for c in path.split("/"))


def c_unquote(raw):
    """Undo git's C-style path quoting. Input/output are bytes."""
    if not (raw.startswith(b'"') and raw.endswith(b'"')):
        return raw
    body = raw[1:-1]
    res = bytearray()
    i = 0
    while i < len(body):
        ch = body[i:i + 1]
        if ch != b"\\":
            res += ch
            i += 1
            continue
        nxt = body[i + 1:i + 2]
        simple = {b'"': b'"', b"\\": b"\\"}
        if nxt in simple:
            res += simple[nxt]
            i += 2
        elif nxt == b"n":
            res += b"\n"
            i += 2
        elif nxt == b"t":
            res += b"\t"
            i += 2
        else:
            res.append(int(body[i + 1:i + 4], 8))
            i += 4
    return bytes(res)


def needs_quoting(text):
    return any(c in text for c in ' \t"\\\n') or any(ord(c) > 127 for c in text)


def c_quote(text):
    out = ['"']
    for ch in text:
        if ch in '"\\':
            out.append("\\" + ch)
        elif ord(ch) > 127:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return "".join(out) + '"'


def map_raw(raw):
    """Apply the rename rule to one fast-export path field (bytes -> bytes)."""
    text = c_unquote(raw).decode("utf-8", "surrogateescape")
    mapped = map_path(text)
    if needs_quoting(mapped):
        return c_quote(mapped).encode("ascii")
    return mapped.encode("utf-8", "surrogateescape")


class StreamRewriter:
    """Rewrites file paths in a git fast-export (--no-data) byte stream."""

    def __init__(self, dst_ref):
        self.dst_ref = dst_ref
        self.out = bytearray()
        self.commit_desc = "?"
        self.marks = {}       # dest -> dataref within current commit
        self.collisions = []

    def emit(self, line):
        self.out += line + b"\n"

    def flush(self):
        sys.stdout.buffer.write(self.out)
        sys.stdout.buffer.flush()
        self.out = bytearray()

    def check_collision(self, dest, dataref):
        prev = self.marks.get(dest)
        if prev is not None and prev != dataref:
            self.collisions.append(
                "%s: '%s' produced by conflicting sources (%s vs %s)"
                % (self.commit_desc,
                   dest.decode("utf-8", "replace"),
                   prev.decode("utf-8", "replace"),
                   dataref.decode("utf-8", "replace")))
        self.marks[dest] = dataref

    def rewrite(self):
        inp = sys.stdin.buffer
        pending = []          # buffered file-command lines of current commit
        in_commit = False
        while True:
            raw_line = inp.readline()
            if not raw_line:
                break  # EOF
            line = raw_line.rstrip(b"\n")
            if line.startswith(b"data "):
                n = int(line[5:])
                payload = inp.read(n)
                if len(payload) != n:
                    die("truncated data block")
                self.emit(line)
                self.out += payload + b"\n"
                continue
            if line.startswith(b"commit "):
                pending, in_commit = self.flush_buffered(pending), True
                self.marks.clear()
                self.commit_desc = line.decode("utf-8", "replace")
                self.emit(b"commit " + self.dst_ref)
                continue
            if not in_commit:
                self.emit(line)
                continue
            if line.startswith((b"from ", b"merge ", b"author ",
                                b"committer ", b"mark ", b"encoding ",
                                b"gpgsig")):
                self.emit(line)
                continue
            if line.startswith(b"M "):
                parts = line.split(b" ", 3)
                if len(parts) != 4:
                    die("malformed M line: %r" % line)
                _, mode, dataref, raw = parts
                dest = map_raw(raw)
                self.check_collision(dest, dataref)
                pending.append(b"M " + mode + b" " + dataref + b" " + dest)
                continue
            if line.startswith(b"D "):
                pending.append(b"D " + map_raw(line[2:]))
                continue
            if line.startswith(b"R ") or line.startswith(b"C "):
                src, dst = split_two_paths(line[2:])
                pending.append(
                    line[:1] + b" " + map_raw(src) + b" " + map_raw(dst))
                continue
            if line == b"deleteall":
                pending, in_commit = self.flush_buffered(pending), True
                self.marks.clear()
                self.emit(line)
                continue
            # Any other command ends this commit's file-command section.
            pending, in_commit = self.flush_buffered(pending), False
            self.emit(line)
        self.flush_buffered(pending)
        self.flush()
        if self.collisions:
            for c in self.collisions:
                sys.stderr.write("collision: %s\n" % c)
            die("path-mapping collisions detected")

    def flush_buffered(self, pending):
        for l in pending:
            self.emit(l)
        return []


def split_two_paths(rest):
    # fast-export separates two quoted/unquoted paths with " ".
    if rest.startswith(b'"'):
        depth = 0
        for i in range(len(rest)):
            ch = rest[i:i + 1]
            if ch == b'"':
                depth ^= 1
            elif ch == b" " and depth == 0:
                return rest[:i], rest[i + 1:]
        die("unterminated quoted path: %r" % rest)
    sp = rest.find(b" ")
    if sp < 0:
        die("malformed two-path command: %r" % rest)
    return rest[:sp], rest[sp + 1:]


class TreeValidator:
    """Walks a tree applying the rename rule and reports collisions."""

    def __init__(self):
        self.errors = []
        self.renamed = 0
        self.total = 0

    def validate_tree(self, treeish):
        self.walk(treeish, "")

    def walk(self, treeish, prefix):
        proc = subprocess.run(["git", "ls-tree", "-z", treeish],
                              capture_output=True)
        if proc.returncode != 0:
            die("cannot read tree %s" % treeish)
        ls = proc.stdout
        rows = [r for r in ls.split(b"\0") if r]
        entries = {}
        for row in rows:
            meta, name = row.split(b"\t", 1)
            mode, otype, oid = meta.split(b" ")
            entries[name] = (otype, oid)
            if otype == b"tree":
                self.walk(oid, prefix + name.decode() + "/")
        mapped = {}
        for name, (otype, oid) in sorted(entries.items()):
            text = name.decode("utf-8", "surrogateescape")
            new_text = map_path(text) if OLD in text.lower() else text
            if otype != b"blob":
                continue
            self.total += 1
            if new_text != text:
                self.renamed += 1
            new = new_text.encode("utf-8", "surrogateescape")
            if new in mapped and mapped[new][1] != oid:
                self.errors.append(
                    "%s%s: two source names map here (%s and %s)"
                    % (prefix, new_text, mapped[new][0], text))
            if new_text != text and new in entries and \
                    entries[new][1] != oid:
                self.errors.append(
                    "%s%s: rename target already exists with different "
                    "content (source %s)" % (prefix, new_text, text))
            mapped[new] = (text, oid)


def main(argv):
    if len(argv) == 4 and argv[1] == "rewrite":
        StreamRewriter(argv[3].encode()).rewrite()
        return 0
    if len(argv) == 3 and argv[1] == "validate":
        v = TreeValidator()
        v.validate_tree(argv[2])
        if v.errors:
            for e in v.errors:
                sys.stderr.write("collision: %s\n" % e)
            die("validation failed")
        print("validated %d blobs, %d renamed paths, no collisions"
              % (v.total, v.renamed))
        return 0
    die("usage: rewrite-upstream-paths.py rewrite <base>..<tip> <dst-ref>\n"
        "       rewrite-upstream-paths.py validate <tree-ish>")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
