@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d f:\godot
C:\Users\edele\AppData\Roaming\Python\Python313\Scripts\scons.exe platform=windows target=editor module_claude_enabled=yes scu_build=yes -j8
