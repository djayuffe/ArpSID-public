# Installing ArpSID

Every [release](https://github.com/djayuffe/ArpSID-public/releases) has one zip
per product and platform, and a `SHA256SUMS.txt`. Each zip contains the bundle
and the matching installer script.

- [Quick install](#quick-install)
- [Which download](#which-download)
- [macOS](#macos) · [Windows](#windows) · [Linux](#linux)
- [Factory presets (VST3)](#factory-presets-vst3)
- [Verifying a download by hand](#verifying-a-download-by-hand)
- [Uninstalling](#uninstalling)
- [Troubleshooting](#troubleshooting)
- [Building and installing from source](#building-and-installing-from-source)

---

## Quick install

One command downloads the latest release for your machine and verifies it
against `SHA256SUMS.txt`, then runs the installer.

```bash
# Linux and macOS
curl -fsSL https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.sh | bash
```

```powershell
# Windows: PowerShell. Installing for all users needs "Run as administrator".
irm https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.ps1 | iex
```

**Options** (`get_arpsid.sh`; pass them after `bash -s --`):

| Option | Effect |
|---|---|
| `--version 0.9.8` | Install that release instead of the latest. |
| `--products vst3,auv2` | macOS only: which products (`vst3`, `auv2`, `auv3`, `standalone`). The default is all four. |
| `--system` | Install plug-ins for all users (uses `sudo`). |
| `--dest DIR` | Linux: install the VST3 into `DIR`. |
| `--yes` | Replace an existing install without asking. |
| `--validate` | macOS: run Apple's `auval` on the AU afterwards. |
| `--keep` | Keep the downloaded zips (the path is printed). |

For example, to install 0.9.8 for all users:

```bash
curl -fsSL https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.sh | bash -s -- --version 0.9.8 --system
```

`get_arpsid.ps1` takes `-Version` and `-Scope User|System`. To use them, save
the script and run
`powershell -ExecutionPolicy Bypass -File .\get_arpsid.ps1 -Scope User`.

The scripts only use `curl` and `unzip` (or PowerShell on Windows). They
stop if a checksum does not match.

---

## Which download

| Platform | Zip | Contains |
|---|---|---|
| macOS 12+ (Apple silicon and Intel) | `ArpSID-<v>-auv2-macos-universal.zip` | `ArpSID.component`: Audio Unit v2 in five flavors (ArpSID, ArpSID Instrument, DrSID drum machine, SID-808, C64 tune player) |
| | `ArpSID-<v>-auv3-macos-universal.zip` | `ArpSID.app`: the Logic-compatible app that carries the AUv3 extension |
| | `ArpSID-<v>-standalone-macos-universal.zip` | `ArpSID Standalone.app`: runs without a DAW (keyboard, transport, preset browser) |
| | `ArpSID-<v>-vst3-macos-universal.zip` | `arpsid_vst3.vst3` + `VST3 Presets/` |
| Windows 10/11 x64 | `ArpSID-<v>-vst3-windows-x64.zip` | `arpsid_vst3.vst3` + `VST3 Presets/` |
| Windows 11 on Arm | `ArpSID-<v>-vst3-windows-arm64.zip` | `arpsid_vst3.vst3` + `VST3 Presets/` |
| Linux x86_64 | `ArpSID-<v>-vst3-linux-x86_64.zip` | `arpsid_vst3.vst3` + `VST3 Presets/` |
| Linux aarch64 | `ArpSID-<v>-vst3-linux-aarch64.zip` | `arpsid_vst3.vst3` + `VST3 Presets/` |
| Any | `ArpSID-<v>-source.zip` | Source code without the C64 ROMs |

Each plug-in zip also contains its installer (`install_macos.sh`, `install.ps1`
or `install.sh`) and an `INSTALL.txt` with the short version of this page.
Every VST3 zip carries the 180 factory patches as `.vstpreset` files in
`VST3 Presets/`; the installers put them where hosts look for presets (see
[Factory presets](#factory-presets-vst3)).

---

## macOS

**With the installer.** Unzip, open Terminal in the folder and run
`./install_macos.sh`. It installs every bundle it finds next to it:

| Bundle | Installed to (default) | With `--system` |
|---|---|---|
| `ArpSID.component` | `~/Library/Audio/Plug-Ins/Components/` | `/Library/Audio/Plug-Ins/Components/` |
| `arpsid_vst3.vst3` | `~/Library/Audio/Plug-Ins/VST3/` | `/Library/Audio/Plug-Ins/VST3/` |
| `ArpSID.app` | `/Applications/`; the AUv3 extension is registered with `pluginkit` | same |
| `ArpSID Standalone.app` | `/Applications/` | same |
| `VST3 Presets/` (VST3 zip) | `~/Library/Audio/Presets/Uber Sound Solutions/ArpSID/` | `/Library/Audio/Presets/Uber Sound Solutions/ArpSID/` |

Then it:

1. removes the download quarantine (`com.apple.quarantine`), and
2. restarts `AudioComponentRegistrar`, so hosts rebuild their Audio Unit list.

Restart your host afterwards. In Logic, quit and reopen it; Logic rescans
changed Audio Units at launch.

| Option | Effect |
|---|---|
| `--system` | Plug-ins for all users (uses `sudo`). |
| `--from DIR` | Install the bundles in `DIR`, e.g. after unzipping several zips into one folder. |
| `--validate` | Run `auval -v aumu <flavor> ASID` for all five AU flavors afterwards. |
| `--uninstall` | Remove every ArpSID product and the VST3 factory presets. |
| `--yes` | Replace existing installs without asking. |

**By hand.**

```bash
xattr -dr com.apple.quarantine ArpSID.component arpsid_vst3.vst3 ArpSID.app "ArpSID Standalone.app"
cp -R ArpSID.component ~/Library/Audio/Plug-Ins/Components/
cp -R arpsid_vst3.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R ArpSID.app "ArpSID Standalone.app" /Applications/
killall -9 AudioComponentRegistrar
```

Open `ArpSID.app` once so macOS registers the AUv3.

**Signing.** The bundles are ad-hoc signed and **not notarized**. That is why
the quarantine must be removed, or Gatekeeper reports them as damaged. Some
hosts load an AUv3 only when it is signed with a Developer ID. The AUv2 and
VST3 have no such restriction.

---

## Windows

**With the installer.** Unzip. In the folder, open PowerShell with **Run as
administrator**, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

| Option | Effect |
|---|---|
| (default) `-Scope System` | Install to `C:\Program Files\Common Files\VST3\`. Every host scans this folder. Needs an elevated PowerShell. |
| `-Scope User` | Install to `%LOCALAPPDATA%\Programs\Common\VST3\` (the per-user VST3 folder, VST 3.7+). No admin rights needed; check that your host scans this folder. |
| `-Dest DIR` | Install into `DIR`. |
| `-PresetsDest DIR` | Put the factory presets in `DIR\Uber Sound Solutions\ArpSID\`. The default is `Documents\VST3 Presets` with `-Scope User` and `%ProgramData%\VST3 Presets` with `-Scope System`. |
| `-NoPresets` | Do not install the factory presets. |
| `-Check` | Only check that the bundle has a module for this PC (x64 or arm64). |
| `-Uninstall` | Remove ArpSID and its factory presets from the chosen folders. |

The installer removes the "downloaded from the internet" mark
(`Unblock-File`), because some hosts refuse to load blocked DLLs.

**By hand.** Copy the `arpsid_vst3.vst3` folder into
`C:\Program Files\Common Files\VST3\`, and the contents of `VST3 Presets`
into `Documents\VST3 Presets\`. If Windows marked the zip as
downloaded, right-click the zip, open **Properties**, tick **Unblock**, then
extract it again.

**Requirements.** Windows 10 or 11, and a 64-bit VST3 host. From 0.9.8 the
C/C++ runtime is linked statically, so no Visual C++ Redistributable is
needed.

---

## Linux

**With the installer.** Unzip, then run:

```bash
./install.sh            # for you: ~/.vst3/, presets in ~/.vst3/presets/
./install.sh --system   # all users: /usr/lib/vst3/ and /usr/share/vst3/presets/ (uses sudo)
./install.sh --check    # only check that the libraries ArpSID needs are there
./install.sh --uninstall
```

`--dest DIR` installs the plug-in into another folder, `--presets-dest DIR`
puts the factory presets in `DIR/Uber Sound Solutions/ArpSID/`, `--no-presets`
skips them, and `--yes` replaces an existing install without asking. The installer checks that the bundle has a module for
your CPU (`x86_64-linux` or `aarch64-linux`). It also runs `ldd` on it and, if
a shared library is missing, prints the packages to install.

**By hand.**

```bash
mkdir -p ~/.vst3/presets
cp -R arpsid_vst3.vst3 ~/.vst3/
cp -R "VST3 Presets/." ~/.vst3/presets/
```

**Runtime libraries.** The editor draws with cairo and pango on X11. Desktop
systems normally have these libraries already. On a minimal system, install:

| Distribution | Command |
|---|---|
| Debian, Ubuntu | `sudo apt-get install libxcb1 libxcb-util1 libxcb-cursor0 libxcb-keysyms1 libxcb-xkb1 libxkbcommon0 libxkbcommon-x11-0 libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libfontconfig1 libfreetype6` |
| Fedora | `sudo dnf install libxcb xcb-util xcb-util-cursor xcb-util-keysyms libxkbcommon libxkbcommon-x11 cairo pango fontconfig freetype` |
| Arch | `sudo pacman -S libxcb xcb-util xcb-util-cursor xcb-util-keysyms libxkbcommon libxkbcommon-x11 cairo pango fontconfig freetype2` |

Hosts known to load Linux VST3 plug-ins include Bitwig Studio, REAPER, Ardour,
Carla and Qtractor. Under Wayland, the editor runs through XWayland, as most
Linux plug-in editors do.

---

## Factory presets (VST3)

The 180 factory patches reach a VST3 host in two ways:

- **The program list.** ArpSID publishes them as a VST3 program list
  (`IUnitInfo`). Hosts that read program lists show it in their program or
  preset menu. The editor's preset picker uses the same list.
- **Preset files.** Hosts whose preset browser reads files from disk, rather
  than the program list, need `.vstpreset` files in the standard VST3 preset
  folders. Every VST3 zip has them in `VST3 Presets/`, and the
  installers copy them to:

  | OS | For you | For all users |
  |---|---|---|
  | Linux | `~/.vst3/presets/Uber Sound Solutions/ArpSID/` | `/usr/share/vst3/presets/Uber Sound Solutions/ArpSID/` |
  | Windows | `Documents\VST3 Presets\Uber Sound Solutions\ArpSID\` | `%ProgramData%\VST3 Presets\Uber Sound Solutions\ArpSID\` |
  | macOS | `~/Library/Audio/Presets/Uber Sound Solutions/ArpSID/` | `/Library/Audio/Presets/Uber Sound Solutions/ArpSID/` |

  They are sorted into folders by role: `Bass`, `Bell`, `Drums`, `Keys`,
  `Lead`, `Metallic` and `Pad`. Each file also carries its name, a musical
  category and the patch description as preset metadata.

Loading a factory preset does the same as picking the program: it changes the
patch and nothing else. The MIX, KIT, DIGI and SETTINGS panels, a loaded `.sid`
tune, the host bypass and the editor's size and tab stay as they were. (A
preset you save yourself from the host is a full snapshot and restores
everything.)

---

## Verifying a download by hand

```bash
sha256sum -c --ignore-missing SHA256SUMS.txt      # Linux
shasum -a 256 -c --ignore-missing SHA256SUMS.txt  # macOS
```

```powershell
(Get-FileHash .\ArpSID-<v>-vst3-windows-x64.zip -Algorithm SHA256).Hash
# compare with the line for that file in SHA256SUMS.txt
```

---

## Uninstalling

| Platform | Command |
|---|---|
| macOS | `./install_macos.sh --uninstall`, or delete the bundles listed under [macOS](#macos) and run `killall -9 AudioComponentRegistrar`. |
| Windows | `.\install.ps1 -Uninstall`, adding `-Scope User` if you installed for yourself; or delete `arpsid_vst3.vst3` from the VST3 folder. |
| Linux | `./install.sh --uninstall`, adding `--system` for a system install; or delete `~/.vst3/arpsid_vst3.vst3` and `~/.vst3/presets/Uber Sound Solutions/ArpSID`. |

Projects keep their ArpSID settings. Installing ArpSID again restores them.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| macOS: "ArpSID is damaged and can't be opened" | Remove the quarantine: `xattr -dr com.apple.quarantine <bundle>`. `install_macos.sh` does this for you. |
| macOS: Logic does not list ArpSID, or lists an old version | Run `killall -9 AudioComponentRegistrar` and restart Logic. Logic's Plug-in Manager can also reset and rescan. |
| macOS: the AUv3 is missing | Open `ArpSID.app` once, or run `pluginkit -a /Applications/ArpSID.app/Contents/PlugIns/arpsid_auv3.appex`. |
| Windows: the host skips ArpSID | Check the host's VST3 folder list, and check the files are unblocked (run `install.ps1` again). Use the zip that matches your PC: x64 or arm64. |
| Linux: the host skips ArpSID, or the editor window is empty | Run `./install.sh --check` and install the listed libraries. Check that your host scans `~/.vst3` (or `/usr/lib/vst3`). |
| Linux: no editor, only a list of parameters | The plug-in was built with `ARPSID_VST3_EDITOR=OFF`. Release builds always include the editor. |
| Linux: the host crashed when opening the editor (0.9.9 and earlier) | Fixed after 0.9.9: the editor did not connect VSTGUI to the host's event loop and crashed on the first X11 call, and reopening it could crash in cairo. Update ArpSID. |
| Linux: the editor does not open and the host reports an error | The host gave the editor no event loop (`Linux::IRunLoop`). ArpSID then refuses to open instead of crashing. Every mainstream Linux host provides one; update the host. |
| The factory patches are missing from the host's preset browser | Install the presets (run the installer again, or copy `VST3 Presets` by hand, see [Factory presets](#factory-presets-vst3)) and let the host rescan its preset folders. The program list works without them. |
| RSID tunes do not play in the C64 player | RSID tunes need your own KERNAL/BASIC/CHARGEN ROM dumps; none are bundled. PSID tunes play without them. |

---

## Building and installing from source

The short version for Linux:

```bash
git clone https://github.com/djayuffe/ArpSID-public.git && cd ArpSID-public
./build.sh --install-deps --fetch-vst3-sdk --install-vst3
```

In detail:

| Step | Command |
|---|---|
| Build packages (Debian/Ubuntu, Fedora, Arch, openSUSE) | `scripts/linux/install_build_deps.sh` (`--dry-run` prints the command; `--no-editor` skips the GUI packages) |
| Steinberg VST3 SDK at the pinned version | `scripts/fetch_vst3_sdk.sh`, which clones into `.deps/vst3sdk`; or CMake `-DARPSID_FETCH_VST3SDK=ON` |
| Build, validate, host test, install to `~/.vst3` | `./build.sh --fetch-vst3-sdk --install-vst3` |
| Release-style zip with its installer | `./build.sh --fetch-vst3-sdk --package-vst3 --no-tests`, which writes `build/dist/ArpSID-<v>-vst3-linux-<arch>.zip` |
| A VST3 without the editor (no X11/cairo/pango needed) | add `--no-vst3-editor`; hosts then show their generic parameter UI |
| System-wide install | `sudo cmake --install build --prefix /usr`, which installs to `/usr/lib/vst3/` |

If an editor build package is missing, CMake stops and names every missing
one.

- **macOS:** Xcode and CMake. `./build.sh --install-auv2 --clear-au-cache --validate-auv2`
  builds and installs the AU; `./build.sh --vst3-sdk <sdk> --install-vst3`
  does the same for the VST3.
- **Windows:** Visual Studio 2022 or newer, and CMake:
  ```bat
  cmake -S . -B build-vst3 -A x64 -DARPSID_BUILD_VST3=ON -DARPSID_FETCH_VST3SDK=ON
  cmake --build build-vst3 --config Release --target arpsid_vst3
  ```
  Then run `scripts\install\install_vst3.ps1 -From build-vst3\VST3\Release\arpsid_vst3.vst3`.

The build is described in more detail in the README and in
[VST3_IMPLEMENTATION.md](VST3_IMPLEMENTATION.md#building-and-installing).
