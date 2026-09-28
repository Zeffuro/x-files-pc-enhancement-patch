# Playing on Linux

Linux support is experimental. Use the same Windows release ZIP with Wine or
Proton. A complete playthrough, Steam Deck controls and Steam Input have not yet
been verified.

Keep each runner's own prefix, which is its private Windows environment. You can
copy the installed game folder between runners. Back up `saves`, `patch.ini`,
`ddraw.ini` and `preferences.ini` before changing your setup.

## Steam and Proton

1. Extract the release ZIP into a writable folder.
2. Add **XFilesSetup.exe** as a non-Steam game. In its Steam properties, enable
   **Force the use of a specific Steam Play compatibility tool** and choose a
   stable Proton version.
3. Set its Steam launch options to:

   ```text
   PROTON_FORCE_LARGE_ADDRESS_AWARE=0 WINE_LARGE_ADDRESS_AWARE=0 WINEDLLOVERRIDES="ddraw=n,b" %command%
   ```

4. Run setup and install into a folder you can find outside the temporary prefix,
   such as a folder under your Linux home directory through Wine's `Z:` drive.
5. Change that shortcut's target to the installed **XFilesPlay.exe** and its
   **Start In** folder to the installed game folder. Keep the same compatibility
   tool and launch options selected.

Use a standard gamepad layout in Steam Input to try the patch's controller
mapping. If buttons act twice, check for a simultaneous keyboard or mouse layout.
Report your layout with controller problems.

Keep both address-space settings above. Proton's larger address space can make
the original game hang on a black screen before the opening movie. If you change
these settings after a failed launch, close the game and its launcher completely
before trying again.

## Lutris

Add a locally installed game with the **Wine** runner. Select a dedicated prefix
and run **XFilesSetup.exe** in that prefix. After installation, set the executable
to **XFilesPlay.exe** and the working directory to its folder.

In the game's runner options, add a DLL override for `ddraw` with value `n,b`.
Keep the prefix and runner the same for setup and play. If using Flatpak Lutris,
make sure it can access both your disc files and the game folder.

For a Proton runner, also set the environment variables
`PROTON_FORCE_LARGE_ADDRESS_AWARE=0` and `WINE_LARGE_ADDRESS_AWARE=0`.

Current Lutris versions can manage Proton through UMU. Choose the runner offered
by Lutris. Older instructions that use Proton's bundled Wine executable directly
may omit the runtime it needs. See the [Lutris release notes](https://github.com/lutris/lutris/releases).

## Wine

Install Wine with support for 32-bit Windows programs using your distribution's
instructions. These commands use an example prefix and installation folder:

```sh
WINEPREFIX="$HOME/Games/xfiles-prefix" wine "/path/to/release/XFilesSetup.exe"
cd "$HOME/Games/The X-Files"
WINEPREFIX="$HOME/Games/xfiles-prefix" WINEDLLOVERRIDES="ddraw=n,b" wine XFilesPlay.exe
```

Choose the game folder in setup before running the last two commands. Use the
patch's launcher for every session. No separate QuickTime installation is needed.
The `ddraw` override selects the display library included with the patch, as
required by [cnc-ddraw's Wine instructions](https://github.com/FunkyFr3sh/cnc-ddraw#instructions).

## Proton outside Steam

Use a launcher that supplies Proton's runtime, such as
[UMU](https://github.com/Open-Wine-Components/umu-launcher). With UMU installed:

```sh
WINEPREFIX="$HOME/Games/xfiles-umu" PROTON_FORCE_LARGE_ADDRESS_AWARE=0 WINE_LARGE_ADDRESS_AWARE=0 WINEDLLOVERRIDES="ddraw=n,b" umu-run "/path/to/release/XFilesSetup.exe"
cd "$HOME/Games/The X-Files"
WINEPREFIX="$HOME/Games/xfiles-umu" PROTON_FORCE_LARGE_ADDRESS_AWARE=0 WINE_LARGE_ADDRESS_AWARE=0 WINEDLLOVERRIDES="ddraw=n,b" umu-run XFilesPlay.exe
```

UMU can download its runtime and Proton on first use. Keep the same prefix and
Proton selection for setup and play. A standalone UMU test does not cover Steam's
overlay or Steam Input.

## Fonts

The save browser uses fonts installed with the game. Keep the original `.TTR`
files in the game folder. If native game text looks wrong when running from a
Windows-mounted folder in WSL, copy the installed game to your Linux home folder
and restart it.

Some original confirmation dialogs use **Courier New**. If it is missing, Wine
substitutes another font. Close the game and install Courier New into the same
prefix. For Wine, [Winetricks](https://github.com/Winetricks/winetricks) provides it:

```sh
WINEPREFIX="$HOME/Games/xfiles-prefix" winetricks courier
```

For a launcher-managed prefix, use that launcher's prefix tools. Installing fonts
into a different prefix will not affect the game.

## Reporting a problem

Use **F10 → Tools → Save report ZIP**. Include your distribution, X11 or Wayland,
GPU and driver, launcher, exact Wine or Proton version, game edition and language,
and the steps that fail. For controller problems, include the controller model
and Steam Input layout. Nothing is uploaded automatically.

For a Proton launch failure, add `PROTON_LOG=1` before the existing Steam launch
options. Proton normally writes `steam-<id>.log` in your home folder. For UMU, add
`UMU_LOG=1 PROTON_LOG=1` before the command. Remove those options after collecting
the log. See [Proton logging options](https://github.com/ValveSoftware/Proton#runtime-config-options).

Start with windowed play. Report broken rendering or pointer alignment with a
screenshot and the settings used. When comparing runners, change one setting at
a time and keep a copy of the save that reproduces the problem.
