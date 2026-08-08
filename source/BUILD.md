# Building the plugin

The release DLL is a native Windows x64 TesmioLoader plugin built with LLVM and no C runtime dependency.

## Requirements

- `clang-cl`
- `clang`
- `lld-link`
- Python 3

## Build

From the `source` directory on Windows, run:

```text
build_release.bat
```

Equivalent commands:

```text
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:hdo.obj helicopter_distribution_office.cpp
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:tinycrt.obj tinycrt.cpp
clang --target=x86_64-pc-windows-msvc -c user32_import.s -o user32_import.obj
lld-link /nologo /dll /nodefaultlib /subsystem:windows /entry:DllMain /out:helicopter_distribution_office.dll hdo.obj tinycrt.obj user32_import.obj /include:__IMPORT_DESCRIPTOR_USER32
python finalize_pe.py helicopter_distribution_office.dll
```

`finalize_pe.py` adds the standard Windows `VERSIONINFO` resource for v1.1.0 and calculates the PE checksum. It modifies only the plugin DLL produced by this build; it does not modify `SOVIET64.exe`.

Copy the completed DLL to `mod/plugins/helicopter_distribution_office.dll` for release.
