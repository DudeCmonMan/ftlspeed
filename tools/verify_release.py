"""Fail the build rather than ship a broken proxy."""

import os
import struct
import sys

PROXY = os.path.join("bin", "dbghelp.dll")
SYSTEM = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "SysWOW64", "dbghelp.dll")
REQUIRED = [
    "StackWalk", "SymInitialize", "SymCleanup",
    "SymGetSymFromAddr", "SymGetModuleBase", "SymFunctionTableAccess",
]

failures = []


def check(ok, label, detail=""):
    print("  %-4s %s%s" % ("PASS" if ok else "FAIL", label, (" - " + detail) if detail else ""))
    if not ok:
        failures.append(label)


def parse(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    machine, sections = struct.unpack_from("<HH", data, pe + 4)
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    directories = pe + 24 + (112 if magic == 0x20B else 96)

    table = []
    base = pe + 24 + opt_size
    for i in range(sections):
        offset = base + i * 40
        vsize, vaddr, rsize, raw = struct.unpack_from("<IIII", data, offset + 8)
        table.append((vaddr, vsize, raw, rsize))

    def to_offset(rva):
        for vaddr, vsize, raw, rsize in table:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raw + (rva - vaddr)
        return None

    def read_string(offset):
        return data[offset:data.index(b"\0", offset)].decode("latin1")

    imports = []
    rva = struct.unpack_from("<I", data, directories + 8)[0]
    if rva:
        cursor = to_offset(rva)
        while True:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", data, cursor)
            if oft == 0 and name_rva == 0 and ft == 0:
                break
            imports.append(read_string(to_offset(name_rva)))
            cursor += 20

    names, count, ordinal_base = set(), 0, 0
    rva, _ = struct.unpack_from("<II", data, directories)
    if rva:
        export = to_offset(rva)
        ordinal_base, count, name_count = struct.unpack_from("<III", data, export + 16)
        _, name_table, _ = struct.unpack_from("<III", data, export + 28)
        for i in range(name_count):
            entry = struct.unpack_from("<I", data, to_offset(name_table) + i * 4)[0]
            names.add(read_string(to_offset(entry)))

    return machine, imports, names, count, ordinal_base


def main():
    if not os.path.exists(PROXY):
        print("FAIL %s not found - did build.bat run?" % PROXY)
        return 1

    machine, imports, names, count, ordinal_base = parse(PROXY)
    print("%s (%d bytes)" % (PROXY, os.path.getsize(PROXY)))

    check(machine == 0x14C, "32-bit x86", "machine=0x%04X" % machine)

    lowered = [d.lower() for d in imports]
    crt = [d for d in lowered
           if "vcruntime" in d or "msvcp" in d or d.startswith("api-ms-win-crt")]
    check(not crt, "static CRT, no runtime DLL dependency", ", ".join(crt))
    check(not any("opengl32" in d for d in lowered),
          "opengl32 resolved at runtime, not statically imported")
    check(bool(imports), "has an import table", ", ".join(imports))

    missing = [n for n in REQUIRED if n not in names]
    check(not missing, "exports the 6 functions FTL imports", ", ".join(missing))

    if os.path.exists(SYSTEM):
        _, _, sys_names, sys_count, sys_base = parse(SYSTEM)
        check((count, ordinal_base) == (sys_count, sys_base),
              "export count and ordinal base match the system DLL",
              "proxy %d/@%d vs system %d/@%d" % (count, ordinal_base, sys_count, sys_base))
        check(names == sys_names, "named export set matches the system DLL",
              "%d differ" % len(names ^ sys_names))
    else:
        check(False, "system dbghelp.dll found for comparison", SYSTEM)

    print()
    if failures:
        print("FAILED: " + "; ".join(failures))
        return 1
    print("All checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
