# Third-party notices

Realistic Reflections - Mirrors contains modified Community Shaders framework and utility code.
The Fallout 4 adaptation includes changes to plugin startup, logging, hooks, shader preparation and build support.
These modifications and the mirror implementation are distributed under GNU GPL version 3,
with the additional permissions retained in [EXCEPTIONS](EXCEPTIONS). See [COPYING](COPYING).
This source snapshot includes modifications made through 20 September 2026.

- [Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) â€” project contributors; GPL-3.0-or-later with additional permissions. Upstream notices remain applicable to derived code.
- [CommonLibF4](https://github.com/alandtse/CommonLibF4) â€” runtime types and F4SE integration; MIT. The vendored source and its [license](extern/CommonLibF4/LICENSE) are included.
- [F4SE](https://f4se.silverlock.org/) â€” the Fallout 4 Script Extender runtime and plugin interface.
- [Mod Configuration Menu](https://www.nexusmods.com/fallout4/mods/21497) and [F4SE Menu Framework](https://github.com/DCCStudios/F4SEMenuFramework) â€” supported optional settings-menu hosts.

Build dependencies retain their individual licenses and copyright notices. Exact resolved versions and source
download references are recorded in [DEPENDENCIES.json](DEPENDENCIES.json); the build uses the pinned vcpkg
baseline and overlay ports included in this tree.

| Dependency | Version | License and notice |
| --- | --- | --- |
| [cppwinrt](https://github.com/microsoft/cppwinrt) | 2.0.250303.1 | [MIT](licenses/cppwinrt.txt) |
| [detours](https://github.com/microsoft/Detours) | 4.0.1#8 | [MIT](licenses/detours.txt) |
| [directxmath](https://github.com/Microsoft/DirectXMath) | 2025-04-03 | [MIT](licenses/directxmath.txt) |
| [directxtex](https://github.com/Microsoft/DirectXTex) | 2025-10-27 | [MIT](licenses/directxtex.txt) |
| [directxtk](https://github.com/Microsoft/DirectXTK) | 2025-10-27 | [MIT](licenses/directxtk.txt) |
| [eabase](https://github.com/electronicarts/EABase) | 2025-08-01 | [BSD-3-Clause](licenses/eabase.txt) |
| [eastl](https://github.com/electronicarts/EASTL) | 3.27.1 | [BSD-3-Clause](licenses/eastl.txt) |
| [fmt](https://github.com/fmtlib/fmt) | 10.2.1 | [MIT](licenses/fmt.txt) |
| [imgui](https://github.com/ocornut/imgui) | 1.90 | [MIT](licenses/imgui.txt) |
| [magic-enum](https://github.com/Neargye/magic_enum) | 0.9.7#1 | [MIT](licenses/magic-enum.txt) |
| [nlohmann-json](https://github.com/nlohmann/json) | 3.12.0#1 | [MIT](licenses/nlohmann-json.txt) |
| [rapidcsv](https://github.com/d99kris/rapidcsv/) | 8.90 | [BSD-3-Clause](licenses/rapidcsv.txt) |
| [rsm-mmio](https://github.com/Ryan-rsm-McKenzie/mmio) | 2.0.0 | [MIT](licenses/rsm-mmio.txt) |
| [spdlog](https://github.com/gabime/spdlog) | 1.17.0 | [MIT](licenses/spdlog.txt) |
| [stb](https://github.com/nothings/stb) | 2024-07-29#1 | [(MIT OR CC-PDDC)](licenses/stb.txt) |
| [unordered-dense](https://github.com/martinus/unordered_dense) | 4.8.1 | [MIT](licenses/unordered-dense.txt) |
| [xbyak](https://github.com/herumi/xbyak) | 7.28 | [BSD-3-Clause](licenses/xbyak.txt) |

Copyright notices in vendored files and dependency license texts are retained as supplied by their authors.
