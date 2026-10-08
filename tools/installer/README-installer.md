# Install helper for Halo CE for PS Vita

`halo_ce_vita_installer.py` gathers your own game files in the layout the
Vita build needs (the main [README](../../README.md#install) gives it) and
copies them to the Vita over VitaShell's FTP. It is one Python file for
Windows, Linux and macOS; `HaloCEVitaInstaller.exe` is the same tool as one
Windows program, with ffmpeg inside it.

**No game data comes with it.** Every step reads your own files: your Xbox
disc image, your Halo PC files.

## Running it

- **Windows:** download `HaloCEVitaInstaller.exe` from the
  [releases](https://github.com/BirchWoodGod/halo-ce-vita/releases/latest)
  (it is attached next to `halo.vpk`) and double-click it. It is one
  program of about 47 MB that unpacks itself to a temporary folder at each
  start, so it takes a few seconds to open. It is not signed: Windows
  SmartScreen says "Windows protected your PC" the first time (click
  "More info", then "Run anyway"), and some antivirus programs may flag it
  wrongly, as they often do with Python programs packed this way. The
  Python file below is the same tool, readable.
- **Any system with Python 3.8 or later:** `python3 halo_ce_vita_installer.py`
  opens the window (on Linux, tkinter may be a separate package:
  `python3-tk` or `tk`). The movie step needs ffmpeg installed.
- **Without the window:** `python3 halo_ce_vita_installer.py --help` lists
  the same steps as commands (see below).

The window walks through five steps. Each can be skipped with **Next**, and
each can be run again: files already in place are kept.

The files are gathered in an output folder first (by default
`Documents/Halo CE Vita files`, about 2.5 GB with everything), laid out as
on the Vita:

```
Halo CE Vita files/
    default.xbe          -> ux0:data/haloce-vita/default.xbe
    maps/*.map           -> ux0:data/haloce-vita/maps/
    movies/*.mp4         -> ux0:data/haloce-vita/movies/
```

Custom maps you put in that `maps` folder yourself are copied too.

## The steps

1. **Xbox game files.** Choose your Xbox Halo disc image (`.iso`, `.xiso`:
   an XISO made by extract-xiso, or a full disc image) or an already
   unpacked game folder. The `maps` folder and `default.xbe` are taken out.
   The disc image is read by the tool itself (a small XDVDFS reader): no
   extract-xiso needed. It checks that the campaign maps and the menu
   (`ui.map`, `a10.map` ... `d40.map`) and `default.xbe` are there, so the PC
   version's maps are not taken by mistake.
2. **Movies (optional).** The disc's Bink movies (`bink/*.bik`) are
   converted with ffmpeg to H.264 MP4s, 640 wide, as the README's Movies
   section does (Standard = its first command, Baseline profile; Better
   quality = its High profile command). The game runs without them: each
   missing movie is skipped (no intro, no attract videos, no credits movie)
   and the main menu comes up ready to play. Skip the step with **Next** to
   save the few minutes it takes.
3. **Halo PC files** (for online play and Custom Edition maps):
   `bitmaps.map`, `sounds.map` and `loc.map`, from one of:
   - **Halo: The Master Chief Collection** on Steam: its
     `halo1/maps/custom_edition` folder, found by itself in your Steam
     libraries (`libraryfolders.vdf`), or chosen with Folder... Not the maps
     directly in `halo1/maps`: those are MCC's own and do not work.
   - **The Halo Custom Edition installer**
     ([halomaps.org](https://www.halomaps.org/hce/detail.cfm?fid=410)).
     Whatever its file name: it is recognised by what is in it. The English
     `halocesetup_en_1.00.exe` is known by its SHA-256 (and the three maps
     unpacked from it are checked against theirs); any other Windows program
     whose resources hold a cabinet with `maps\bitmaps.map`, `sounds.map` and
     `loc.map` is taken as an installer too. The cabinet (LZX) is unpacked by
     the tool itself, in about 20 seconds, with nothing to install. Anything
     else is reported as "This isn't the Halo Custom Edition installer".
   - **A folder** that has them, such as a Custom Edition install's `maps`.

   Each map's header is checked (the right kind of resource map).
4. **Copy to the Vita.** On the Vita, open VitaShell and press SELECT: it
   shows an address like `ftp://192.168.1.20:1337`. Type it in the tool and
   press Copy. **Keep the Vita awake** while it copies (touch the screen now
   and then, or turn off auto-standby in the Vita's power settings): if it
   sleeps, the connection drops. The tool tries again by itself a few times;
   if it gives up, press Copy again: files already on the Vita with the right
   size are skipped, so it goes on from where it stopped. Every file is
   checked by its size on the Vita after copying. "Copy files already on the
   Vita again" replaces them all.
5. **The VPK.** Download `halo.vpk` from the
   [releases](https://github.com/BirchWoodGod/halo-ce-vita/releases/latest)
   (a VPK next to the tool is picked up by itself). The tool copies it to
   `ux0:data/`. Then, on the Vita, leave FTP mode, open `ux0:data/` in
   VitaShell, press Cross on the VPK and confirm. The tool cannot install
   it: only VitaShell on the Vita can.

Still needed on the Vita, as the README says: HENkaku/Enso, VitaShell, and the
shader compiler `ur0:data/libshacccg.suprx` (ShaRKF00D extracts it).

## Command line

```
python3 halo_ce_vita_installer.py [--out FOLDER] xbox "Halo.iso"
python3 halo_ce_vita_installer.py [--out FOLDER] movies "Halo.iso" [--quality high] [--ffmpeg PATH]
python3 halo_ce_vita_installer.py [--out FOLDER] pc-files --mcc [FOLDER]
python3 halo_ce_vita_installer.py [--out FOLDER] pc-files --installer "any name.exe"
python3 halo_ce_vita_installer.py [--out FOLDER] pc-files --folder FOLDER
python3 halo_ce_vita_installer.py check-installer "any name.exe"
python3 halo_ce_vita_installer.py [--out FOLDER] upload 192.168.1.20 [--replace]
python3 halo_ce_vita_installer.py vpk 192.168.1.20 [halo.vpk]
python3 halo_ce_vita_installer.py selftest
```

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
- No shell is used to run anything; ffmpeg is run with an argument list.
- The Windows build downloads ffmpeg from one fixed address and refuses it
  unless its SHA-256 matches (`build_windows.py`).

## Tests

```
python3 -m pip install -r requirements-dev.txt
python3 -m pytest tests
```

The tests build everything they read: a small XDVDFS image, an LZX
compressor (the decoder's other half) and cabinets, a Windows program with a
cabinet in its resources, fake resource maps, a stand-in ffmpeg, and a local
FTP server that answers like VitaShell (its LIST format, no NLST, no SIZE, a
connection that drops on demand, doubled transfer replies). The window test
runs where there is a display.

## Building the Windows program

On Windows, from the repository:

```
tools\installer\build_windows.bat
```

or `python tools/installer/build_windows.py` (`--no-ffmpeg` leaves ffmpeg
out). It downloads FFmpeg 9.0.2 (gyan.dev's essentials build, GPL v3) from a
fixed address, checks its SHA-256, and builds
`tools/installer/dist/HaloCEVitaInstaller.exe` with PyInstaller; ffmpeg's
LICENSE and README.txt (which names its source) go inside next to it. The
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
