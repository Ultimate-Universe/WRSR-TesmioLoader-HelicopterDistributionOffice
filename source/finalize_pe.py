#!/usr/bin/env python3
"""Finalize the HDO release DLL.

Adds a standard RT_VERSION resource containing Windows VERSIONINFO metadata and
writes a valid PE checksum. The script only operates on the plugin binary built
from the source in this directory; it does not patch the game executable.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

VERSION = (1, 2, 0, 0)
LANG_ID = 0x0409
CODEPAGE = 1200


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def utf16z(text: str) -> bytes:
    return text.encode("utf-16le") + b"\x00\x00"


def vi_block(key: str, *, value: bytes = b"", value_is_text: bool = False,
             children: list[bytes] | None = None, block_type: int = 0,
             value_length: int | None = None) -> bytes:
    children = children or []
    body = bytearray(b"\x00" * 6)
    body += utf16z(key)
    while len(body) % 4:
        body.append(0)

    if value:
        body += value
        while len(body) % 4:
            body.append(0)

    for child in children:
        body += child

    if value_length is None:
        if value_is_text:
            value_length = len(value) // 2
        else:
            value_length = len(value)

    struct.pack_into("<HHH", body, 0, len(body), value_length, block_type)
    return bytes(body)


def make_version_info() -> bytes:
    a, b, c, d = VERSION
    fixed = struct.pack(
        "<13I",
        0xFEEF04BD,       # dwSignature
        0x00010000,       # dwStrucVersion
        (a << 16) | b,    # dwFileVersionMS
        (c << 16) | d,    # dwFileVersionLS
        (a << 16) | b,    # dwProductVersionMS
        (c << 16) | d,    # dwProductVersionLS
        0x0000003F,       # dwFileFlagsMask
        0x00000000,       # dwFileFlags
        0x00040004,       # VOS_NT_WINDOWS32
        0x00000002,       # VFT_DLL
        0x00000000,       # dwFileSubtype
        0x00000000,       # dwFileDateMS
        0x00000000,       # dwFileDateLS
    )

    strings = {
        "CompanyName": "Ultimate-Universe",
        "FileDescription": "Helicopter Distribution Office - TesmioLoader Plugin",
        "FileVersion": "1.2.0",
        "InternalName": "helicopter_distribution_office",
        "OriginalFilename": "helicopter_distribution_office.dll",
        "ProductName": "Helicopter Distribution Office",
        "ProductVersion": "1.2.0",
    }

    string_entries = []
    for key, text in strings.items():
        value = utf16z(text)
        string_entries.append(
            vi_block(key, value=value, value_is_text=True, block_type=1)
        )

    string_table = vi_block(
        "040904B0", children=string_entries, block_type=1, value_length=0
    )
    string_file_info = vi_block(
        "StringFileInfo", children=[string_table], block_type=1, value_length=0
    )

    translation = struct.pack("<HH", LANG_ID, CODEPAGE)
    var = vi_block(
        "Translation", value=translation, block_type=0, value_length=len(translation)
    )
    var_file_info = vi_block(
        "VarFileInfo", children=[var], block_type=1, value_length=0
    )

    return vi_block(
        "VS_VERSION_INFO",
        value=fixed,
        block_type=0,
        value_length=len(fixed),
        children=[string_file_info, var_file_info],
    )


def make_resource_section(section_rva: int) -> bytes:
    version = make_version_info()

    # Three-level numeric resource tree:
    # RT_VERSION (16) -> resource id 1 -> language 0x0409 -> data entry.
    root_off = 0
    type_off = 24
    name_off = 48
    data_entry_off = 72
    version_off = align(88, 4)

    out = bytearray(version_off)

    def directory(offset: int, entry_id: int, child: int, is_directory: bool) -> None:
        struct.pack_into("<IIHHHH", out, offset, 0, 0, 0, 0, 0, 1)
        child_value = child | (0x80000000 if is_directory else 0)
        struct.pack_into("<II", out, offset + 16, entry_id, child_value)

    directory(root_off, 16, type_off, True)
    directory(type_off, 1, name_off, True)
    directory(name_off, LANG_ID, data_entry_off, False)

    struct.pack_into(
        "<IIII",
        out,
        data_entry_off,
        section_rva + version_off,
        len(version),
        CODEPAGE,
        0,
    )
    out += version
    return bytes(out)


def pe_checksum(data: bytes, checksum_offset: int) -> int:
    buf = bytearray(data)
    if checksum_offset + 4 <= len(buf):
        buf[checksum_offset:checksum_offset + 4] = b"\x00\x00\x00\x00"

    checksum = 0
    padded = bytes(buf) + b"\x00" * ((4 - len(buf) % 4) % 4)
    for i in range(0, len(padded), 4):
        dword = struct.unpack_from("<I", padded, i)[0]
        checksum = (checksum & 0xFFFFFFFF) + dword + (checksum >> 32)
        if checksum > 0xFFFFFFFF:
            checksum = (checksum & 0xFFFFFFFF) + (checksum >> 32)

    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum = checksum + (checksum >> 16)
    checksum = checksum & 0xFFFF
    checksum += len(buf)
    return checksum & 0xFFFFFFFF


def finalize(path: Path) -> None:
    data = bytearray(path.read_bytes())
    if data[:2] != b"MZ":
        raise SystemExit("Not a PE file")

    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise SystemExit("Invalid PE signature")

    coff_off = pe_off + 4
    machine, num_sections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, coff_off)
    if machine != 0x8664:
        raise SystemExit("Expected x86-64 PE")

    opt_off = coff_off + 20
    magic = struct.unpack_from("<H", data, opt_off)[0]
    if magic != 0x20B:
        raise SystemExit("Expected PE32+")

    section_alignment = struct.unpack_from("<I", data, opt_off + 0x20)[0]
    file_alignment = struct.unpack_from("<I", data, opt_off + 0x24)[0]
    size_headers = struct.unpack_from("<I", data, opt_off + 0x3C)[0]
    num_dirs = struct.unpack_from("<I", data, opt_off + 0x6C)[0]
    if num_dirs < 3:
        raise SystemExit("PE has no resource data-directory slot")

    sec_off = opt_off + opt_size
    first_raw = len(data)
    sections = []
    for i in range(num_sections):
        off = sec_off + i * 40
        name = bytes(data[off:off + 8]).rstrip(b"\x00")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, vsz, va, rsz, raw))
        if raw:
            first_raw = min(first_raw, raw)

    new_header_off = sec_off + num_sections * 40
    if new_header_off + 40 > min(size_headers, first_raw):
        raise SystemExit("No room for an additional PE section header")
    if any(name == b".rsrc" for name, *_ in sections):
        raise SystemExit("DLL already contains a .rsrc section")

    last = max(sections, key=lambda s: s[2])
    _, last_vsz, last_va, last_rsz, _ = last
    new_va = align(last_va + max(last_vsz, last_rsz), section_alignment)
    resource = make_resource_section(new_va)
    raw_size = align(len(resource), file_alignment)
    raw_ptr = align(len(data), file_alignment)

    if len(data) < raw_ptr:
        data += b"\x00" * (raw_ptr - len(data))
    data += resource
    data += b"\x00" * (raw_size - len(resource))

    # Section header.
    name = b".rsrc\x00\x00\x00"
    characteristics = 0x40000040  # initialized data, readable
    header = struct.pack(
        "<8sIIIIIIHHI",
        name,
        len(resource),
        new_va,
        raw_size,
        raw_ptr,
        0,
        0,
        0,
        0,
        characteristics,
    )
    data[new_header_off:new_header_off + 40] = header

    # Update COFF/optional header fields.
    struct.pack_into("<H", data, coff_off + 2, num_sections + 1)
    size_init = struct.unpack_from("<I", data, opt_off + 0x08)[0]
    struct.pack_into("<I", data, opt_off + 0x08, size_init + raw_size)
    struct.pack_into("<I", data, opt_off + 0x38, align(new_va + len(resource), section_alignment))

    resource_dir = opt_off + 0x70 + 2 * 8
    struct.pack_into("<II", data, resource_dir, new_va, len(resource))

    checksum_off = opt_off + 0x40
    struct.pack_into("<I", data, checksum_off, 0)
    checksum = pe_checksum(bytes(data), checksum_off)
    struct.pack_into("<I", data, checksum_off, checksum)

    path.write_bytes(data)
    print(f"Finalized {path.name}: .rsrc RVA=0x{new_va:X}, size={len(resource)}, checksum=0x{checksum:08X}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", nargs="?", default="helicopter_distribution_office.dll")
    args = parser.parse_args()
    finalize(Path(args.dll))


if __name__ == "__main__":
    main()
