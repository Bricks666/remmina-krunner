<!-- SPDX-FileCopyrightText: 2026 Remmina KRunner contributors -->
<!-- SPDX-License-Identifier: 0BSD -->

# Remmina KRunner

Remmina KRunner is a Plasma 6 search provider for saved Remmina connections.
It searches one selected native, Flatpak, or Snap installation and can open
Remmina's connection-creation window without linking to Remmina libraries.

## Installation

The runtime needs Plasma 6, Qt 6.7 or newer, the matching KDE Frameworks 6
libraries, D-Bus, and at least one supported Remmina installation. The source
workflow additionally needs Git and rootless Podman; compilers and build tools
stay in the checked-in Fedora 44 container image.

### Install a verified release

For a published release, download both the archive and its checksum from the
same GitHub Releases page. Verify before extracting or running any bundled
script:

```bash
version=v0.1.0
archive="remmina-krunner-${version}-linux-x86_64.tar.gz"
base_url="https://github.com/Bricks666/remmina-krunner/releases/download/${version}"
curl --fail --show-error --location --remote-name "${base_url}/${archive}"
curl --fail --show-error --location --remote-name "${base_url}/${archive}.sha256"
sha256sum --check "${archive}.sha256"
tar -xzf "${archive}"
"./${archive%.tar.gz}/install.sh"
```

The installer is user-local: it uses `$HOME/.local` and `XDG_DATA_HOME`, uses
neither sudo nor a package manager, refreshes KDE's service cache, and asks the
runner to rescan installed Remmina applications. A nonempty `XDG_DATA_HOME`
must be an absolute normalized path. Keep the verified extracted bundle so its
matching `uninstall.sh` is available later.

### Build from source

Clone the repository and run the checked-in container workflow:

```bash
git clone https://github.com/Bricks666/remmina-krunner.git
cd remmina-krunner
./scripts/container.sh check
./scripts/container.sh release-build
```

`check` performs a debug build, the complete test suite, and an exact staged
install inspection. `release-build` proves the non-test Release configuration.
Use the checksum-verified bundle above for a normal user-local installation.
The only host path mounted by either command is the repository workspace;
container `HOME` is `/tmp`.

### Uninstall or upgrade

Run `uninstall.sh` from the same verified bundle and with the same
`XDG_DATA_HOME` used during installation. It removes only the runner binary,
D-Bus/KRunner metadata, and configuration module; it preserves
`~/.config/remmina-krunnerrc`, all Remmina profiles, and unrelated files.
Installing a newer verified bundle performs an atomic user-local upgrade.

## Use

Enable “Remmina” in System Settings → Search → Plasma Search, then use:

- `rem <query>` to search the selected installation's profiles.
- `rem` returns no results and does not read profiles.
- Exact, case-insensitive `rem new` offers “Create a new Remmina connection”.
  Longer input such as `rem new york` remains an ordinary profile search.

The result title is the profile `name`. Its subtitle contains the available
protocol, raw `server`, and display `labels`. Search covers only name, server
(including an IP address, port, hostname, or domain), and individual labels.
Whitespace-separated query tokens may match different fields. An exact field
or label match ranks above a prefix, which ranks above a substring; stable
name/server/path ordering breaks ties. Protocol is displayed but is not a
search field.

The installed metadata and the service `Config()` response use the anchored,
case-insensitive activation expression `(?i)^rem(?:\s.*)?$`. The service still
parses every input itself, so empty input and the reserved creation command
have the behavior above even if a Plasma/KF version handles runner trigger
words differently.

## Choosing a Remmina installation

The Plasma Search settings page rescans application binaries and lists every
detected native, Flatpak, and Snap instance. Detection never guesses from
profile files. On first use, or when the saved choice disappears, selection
falls back in this order: native instances in `PATH`, Flatpak user/system
instances, then Snap. The repaired fallback is saved immediately. A manual
dropdown change is pending until **Apply**.

Application scans occur after installation, whenever settings open, and at
runner service startup. If nothing is installed, settings show
“No Remmina installations found.” A lookup returns an informational unavailable
result; choosing creation produces a desktop failure notification without
private connection details.

Only the selected instance supplies profiles and handles connection or
creation launches:

- A native instance uses host XDG/legacy Remmina profile locations and invokes
  its selected executable with fixed `--connect` or `--new` arguments.
- A Flatpak instance uses `org.remmina.Remmina` data below its per-application
  XDG area and launches the chosen user or system installation through
  Flatpak with a fixed argument list.
- A Snap instance uses the active `~/snap/remmina/current` and common data
  locations and the stable Remmina Snap launcher. Revision changes do not
  discard the selection.

Missing profile directories, unreadable profiles, and launch failures are
reported generically. Check that the selected package still exists, reopen
settings to rescan, select the intended instance, and press Apply.

## Profile refresh and privacy

Profile discovery is lazy. The first nonempty lookup in a KRunner match session
builds the list; later keystrokes reuse it. A directory/file watcher marks the
snapshot dirty after create, edit, rename, or removal, and the next lookup
rescans. A new session also checks fingerprints, while a selection change or
service restart clears the snapshot. Neither bare `rem` nor `rem new` scans.

Reading is strictly read-only. The parser retains only name, server, labels,
protocol, and the internal profile identity needed to launch. It does not
retain a password, username, gateway, notes, SSH fields, or unknown keys.
Logs and D-Bus errors do not include raw profile contents, connection metadata,
or a profile path. The runner never writes Remmina profiles.

## Safe manual Plasma validation

After installing a verified bundle in a manual Plasma session:

1. Open Plasma Search settings and confirm only the expected package instances
   are listed; select one and Apply.
2. Confirm bare `rem` is empty, and use a temporary synthetic profile to test
   searches by name, server/IP/domain, and label plus exact `rem new`.
3. Create, edit, and remove only that temporary profile and start a new KRunner
   session to check watcher/fingerprint refresh.
4. Temporarily remove or deselect the test instance to check fallback and the
   no-installation messages, then restore it.

Do not print, copy, attach, or log profile files or real result text while
validating. Remove the synthetic profile when finished.

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md). All builds and tests run through Podman.

## License

Project code and documentation are 0BSD. The KDE KRunner D-Bus interface XML
in `data/org.kde.krunner1.xml` is LGPL-2.0-or-later; see `LICENSES/`.
