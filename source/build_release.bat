@echo off
setlocal

clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:hdo.obj helicopter_distribution_office.cpp || exit /b 1
clang-cl /nologo /c /O2 /GS- /GR- /EHs-c- /Zl /Fo:tinycrt.obj tinycrt.cpp || exit /b 1
clang --target=x86_64-pc-windows-msvc -c user32_import.s -o user32_import.obj || exit /b 1
lld-link /nologo /dll /machine:x64 /nodefaultlib /subsystem:windows /entry:DllMain /dynamicbase /highentropyva /nxcompat /out:helicopter_distribution_office.dll hdo.obj tinycrt.obj user32_import.obj /include:__IMPORT_DESCRIPTOR_USER32 || exit /b 1
python finalize_pe.py helicopter_distribution_office.dll || exit /b 1

echo Built helicopter_distribution_office.dll v1.2.0
endlocal
