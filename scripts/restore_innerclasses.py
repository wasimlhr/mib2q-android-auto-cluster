#!/usr/bin/env python3
"""Create a compile-only copy of a jxe2jar JAR with InnerClasses restored.

jxe2jar preserves nested names such as Outer$Inner but drops the InnerClasses
attribute that javac needs to resolve Outer.Inner.  The output is only a build
classpath input; it is never installed on the head unit.
"""

from __future__ import annotations

import argparse
import struct
import zipfile
from pathlib import Path


def read_constant_pool(data: bytes, offset: int):
    count = struct.unpack_from(">H", data, offset)[0]
    offset += 2
    pool = [None] * count
    index = 1
    while index < count:
        tag = data[offset]
        if tag == 1:
            length = struct.unpack_from(">H", data, offset + 1)[0]
            pool[index] = (1, data[offset + 3:offset + 3 + length])
            offset += 3 + length
        elif tag in (3, 4):
            offset += 5
        elif tag in (5, 6):
            offset += 9
            index += 1
        elif tag in (7, 8, 16, 19, 20):
            pool[index] = (tag, struct.unpack_from(">H", data, offset + 1)[0])
            offset += 3
        elif tag in (9, 10, 11, 12, 17, 18):
            offset += 5
        elif tag == 15:
            offset += 4
        else:
            raise ValueError(f"bad constant-pool tag {tag}")
        index += 1
    return count, pool, offset


def skip_members(data: bytes, offset: int) -> int:
    count = struct.unpack_from(">H", data, offset)[0]
    offset += 2
    for _ in range(count):
        offset += 6
        attribute_count = struct.unpack_from(">H", data, offset)[0]
        offset += 2
        for _ in range(attribute_count):
            length = struct.unpack_from(">I", data, offset + 2)[0]
            offset += 6 + length
    return offset


def patch_class(data: bytes, entries: list[tuple[str, str | None, str | None]]) -> bytes:
    count, pool, pool_end = read_constant_pool(data, 8)
    utf8 = {}
    classes = {}
    for index, entry in enumerate(pool):
        if entry and entry[0] == 1:
            utf8.setdefault(entry[1], index)
    for index, entry in enumerate(pool):
        if entry and entry[0] == 7:
            classes.setdefault(pool[entry[1]][1], index)

    extra = bytearray()
    next_index = [count]

    def add_utf8(value):
        raw = value.encode("utf-8") if isinstance(value, str) else value
        if raw in utf8:
            return utf8[raw]
        extra.extend(struct.pack(">BH", 1, len(raw)) + raw)
        utf8[raw] = next_index[0]
        next_index[0] += 1
        return utf8[raw]

    def add_class(value: str):
        raw = value.encode("utf-8")
        if raw in classes:
            return classes[raw]
        name_index = add_utf8(raw)
        extra.extend(struct.pack(">BH", 7, name_index))
        classes[raw] = next_index[0]
        next_index[0] += 1
        return classes[raw]

    rows = [
        struct.pack(">HHHH", add_class(inner), add_class(outer) if outer else 0,
                    add_utf8(simple) if simple else 0, 0x0009)
        for inner, outer, simple in entries
    ]
    attribute_name = add_utf8("InnerClasses")

    offset = pool_end + 6
    interface_count = struct.unpack_from(">H", data, offset)[0]
    offset += 2 + 2 * interface_count
    offset = skip_members(data, offset)
    offset = skip_members(data, offset)
    attributes_offset = offset
    attribute_count = struct.unpack_from(">H", data, offset)[0]
    offset += 2
    kept = bytearray()
    for _ in range(attribute_count):
        name_index, length = struct.unpack_from(">HI", data, offset)
        if pool[name_index] is None or pool[name_index][1] != b"InnerClasses":
            kept.extend(data[offset:offset + 6 + length])
        offset += 6 + length

    body = struct.pack(">H", len(rows)) + b"".join(rows)
    attribute = struct.pack(">HI", attribute_name, len(body)) + body
    kept_count = 0
    position = 0
    while position < len(kept):
        length = struct.unpack_from(">I", kept, position + 2)[0]
        position += 6 + length
        kept_count += 1

    output = bytearray(data[:8])
    output += struct.pack(">H", next_index[0])
    output += data[10:pool_end] + extra
    output += data[pool_end:attributes_offset]
    output += struct.pack(">H", kept_count + 1) + kept + attribute
    return bytes(output)


def restore(source: Path, destination: Path) -> tuple[int, int]:
    with zipfile.ZipFile(source) as input_jar:
        class_names = {name[:-6] for name in input_jar.namelist() if name.endswith(".class")}
        declarations: dict[str, list[tuple[str, str | None, str | None]]] = {}
        for name in sorted(class_names):
            base = name.rsplit("/", 1)[-1]
            if "$" not in base:
                continue
            outer, simple = name.rsplit("$", 1)
            if outer not in class_names:
                continue
            entry = (name, None if simple.isdigit() else outer, None if simple.isdigit() else simple)
            declarations.setdefault(name, []).append(entry)
            declarations.setdefault(outer, []).append(entry)
            ancestor = outer
            while "$" in ancestor.rsplit("/", 1)[-1]:
                parent, ancestor_simple = ancestor.rsplit("$", 1)
                if parent not in class_names:
                    break
                declarations[name].append((ancestor, parent, ancestor_simple))
                ancestor = parent

        patched = skipped = 0
        with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output_jar:
            for item in input_jar.infolist():
                data = input_jar.read(item.filename)
                key = item.filename[:-6]
                if item.filename.endswith(".class") and key in declarations:
                    try:
                        data = patch_class(data, declarations[key])
                        patched += 1
                    except (IndexError, KeyError, struct.error, ValueError) as exc:
                        skipped += 1
                        print(f"skip {key}: {exc}")
                output_jar.writestr(item, data)
    return patched, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    patched, skipped = restore(args.source, args.destination)
    print(f"patched {patched} classes, skipped {skipped}")
    return 1 if skipped else 0


if __name__ == "__main__":
    raise SystemExit(main())
