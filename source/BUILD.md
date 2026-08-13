# Building the plugin

The v1.2.0 release DLL is a native Windows x64 TesmioLoader plugin. It is
built without the Microsoft C runtime and contains a conventional PE import
table, x64 unwind metadata and embedded Windows version information.

## Requirements

- LLVM `clang-cl`
- LLVM `clang`
- LLVM `lld-link`
- Python 3

The tools must be available on `PATH` in a Windows command prompt.

## Build

Run the following from the `source` directory:

```text
build_release.bat
```

The script performs these steps:

1. Compiles the plugin and minimal no-CRT memory helpers.
2. Builds the small USER32 import descriptor used for `GetAsyncKeyState`.
3. Links a native x64 DLL with ASLR, high-entropy VA and NX compatibility.
4. Adds the v1.2.0 `VERSIONINFO` resource and calculates a nonzero PE checksum.

Equivalent commands:

```text
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:hdo.obj helicopter_distribution_office.cpp
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:tinycrt.obj tinycrt.cpp
clang --target=x86_64-pc-windows-msvc -c user32_import.s -o user32_import.obj
lld-link /nologo /dll /machine:x64 /nodefaultlib /subsystem:windows /entry:DllMain /dynamicbase /highentropyva /nxcompat /out:helicopter_distribution_office.dll hdo.obj tinycrt.obj user32_import.obj /include:__IMPORT_DESCRIPTOR_USER32
python finalize_pe.py helicopter_distribution_office.dll
```

The completed DLL is `source/helicopter_distribution_office.dll`. Copy it to
`mod/plugins/helicopter_distribution_office.dll` when assembling a release.

No external manifest file, manifest injection step, generated source file or
post-build patch to `SOVIET64.exe` is required.

## Expected plugin exports

- `TsmPluginApiVersion`
- `TsmPluginInit`
- `TsmPluginStart`

Intermediate `.obj`, `.lib`, `.exp`, `.pdb` and locally built DLL files belong
only in the developer working directory and are excluded by the repository
`.gitignore`.
