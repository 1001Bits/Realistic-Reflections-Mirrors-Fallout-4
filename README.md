# Realistic Reflections - Mirrors for Fallout 4

Planar mirror rendering, reflected player animation, workshop mirrors and in-game quality controls for Fallout 4.
The universal plugin targets Fallout 4 1.10.163, Fallout 4 1.11.240 and Fallout 4 VR 1.2.72.

## Build

Build on Windows x64 with Visual Studio 2022's Desktop development with C++ workload, a Windows SDK,
CMake 3.21 or newer, Python 3 and vcpkg. Set `VCPKG_ROOT` to your vcpkg checkout.
The manifest pins its dependency baseline; required overlay ports and CommonLibF4 sources are included.

From this directory:

```powershell
cmake --preset ALL
cmake --build --preset ALL
cmake --install build/ALL --config Release --prefix build/package
```

The plugin is `build/ALL/Release/RealisticReflectionsMirrors.dll`. Installation stages the flat-game assets
and compiles the mirror shaders. The optional VR material is staged separately with:

```powershell
cmake --install build/ALL --config Release --prefix build/package-vr --component VRAssets
```

The Creation Kit helper builds by default. Its asset checks run with:

```powershell
ctest --test-dir build/ALL/tools/ck -C Release --output-on-failure
```

Set `BUILD_MIRROR_CK_HELPER=OFF` at configuration time to build without the authoring helper.
Use the matching F4SE runtime for the installed game. Launch F4SE with the game directory as its working directory.

## Quality controls

On flat Fallout 4, settings are available through MCM or the optional
[F4SE Menu Framework](https://github.com/DCCStudios/F4SEMenuFramework).
In Menu Framework, open **Realistic Reflections - Mirrors > Settings**. Both menus
use `Data/MCM/Settings/MirrorsOfFallout.ini`; Menu Framework works without MCM installed.
Changes save and apply when the framework menu closes, or when **Apply** is pressed.
Inactive controls are hidden, and switching modes preserves manual preferences.

Choose Automatic, Preset or Manual. Automatic tests resolution reductions and restores detail when they
do not improve measured frame times. Under sustained pressure it can use Low's reduced shadow policy and
scene distances. The flat-game renderer retains the same nearby movement refresh behaviour as Low.

On the flat-game renderer, Automatic and presets share refresh limits based on the pane's longest dimension in
output pixels. After one second at 192 pixels or smaller, the limit is 30 Hz; at 64 pixels or smaller,
it is 15 Hz. Faster refresh resumes immediately above 256 and 96 pixels respectively. These separate
entry and exit thresholds prevent oscillation. A pane visible through another mirror retains its configured
rate because its size in the main camera cannot describe its reflected size. Manual refresh settings remain explicit.

Flat-game mirrors confirmed hidden by scene depth suspend capture. Visibility checks are asynchronous and never wait
for the GPU. The previous complete reflection remains available while visibility changes.

## Source layout

- `src/`, `include/`: plugin, renderer and runtime support.
- `features/`: shader sources and feature configuration.
- `package-mirrors/`, `package-mirrors-vr/`: runtime assets and menu defaults.
- `tools/`: shader compilation, asset generation and Creation Kit helper sources.
- `extern/`, `cmake/`, `vcpkg.json`: vendored dependency and build configuration.

## License and credits

Distributed under GNU GPL version 3 with the additional permissions in [EXCEPTIONS](EXCEPTIONS).
See [COPYING](COPYING) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Third-party components retain
their own copyright notices and licenses. [DEPENDENCIES.json](DEPENDENCIES.json) records resolved dependency
versions and download references. Game assets remain subject to their respective rights holders' terms.
