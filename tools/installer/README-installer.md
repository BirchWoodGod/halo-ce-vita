# Halo CE Setup

`halo_ce_vita_installer.py` is a standard Setup wizard for Halo CE: it installs
the game on a PS Vita, over VitaShell's FTP, or on this PC, from your own game
files. It is one Python file for Windows, Linux and macOS;
`HaloCEVitaInstaller.exe` is the same tool as one Windows program, with ffmpeg
inside it.

**No game data comes with it.** Every step reads your own files: your Xbox
disc image or your own Halo: The Master Chief Collection, your Halo PC
files.

## Running it

- **Windows:** download `HaloCEVitaInstaller.exe` from the
  [releases](https://github.com/BirchWoodGod/halo-ce-vita/releases/latest)
  (it is attached next to the VPK, `halo-ce-vita-<version>-vita.vpk`) and double-click it. It is one
  program of about 48 MB that unpacks itself to a temporary folder at each
  start, so it takes a few seconds to open. It is not signed: Windows
  SmartScreen says "Windows protected your PC" the first time (click
  "More info", then "Run anyway"), and some antivirus programs may flag it
  wrongly, as they often do with Python programs packed this way. The
  Python file below is the same tool, readable.
- **Any system with Python 3.8 or later:** `python3 halo_ce_vita_installer.py`
  opens the window (on Linux, tkinter may be a separate package:
  `python3-tk` or `tk`). Converting the movies needs ffmpeg installed.
- **Without the window:** `python3 halo_ce_vita_installer.py --help` lists
  the same work as commands (see below).

Setup downloads the VPK and the PC build from the project's releases and
checks their signature. A file of yours (a VPK or an archive, next to the tool or
not) is only used if you choose it, after a "not signature-checked" warning;
nothing next to the tool is picked up by itself.

## The wizard

Next, next, finish, with **< Back**, **Next >** and **Cancel** at the bottom
right. Cancel asks first.

1. **Welcome.** The fan-project notice.
2. **License Agreement.** The GPL's text. Next stays off until you accept.
   No game data is included.
3. **Where to Install.** PS Vita or PC. Platforms that are not ready yet are
   listed greyed out and cannot be picked.
4. **Game Files.** One page.
   - **Automatic** takes everything from your own Halo: The Master Chief
     Collection, found the way the game does (see [the files](#the-files)).
   - **Manual**: your Xbox disc image or game folder, the Halo Custom Edition
     installer or folder (required on the Vita, optional on PC), and on the
     Vita whether to convert the disc's movies. If MCC is not found, Manual is
     selected and the page says why.
5. **Destination.**
   - **Vita:** its address, as VitaShell shows it, with **Test connection**;
     the VPK; the folder the files are gathered in on their way (about 2.5 GB).
   - **PC:** the install folder (`%LOCALAPPDATA%\Programs\Halo CE`,
     `~/.local/share/halo-ce` on Linux: no admin needed), with **Browse**, the
     space needed and free, and check boxes for a desktop shortcut and a Start
     menu entry. Setup downloads the PC build; untick **Download the PC build**
     to choose an archive of your own, after a warning that it is not
     signature-checked.
6. **Ready to Install.** A plain list of your choices, and the **Install**
   button.
7. **Installing.** One progress bar with the current file, doing everything in
   order. A new PC install that is cancelled or fails is taken back; what
   already reached the Vita stays, so a second try goes on from there.
8. **Finish.** "Setup completed", with **Launch Halo CE** on PC, or what is
   left to do on the Vita. If it stopped, **< Back** goes to Ready to Install
   to try again.

Adding a game later is one entry in `GAMES`: a game page then shows up by
itself. `install_flow()` gives the pages of each choice.

### A PC install

Like any installer, for the current user:

- the PC build's archive, `halo-ce-vita-<version>-pc-windows.zip` or
  `-pc-linux.tar.gz`, unpacked into the install folder;
- the game files in the folder of its program, `build/windows` or
  `build/linux`, where the game looks (without the movies: the PC build plays
  none), gathered straight there, with no second copy. A `maps` folder is made
  there even with MCC's files alone, as the game needs one to take that folder
  for its data;
- the shortcuts: on Windows a desktop shortcut and a Start menu entry, which
  start `build\windows\halo.exe` in the install folder; on Linux `.desktop`
  files in `~/Desktop` and `~/.local/share/applications`;
- on Windows `uninstall.exe` in the folder, and an Apps & Features entry under
  `HKCU` (`Software\Microsoft\Windows\CurrentVersion\Uninstall\HaloCE`);
- `halo-ce-install.json` in the folder: every file written and its size.

Run Setup again and it offers **Repair** (it checks every file of that record
and puts back what is missing or the wrong size: the PC build by the signed
download or an archive you choose, the game files from the source you choose), **Uninstall**, or the whole wizard
again. Uninstall removes the files of the record and the shortcuts, and no
other file: your saved games (`%APPDATA%\halo`, `~/.local/share/halo-linux`) and
what the game wrote beside itself stay.

Setup downloads the PC build from the newest release and checks it the way the game checks its updates: the signature of
`halo-update-pc.txt` by the release key built into the tool, then the file's
size and SHA-512 (`tools/release/README.md`, "The PC builds' manifest"). If
anything does not match it stops, keeps nothing and installs nothing. An archive
lying next to Setup is never picked up by itself (Setup is usually run from
Downloads, where any site can drop a file). Untick **Download the PC build** to
choose one you already have: Setup warns that it is not signature-checked and
asks first, reads it as untrusted all the same, writes its SHA-256 to the log, and
compares it with the signed list. If its SHA-512 is the release's, it counts as a
checked download. **Launch Halo CE** on the last page starts only a build checked
and unpacked by this run of an install, never a repair (the record is a file you
can write, and repair compares sizes alone): an archive that does not match
installs, and you start it from its shortcut. A checked build is not put beside
programs it does not hold, in the folders it installs into: those of an earlier
install go, and any other stops the install until they are moved away. The version of an unchecked archive is only its file's name: it is
never taken as the version a signed download may not be older than.

For a Vita, the files are gathered in a folder first (by default
`Documents/Halo CE Vita files`, about 2.5 GB with everything), laid out as
the game reads them, and stay there afterwards, so a copy that stopped can go on:

```
Halo CE Vita files/
    default.xbe          -> ux0:data/haloce-vita/default.xbe
    maps/*.map           -> ux0:data/haloce-vita/maps/
    movies/*.mp4         -> ux0:data/haloce-vita/movies/
    mcc/halo1/...        -> ux0:data/haloce-vita/mcc/halo1/...   (from MCC)
```

Custom maps you put in that `maps` folder yourself are copied too.

## The files

What each source gives, and how it is checked. In the window they are the Game
Files page; on the command line, the commands below.

1. **Game files**, one of two ways:
   - **Your Xbox game.** Choose your Xbox Halo disc image (`.iso`, `.xiso`:
     an XISO made by extract-xiso, or a full disc image) or an already
     unpacked game folder. The `maps` folder and `default.xbe` are taken out.
     The disc image is read by the tool itself (a small XDVDFS reader): no
     extract-xiso needed. It checks that the campaign maps and the menu
     (`ui.map`, `a10.map` ... `d40.map`) and `default.xbe` are there, so the
     PC version's maps are not taken by mistake.
   - **Halo: The Master Chief Collection on Steam**, for the builds that
     play MCC's own Halo CE maps (1.1.1-insider.25 and later; see
     [docs/mcc_cea_maps.md](../../docs/mcc_cea_maps.md)). The tool finds
     MCC's folder by itself (Steam's `libraryfolders.vdf` and, on Windows,
     the registry: Steam's folder and MCC's Steam uninstall entry; also the
     Xbox app's `XboxGames` folders), or choose it with Browse...
     (Steam: right-click the game, Manage, Browse local files; its `halo1`
     folder or a Steam library works too). It copies exactly what the game
     reads, about 1.4 GB, to `ux0:data/haloce-vita/mcc/`:

     ```
     mcc/halo1/maps/a10.map ... d40.map         the campaign
     mcc/halo1/maps/beavercreek.map ...         the 19 multiplayer maps
     mcc/halo1/maps/bitmaps.map                 their pixels (MCC's own, 186 MB)
     mcc/halo1/maps/ui.map                      MCC's menus (used only with no Xbox ui.map)
     mcc/halo1/maps/custom_edition/             Halo PC's bitmaps.map, loc.map, sounds.map
     mcc/halo1/sound/pc/sounds_adpcm.fsb        the classic sound effects (88 MB)
     mcc/halo1/sound/pc/lst/sounds_*.lst.bin    which sounds they are (the adpcm list is
                                                needed; the others only count what is silent)
     ```

     Nothing else of MCC (its dialogue and music banks, the other games) is
     copied. Each file's header is checked (cache version 13, FSB5, the
     lists' version), before and after copying; the campaign and
     `bitmaps.map` must be there, or nothing is copied. Nothing of MCC goes
     to the `maps` folder.

2. **Movies (optional, Vita only).** The PC build skips movies, and MCC has
   none. The disc's Bink movies (`bink/*.bik`) are
   converted with ffmpeg to H.264 MP4s, 640 wide, as the README's Movies
   section does. Both choices are H.264 Constrained Baseline with no
   B-frames, the only kind the Vita plays cleanly (High profile's B-frames
   show as white frames): Standard is the README's first command, Better
   quality the same profile at a higher quality (`-crf 18 -preset slow`,
   larger files, slower to convert). The game runs without them: each
   missing movie is skipped (no intro, no attract videos, no credits movie)
   and the main menu comes up ready to play. Untick the box to save the few
   minutes it takes.
3. **Halo PC files (required on the Vita, optional on PC).** Halo Custom
   Edition's `bitmaps.map`, `sounds.map` and `loc.map`. Setup will not go past
   Game Files on the Vita without them, and says which are missing and where to
   get them. They count as there when they are in the gathered `maps`
   or in the MCC files of step 1 (`mcc/halo1/maps/custom_edition`, which
   the builds that play MCC's maps read when the `maps` folder has none: an
   MCC-only install needs nothing more, which is why Automatic does not ask).
   Builds before 1.1.1-insider.25 read them from `maps` only: this step puts
   them there.
   They come from one of:
   - **Halo: The Master Chief Collection** on Steam: its
     `halo1/maps/custom_edition` folder, found by itself in your Steam
     libraries (`libraryfolders.vdf`, the registry), or chosen with
     Folder... (the wizard fills the field in when it finds one of these). Not
     the maps directly in `halo1/maps`: those are MCC's own
     and do not work.
   - **The Halo Custom Edition installer**
     ([halomaps.org](https://www.halomaps.org/hce/detail.cfm?fid=410)).
     Whatever its file name: it is recognised by what is in it. The English
     `halocesetup_en_1.00.exe` is known by its SHA-256 (and the three maps
     unpacked from it are checked against theirs); any other Windows program
     whose resources hold a cabinet with `maps\bitmaps.map`, `sounds.map` and
     `loc.map` is taken as an installer too. The cabinet (LZX) is unpacked by
     the tool itself, in about 20 seconds, with nothing to install. Anything
     else is reported as "This isn't the Halo Custom Edition installer".
   - **A folder** that has them, such as a Custom Edition install's `maps`
     (on Windows found by itself where its installer recorded it, the
     registry's `Microsoft Games\Halo CE`, or in `Program Files`). A
     `halocesetup*.exe` in your Downloads is picked up by itself too.

   Each map's header is checked (the right kind of resource map).
4. **Sending it to the Vita.** On the Vita, open VitaShell and press SELECT: it
   shows an address like `ftp://192.168.1.20:1337`. **Keep the Vita awake** while it copies (touch the screen now
   and then, or turn off auto-standby in the Vita's power settings): if it
   sleeps, the connection drops. The tool tries again by itself a few times;
   if it gives up, press **< Back** and Install again: files already on the
   Vita with the right size are skipped, so it goes on from where it stopped.
   Every file is checked by its size on the Vita after copying. The command
   line's `upload --replace` copies files already there again.
5. **The VPK (Vita).** Download `halo-ce-vita-<version>-vita.vpk` from the
   [releases](https://github.com/BirchWoodGod/halo-ce-vita/releases/latest)
   (Setup downloads and signature-checks it by itself; a VPK of your own is only
   sent if you untick that and choose it). The tool copies it to
   `ux0:data/`. Then, on the Vita, leave FTP mode, open `ux0:data/` in
   VitaShell, press Cross on the VPK and confirm. The tool cannot install
   it: only VitaShell on the Vita can. Finish says so too.

Still needed on the Vita, as the README says: HENkaku/Enso, VitaShell, and the
shader compiler `ur0:data/libshacccg.suprx` (ShaRKF00D extracts it).

## The window's look

The window is ttk's native theme: `vista` on Windows, `aqua` on macOS, `clam`
elsewhere, with the system's own fonts. The only colour Setup sets is the white
of the band at the top and of the first and last pages, as a Windows wizard has
it. The picture on those two pages and the icon are drawn by the tool itself
(`sidebar_png`, `icon_png`): no box art, logos or other artwork, and no image
files. On Windows the window is DPI aware, so its text is sharp on scaled
displays.

Setup keeps one small file, for where it installed:
`%APPDATA%\HaloCEVita\installer-settings.json` on Windows,
`~/.config/halo-ce-vita/installer-settings.json` on Linux
(`$XDG_CONFIG_HOME`), `~/Library/Application Support/HaloCEVita/` on macOS.
What it did last is in `setup-log.txt` beside it.

## Command line

```
python3 halo_ce_vita_installer.py [--out FOLDER] xbox "Halo.iso"
python3 halo_ce_vita_installer.py [--out FOLDER] mcc [FOLDER]
python3 halo_ce_vita_installer.py [--out FOLDER] movies "Halo.iso" [--quality better] [--ffmpeg PATH]
python3 halo_ce_vita_installer.py [--out FOLDER] pc-files --mcc [FOLDER]
python3 halo_ce_vita_installer.py [--out FOLDER] pc-files --installer "any name.exe"
python3 halo_ce_vita_installer.py [--out FOLDER] pc-files --folder FOLDER
python3 halo_ce_vita_installer.py check-installer "any name.exe"
python3 halo_ce_vita_installer.py [--out FOLDER] check
python3 halo_ce_vita_installer.py [--out FOLDER] upload 192.168.1.20 [--replace]
python3 halo_ce_vita_installer.py vpk 192.168.1.20 [your.vpk]   # no file: the signed download
python3 halo_ce_vita_installer.py [--out FOLDER] pc-copy [BUILD FOLDER] [--replace]
python3 halo_ce_vita_installer.py [--out FOLDER] pc-install [FOLDER] [--package ARCHIVE | --download] [--desktop-shortcut] [--start-menu]
python3 halo_ce_vita_installer.py pc-uninstall [FOLDER]
python3 halo_ce_vita_installer.py --uninstall [FOLDER]
python3 halo_ce_vita_installer.py selftest
```

`check` says whether the output folder has the required Halo Custom
Edition files (and, if not, which are missing and where to get them; exit
code 1); `upload` warns the same way but copies the rest. Automatic is `mcc`
with no folder, then `upload` and `vpk`, or `pc-install`, which does what the
wizard's PC install does with what is in the output folder (`--out`). `pc-copy`
copies the gathered files into a PC build you unpacked yourself. `--uninstall`
is the window's uninstaller, which Apps & Features runs.

(`HaloCEVitaInstaller.exe` takes the same commands, but as a windowed program
it prints nothing; `selftest --report FILE` writes its results to a file.)

## Safety

- Every name read from a disc image is checked before a file is written: no
  `..`, no slashes or drive letters, no Windows device names, no control
  characters; the result must lie inside the output folder and not go
  through a link. The cabinet's names are only compared: the three maps are
  written under the tool's own names.
- Disc image and cabinet structures are bounds-checked (sizes, offsets,
  entry counts, directory loops, a damaged LZX stream) and cabinet block
  checksums are verified.
- FTP commands are built from checked names only (no line breaks reach the
  Vita), and the data connection goes only to the Vita's own address.
- MCC's folder is only read: the tool copies the files it names (fixed
  names, never a name read from MCC), into the output folder.
- The copy to a PC build uses the same checked names, writes only inside the
  build's data folder, refuses to write through a link, and moves each file
  into place only once it is whole.
- The PC build's archive is read as untrusted before a byte of it is written:
  this system's build must be in it, every name is checked as above, links,
  devices and doubled names are refused, sizes are bounded, and each file is
  checked (CRC, size) and moved into place only once it is whole. Only the
  checked download, or the archive you chose after the warning, is used.
- The PC download goes over HTTPS to GitHub's hosts alone, redirects too. The
  signature is checked before the list is read and the archive is asked for
  only after that; its size and SHA-512 are then the signed ones or it is
  thrown away. A list older than a verified install or than Setup is refused. No key
  but the release key's public half is in the tool. Launch starts only such a
  download.
- The install's record is only a file in a folder the user can write:
  uninstall and repair take its paths through the same checks, inside the
  install folder, and remove shortcuts only by their exact names. Uninstall
  never deletes a folder's contents wholesale.
- Windows' registry is written under `HKCU` only, in the one key above.
- No shell runs anything that holds a name or a path: ffmpeg and the game run
  with an argument list; PowerShell makes the shortcuts from a fixed line, which
  reads the paths from the environment; the temporary uninstaller's clean-up is
  a fixed `cmd` line, reading its folder from the environment too.
- The Windows build downloads ffmpeg from one fixed address and refuses it
  unless its SHA-256 matches (`build_windows.py`).

## Tests

```
python3 -m pip install -r requirements-dev.txt
python3 -m pytest tests
```

The tests build everything they read: a stand-in MCC folder (made-up
files with MCC's headers; Windows' registry mocked), a small XDVDFS image, an LZX
compressor (the decoder's other half) and cabinets, a Windows program with a
cabinet in its resources, fake resource maps, a stand-in ffmpeg, and a local
FTP server that answers like VitaShell (its LIST format, no NLST, no SIZE, a
connection that drops on demand, doubled transfer replies), a stand-in Steam
library and PC build archives, good and hostile. Each test has a home folder of
its own, so a real Steam or MCC is never read. The Windows parts of a PC install
run anywhere, against a stand-in registry and PowerShell. The window's tests run
where there is a display: every page fits, Next and Back, the license's gate,
greyed platforms, MCC found and not found, every field's check, Test connection,
Automatic and Manual through to a real install, the Vita's run, a failure and
Back, Cancel, repair and uninstall.

## Building the Windows program

On Windows, from the repository:

```
tools\installer\build_windows.bat
```

or `python tools/installer/build_windows.py` (`--no-ffmpeg` leaves ffmpeg
out). It downloads FFmpeg 9.0.2 (gyan.dev's essentials build, GPL v3) from a
fixed address, checks its SHA-256, and builds
`tools/installer/dist/HaloCEVitaInstaller.exe` with PyInstaller; ffmpeg's
LICENSE and README.txt (which names its source) go inside next to it, and
so do the project's `LICENSE`, which the License page shows, and the icon,
drawn into `build/` by the tool itself. The
GitHub Actions workflow `.github/workflows/windows-installer.yml` does the
same on `windows-latest`, runs the tests and the program's selftest, and
uploads the program as an artifact. Nothing it downloads is committed.

## ffmpeg's licence

`HaloCEVitaInstaller.exe` includes ffmpeg (FFmpeg 9.0.2, gyan.dev's
essentials build), which is licensed under the GNU GPL version 3, like this
project. Its LICENSE and README.txt are inside the program next to it; the
README names the exact FFmpeg source it was built from. FFmpeg's source:
<https://ffmpeg.org/download.html#get-sources> (the release tarball
`ffmpeg-9.0.2.tar.xz`), and gyan.dev's build notes:
<https://www.gyan.dev/ffmpeg/builds/>. Each release that attaches the
program says so in its notes.
