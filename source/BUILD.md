# Building the plugin

The release DLL is built as a Windows x64 TesmioLoader plugin with Clang/LLVM and no CRT dependency.

## Requirements

- `clang-cl`
- `clang`
- `lld-link`

## Build

Run from the `source` directory:

```text
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:hdo.obj helicopter_distribution_office.cpp
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:tinycrt.obj tinycrt.cpp
clang --target=x86_64-pc-windows-msvc -c user32_import.s -o user32_import.obj
lld-link /nologo /dll /nodefaultlib /subsystem:windows /entry:DllMain /out:helicopter_distribution_office.dll hdo.obj tinycrt.obj user32_import.obj /include:__IMPORT_DESCRIPTOR_USER32
```

The resulting `helicopter_distribution_office.dll` is copied to `mod/plugins/` for release.
