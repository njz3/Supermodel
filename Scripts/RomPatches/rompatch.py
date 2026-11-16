"""
Supermodel's ROM patches (Src/RomPatches.h): an XML file a patch, its changes
to the game's ROM files as bytes at offsets (none of the game's own data:
each file named with its CRC32), always or by option, with descriptions,
requirements and conflicts. Supermodel lists them in Game Options, applies
them as the ROMs load and writes the patched files out (to program EPROMs).

    patch = Patch("forest", "FOREST, the hidden fifth course", "What it does.")
    patch.base = diff(original_files, patched_files)        # {file name: (crc32, [(offset, bytes)])}
    patch.options.append(Option("rain", "Rain", "...", default=True,
                                changes=diff(patched_files, with_rain_files)))
    patch.write("Assets/Patches/srally2/forest.xml")

Files are {name: bytes}; diff() keeps only the bytes that differ, by runs.
"""

import zlib
from xml.sax.saxutils import escape, quoteattr

__all__ = ["Patch", "Option", "Value", "diff", "merge"]


def diff(before, after, original=None):
    """{name: (crc32, [(offset, bytes)])} for the files that differ (runs of differing bytes); the CRC32 the game's file
    (original's, before's when not given: an option's changes are made over the base's, for the game's files)"""
    original = original or before
    out = {}
    for name, old in before.items():
        new = after[name]
        if len(old) != len(new):
            raise ValueError("%s: the patched file's size differs" % name)
        if old == new:
            continue
        runs, at, n = [], 0, len(old)
        while at < n:
            if old[at] == new[at]:
                at += 1
                continue
            start = at
            while at < n and old[at] != new[at]:
                at += 1
            runs.append((start, bytes(new[start:at])))
        out[name] = (zlib.crc32(original[name]) & 0xFFFFFFFF, runs)
    return out


def merge(*changes):
    """Several diffs of the same original files as one (they must not overlap)"""
    out = {}
    for c in changes:
        for name, (crc, runs) in c.items():
            if name in out and out[name][0] != crc:
                raise ValueError("%s: diffs of different files" % name)
            out.setdefault(name, (crc, []))[1].extend(runs)
    for name, (crc, runs) in out.items():
        runs.sort()
        for (a, x), (b, _) in zip(runs, runs[1:]):
            if a + len(x) > b:
                raise ValueError("%s: changes overlap at 0x%X" % (name, b))
    return out


def _files_xml(changes, indent):
    pad = " " * indent
    lines = []
    for name in sorted(changes):
        crc, runs = changes[name]
        lines.append('%s<file name=%s crc32="%08X">' % (pad, quoteattr(name), crc))
        for offset, data in runs:
            text = data.hex().upper()
            lines.append('%s  <data offset="%X">%s</data>' % (pad, offset, text))
        lines.append("%s</file>" % pad)
    return lines


def _text(tag, text, indent):
    if not text:
        return []
    pad = " " * indent
    body = "\n".join(pad + "  " + line if line else "" for line in text.strip().split("\n"))
    return ["%s<%s>" % (pad, tag), escape(body), "%s</%s>" % (pad, tag)]


class Value:
    def __init__(self, name, title, description="", changes=None):
        self.name, self.title, self.description, self.changes = name, title, description, changes or {}


class Option:
    """On or off (default True or False), or with values a choice among them (default a value's name)"""

    def __init__(self, name, title, description="", default=False, changes=None, values=None, requires=(), conflicts=()):
        self.name, self.title, self.description = name, title, description
        self.default, self.changes, self.values = default, changes or {}, values or []
        self.requires, self.conflicts = list(requires), list(conflicts)


class Patch:
    def __init__(self, name, title, description="", requires=(), conflicts=(), comment=""):
        self.name, self.title, self.description, self.comment = name, title, description, comment
        self.requires, self.conflicts = list(requires), list(conflicts)
        self.base, self.options = {}, []

    def check(self):
        """No two changes on the same bytes (the base, the options, each choice's values against the rest)"""
        everything = [self.base] + [o.changes for o in self.options]
        merge(*everything)
        for o in self.options:
            for v in o.values:
                merge(*(everything + [v.changes]))

    def xml(self):
        self.check()
        out = ['<?xml version="1.0" encoding="utf-8"?>']
        if self.comment:
            out.append("<!-- %s -->" % self.comment.replace("--", "- -"))
        out.append("<patch name=%s title=%s>" % (quoteattr(self.name), quoteattr(self.title)))
        out += _text("description", self.description, 2)
        out += ["  <requires>%s</requires>" % escape(n) for n in self.requires]
        out += ["  <conflicts>%s</conflicts>" % escape(n) for n in self.conflicts]
        out += _files_xml(self.base, 2)
        for o in self.options:
            default = o.default if o.values else ("true" if o.default else "false")
            out.append("  <option name=%s title=%s default=%s>" % (quoteattr(o.name), quoteattr(o.title), quoteattr(default)))
            out += _text("description", o.description, 4)
            out += ["    <requires>%s</requires>" % escape(n) for n in o.requires]
            out += ["    <conflicts>%s</conflicts>" % escape(n) for n in o.conflicts]
            out += _files_xml(o.changes, 4)
            for v in o.values:
                out.append("    <value name=%s title=%s>" % (quoteattr(v.name), quoteattr(v.title)))
                out += _text("description", v.description, 6)
                out += _files_xml(v.changes, 6)
                out.append("    </value>")
            out.append("  </option>")
        out.append("</patch>")
        return "\n".join(out) + "\n"

    def write(self, path):
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(self.xml())
