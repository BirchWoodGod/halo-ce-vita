"""Ninja rules for the PlayStation Vita build (``ninja vita``).

The game and the XDK-facing platform layer (port/linux/src, shared with the
Linux and Android builds) are compiled by clang for ARMv7 with the game's
MSVC-compatible ABI: 32-bit enums, 16-bit wchar_t, signed char, and no
alignment assumed for pointer accesses (the game reads floats from 2-byte
aligned buffers, which VLDR would fault on). The Vita side (port/vita/host)
is compiled by VitaSDK's GCC with the SDK's own ABI (small enums, 32-bit
wchar_t), because the Sce headers' structures depend on it; the two halves
meet only through port/linux/src/posix.h and port/vita/include/vita_host.h,
whose parameters are 32-bit scalars and pointers. See port/vita/README.md.
"""

import json
import os
from pathlib import Path
from typing import Any, Dict, List, Optional

from .ninja_syntax import Writer
from .vita_shader_generator_id import SOURCES as SHADER_GENERATOR_NAMES
from .linux_build import (GAME_FLAGS, PLATFORM_FLAGS, XDK_INCLUDE, TOML_DIR, KCP_DIR, ZLIB_DIR, ZLIB_SOURCES,
                          ZLIB_DEFINES, MONOCYPHER_DIR, MONOCYPHER_SOURCES, musl_math_sources,
                          MUSL_MATH_DIR, ANDROID_VARIADIC_PROTOTYPE_FILES, xdk_headers, _quote)

LINUX_DIR = Path("port/linux")
VITA_DIR = Path("port/vita")
BUILD = Path("build/vita")
# what tools/vita_shader_generator_id.py hashes
SHADER_GENERATOR_SOURCES = [Path(name) for name in SHADER_GENERATOR_NAMES]

TITLE_ID = "HCEV00001"
TITLE = "Halo CE"
# the version the LiveArea and the system show (APP_VER, "XX.YY")
APP_VERSION = "01.03"

# The game's directories compiled without -fmax-type-align=1 (configure.py
# --vita-aligned hot; EXPERIMENTAL, the default is none). On the hardware
# it crashed in the b30 fight (Oct 2): actions.c passed a stack byte array
# standing in for a struct path_collision_result, which clang had placed
# at an odd address, to actor_move_try_evasion_vector, whose VLDR of the
# result's point faulted. That one is fixed (the arrays are aligned now),
# but the game has more storage typed as bytes and read as structures, and
# no sweep proves there is no other: the x86 UBSan sweep below saw nothing,
# its stack layout differing. The flag makes clang take every pointer
# as unaligned, so each float the game loads or stores through one goes via
# an integer register and a transfer, which stalls the Cortex-A9 (and its
# double-word copies become byte work); it is needed for code that reads
# floats from 2-byte aligned data (the script engine, hs/). A UBSan
# alignment sweep of every campaign level (x86 harness, a scripted player,
# ~4000 ticks each) saw misaligned accesses only in hs/, cseries/
# debug_memory.c and the one file excepted below. Pi (Cortex-A72, which
# minds the transfers less than the Vita's A9), deterministic b30 bench:
# game tick -6.6%.
VITA_ALIGNED_DIRS = (
    "source/objects/", "source/physics/", "source/ai/", "source/render/", "source/rasterizer/",
    "source/math/", "source/structures/", "source/models/", "source/units/", "source/items/",
    "source/effects/", "source/camera/", "source/game/", "source/memory/",
)
# ... but for these, whose single misaligned access the sweep did see (a
# 32-bit read of a byte bit vector at an odd address)
VITA_ALIGNED_EXCEPTIONS = {"source/physics/breakable_surfaces.c"}

# The MSVC/Xbox ABI of LINUX_ABI_FLAGS, on 32-bit ARM
VITA_ABI_FLAGS = [
    "--target=armv7a-none-eabihf",
    "-mcpu=cortex-a9",
    "-mfpu=neon",
    "-mfloat-abi=hard",
    "-mthumb",
    # sqrtf and friends inline as VFP instructions instead of libm calls
    # that would set errno (the results are the same: IEEE exact)
    "-fno-math-errno",
    "-D__vita__",
    "-DHALO_VITA=1",
    "-DHALO_RELOCATABLE_TAG_CACHE=1",
    "-fms-extensions",
    "-fshort-wchar",
    "-fno-short-enums",
    "-fsigned-char",
    "-fmax-type-align=1",
    "-femulated-tls",
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-fno-omit-frame-pointer",
    "-ffp-contract=off",
    "-ffunction-sections",
    "-fdata-sections",
    "-O2",
    "-g",
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]

HOST_FLAGS = [
    "-mcpu=cortex-a9",
    "-mfpu=neon",
    "-mthumb",
    "-O2",
    "-g",
    "-Wall",
    "-ffunction-sections",
    "-fdata-sections",
]

# the platform layer's files the Vita does not build, or builds its own of
LINUX_SOURCES_REPLACED = {
    "posix_files.c", "posix_net.c", "posix_update.c", "posix_upnp.c", "posix_profile.c",  # port/vita/host
    "memory_watch.c",  # port/vita/platform/vita_memory_watch.c
    "bink_null.c",  # port/vita/platform/bink_vita.c (the Vita's video player)
    # the OpenGL renderer: port/vita/platform/d3d8_gxm.c, nv2a_*_cg.c, vita_textures.c
    "d3d8_gl.c", "gl_functions.c", "nv2a_vsh.c", "nv2a_psh.c", "xbox_textures.c",
}

VITA_LIBRARIES = [
    "SDL3", "SceGxm_stub", "SceDisplay_stub", "SceCtrl_stub", "SceAudio_stub", "SceAudioIn_stub",
    "SceSysmodule_stub", "SceHid_stub", "SceTouch_stub", "SceMotion_stub", "ScePower_stub",
    "SceAppUtil_stub", "SceAppMgr_stub", "SceCommonDialog_stub", "SceIme_stub", "SceKernelDmacMgr_stub",
    "SceShaccCg_stub", "SceAvPlayer_stub", "SceCamera_stub", "SceRtc_stub", "SceNet_stub", "SceNetCtl_stub", "ScePspnetAdhoc_stub", "pthread", "m", "c",
]


def vita_sdk(sln: Any) -> Path:
    explicit = getattr(sln, "vita_sdk", None) or os.environ.get("VITASDK")
    return Path(explicit) if explicit else Path.home() / "vitasdk"


def generate_vita_build(n: Writer, sln: Any) -> None:
    config_path = LINUX_DIR / "port.json"
    sdk = vita_sdk(sln)
    if not config_path.is_file() or not (VITA_DIR / "host").is_dir():
        return
    if not (sdk / "bin" / "arm-vita-eabi-gcc").is_file():
        n.comment(f"Vita build: no VitaSDK at {sdk} (set VITASDK)")
        return
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))
    sysroot = sdk / "arm-vita-eabi"
    semantics_header = Path("build/linux/halo_msvc_semantics.h")
    platform_semantics_header = Path("build/linux/platform_msvc_semantics.h")
    prefix_header = LINUX_DIR / "include" / "halo_linux_prefix.h"
    obj_dir = BUILD / "obj"

    n.comment("PlayStation Vita build (ninja vita); see port/vita/README.md")
    n.variable("vita_cc", getattr(sln, "vita_cc", None) or "clang")
    n.variable("vita_sdk_bin", str(sdk / "bin"))
    n.rule(
        name="vita_cc",
        command="$vita_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="VITA CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="vita_host_cc",
        command="$vita_sdk_bin/arm-vita-eabi-gcc -MMD -MF $out.d $cflags -c $in -o $out",
        description="VITA HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    # link-time optimisation of the clang side: the game and platform objects
    # are bitcode, partially linked into one relocatable object by lld
    n.rule(
        name="vita_lto",
        command="ld.lld -r -m armelf_linux_eabi --lto-O3 -mllvm -mcpu=cortex-a9 -mllvm -float-abi=hard -mllvm -emulated-tls -o $out @$out.rsp",
        description="VITA LTO $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.rule(
        name="vita_link",
        command=("$vita_sdk_bin/arm-vita-eabi-gcc -Wl,-q,--defsym=__sce_headroom=0x1000 "
                 "-Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--gc-sections "
                 "-o $out @$out.rsp $libs"),
        description="VITA LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.rule(name="vita_velf", command="$vita_sdk_bin/vita-elf-create $in $out", description="VITA VELF $out")
    n.rule(name="vita_eboot", command="$vita_sdk_bin/vita-make-fself -s -c $in $out", description="VITA EBOOT $out")
    n.rule(
        name="vita_sfo",
        command=f'$vita_sdk_bin/vita-mksfoex -s TITLE_ID={TITLE_ID} -s APP_VER={APP_VERSION} -d ATTRIBUTE2=12 "{TITLE}" $out',
        description="VITA SFO $out",
    )
    n.rule(
        name="vita_vpk",
        command="$vita_sdk_bin/vita-pack-vpk -s $sfo -b $eboot $assets $out",
        description="VITA VPK $out",
    )

    lto = getattr(sln, "port_lto", "off") != "off"
    abi = " ".join(VITA_ABI_FLAGS + [f"--sysroot={_quote(sysroot)}", f"-isystem {_quote(sysroot / 'include')}"] +
                   (["-DHALO_RELEASE"] if getattr(sln, "port_release", False) else []) +
                   (["-flto"] if lto else []))
    port_include = LINUX_DIR / "include"
    vita_include = VITA_DIR / "include"
    sdk_flags = f"-idirafter {XDK_INCLUDE}"
    excluded = set(config.get("exclude_sources", []))
    objects: List[Path] = []
    clang_objects: List[Path] = []
    implicit = [*xdk_headers(), prefix_header, semantics_header, platform_semantics_header]

    def add(source: Path, rule: str, cflags: str, extra: Optional[List[Path]] = None) -> None:
        obj = obj_dir / source.with_suffix(".o")
        (clang_objects if rule == "vita_cc" and lto else objects).append(obj)
        n.build(outputs=obj, rule=rule, inputs=source, implicit=(implicit if rule == "vita_cc" else []) + (extra or []),
                variables={"cflags": cflags})

    # the game
    for proj in sln.projects:
        if proj.name not in config["projects"]:
            continue
        options = proj.options
        defines = " ".join(f"-D{d}" for d in options.get("defines") or [])
        includes = " ".join(
            f"-I{_quote(d)}" for d in options.get("include_dirs") or [] if Path(d) != Path("xbox/include")
        )
        game_cflags = " ".join([
            abi, " ".join(GAME_FLAGS), f"-include {prefix_header}", f"-include {semantics_header}",
            defines, f"-I{vita_include}", f"-I{port_include}",
            # the headers of the port's own game units (port/linux/game)
            f"-iquote {Path(config['game_sources'])}", includes, sdk_flags,
        ])
        for obj in proj.objects:
            name = str(obj.file_path).replace(os.sep, "/")
            if obj.status.name == "Missing" or name in excluded or obj.file_path.suffix.lower() != ".c":
                continue
            cflags = game_cflags
            if (getattr(sln, "vita_aligned", "none") == "hot" and name.startswith(VITA_ALIGNED_DIRS) and
                    name not in VITA_ALIGNED_EXCEPTIONS):
                cflags = cflags.replace(" -fmax-type-align=1 ", " ")
            # reals travel in their own registers on hard-float ARM, as on Android
            if name in ANDROID_VARIADIC_PROTOTYPE_FILES:
                cflags += " -include port/android/include/halo_android_variadic_prototypes.h"
            # the process's main is the host's (port/vita/host/vita_main.c)
            if name == "source/shell/shell_xbox.c":
                cflags += " -Dmain=halo_main"
            add(obj.file_path, "vita_cc", cflags)
        for source in sorted(Path(config["game_sources"]).glob("*.c")):
            add(source, "vita_cc", game_cflags)

    # the platform layer shared with Linux, and the Vita's game-ABI parts of it
    platform_dir = Path(config["platform_sources"])
    platform_cflags = " ".join([
        abi, " ".join(PLATFORM_FLAGS), "-w", f"-include {prefix_header}", f"-include {platform_semantics_header}",
        f"-I{platform_dir}", f"-I{vita_include}", f"-I{port_include}", f"-I{TOML_DIR}", f"-I{KCP_DIR}",
        f"-I{MONOCYPHER_DIR}", "-Isource -Isource/cseries", sdk_flags,
    ])
    for source in sorted(platform_dir.glob("*.c")):
        if source.name not in LINUX_SOURCES_REPLACED:
            add(source, "vita_cc", platform_cflags)
    for source in sorted((VITA_DIR / "platform").glob("*.c")):
        add(source, "vita_cc", platform_cflags)
    add(TOML_DIR / "tomlc17.c", "vita_cc", " ".join([abi, "-std=gnu11", "-w"]))
    add(KCP_DIR / "ikcp.c", "vita_cc", " ".join([abi, "-std=gnu11", "-w"]))
    # (from OpenCE) the port's zlib, which inflates the maps
    # (cache_files_decompress_windows.c) instead of the game's own 1.1.3
    for name in ZLIB_SOURCES:
        add(ZLIB_DIR / name, "vita_cc", " ".join([abi, "-std=gnu11", *ZLIB_DEFINES, "-w"]))
    # internet play's signatures and password keys (port/third_party/monocypher): its
    # own word-aligned loads (the game's -fmax-type-align=1 would make Argon2 crawl)
    for name in MONOCYPHER_SOURCES:
        add(MONOCYPHER_DIR / name, "vita_cc", " ".join([abi.replace("-fmax-type-align=1", ""), "-std=gnu11", "-w"]))
    for source in musl_math_sources():
        add(source, "vita_cc", " ".join([abi, "-std=gnu11", "-w", f"-I{MUSL_MATH_DIR}/include",
                                          f"-include {MUSL_MATH_DIR}/include/libm.h"]))

    # the shader generator id (vita_gxm.c's shader cache): a hash of the sources that write the Cg
    generator_header = BUILD / "gen" / "vita_shader_generator_id.h"
    n.rule(name="vita_shader_generator_id",
           command="python3 tools/vita_shader_generator_id.py --header $out",
           description="VITA SHADER GENERATOR ID $out", restat=True)
    n.build(outputs=generator_header, rule="vita_shader_generator_id",
            implicit=[Path("tools/vita_shader_generator_id.py"), *SHADER_GENERATOR_SOURCES])

    # the Vita side, with the SDK's ABI
    host_cflags = " ".join(HOST_FLAGS + [f"-I{platform_dir}", f"-I{vita_include}", f"-I{generator_header.parent}"])
    for source in sorted((VITA_DIR / "host").glob("*.c")):
        add(source, "vita_host_cc", host_cflags, [generator_header] if source.name == "vita_gxm.c" else None)
    # the files half of the Linux host boundary works as it is on newlib
    add(LINUX_DIR / "src" / "posix_files.c", "vita_host_cc", host_cflags + " -D_GNU_SOURCE")

    if lto:
        lto_object = BUILD / "halo_lto.o"
        n.build(outputs=lto_object, rule="vita_lto", inputs=clang_objects)
        objects.insert(0, lto_object)
    elf = BUILD / "halo.elf"
    velf = BUILD / "halo.velf"
    eboot = BUILD / "eboot.bin"
    sfo = BUILD / "param.sfo"
    vpk = BUILD / "halo.vpk"
    n.build(outputs=elf, rule="vita_link", inputs=objects,
            variables={"libs": " ".join(f"-l{lib}" for lib in VITA_LIBRARIES)})
    n.build(outputs=velf, rule="vita_velf", inputs=elf)
    n.build(outputs=eboot, rule="vita_eboot", inputs=velf)
    n.build(outputs=sfo, rule="vita_sfo", implicit=[Path("tools/vita_build.py")])
    assets = []
    for asset in sorted((VITA_DIR / "sce_sys").rglob("*")) if (VITA_DIR / "sce_sys").is_dir() else []:
        if asset.is_file():
            assets.append(f"-a {_quote(asset)}={_quote(asset.relative_to(VITA_DIR))}")
    # files at the root of app0: (the shipped shader programs, port/vita/app0/shaders.pak:
    # tools/vita_shader_pack.py)
    # The pack goes in stamped with this build's generator id (made again when
    # that id changes), so the VPK's pack names its eboot's generator; the stamp
    # notes it when the pack in the tree was made for another one.
    n.rule(name="vita_shader_pack_stamp",
           command=f"python3 tools/vita_shader_pack.py --stamp $in --generator-header {generator_header} --output $out",
           description="VITA SHADER PACK $out", restat=True)
    app0_files = []
    for asset in sorted((VITA_DIR / "app0").rglob("*")) if (VITA_DIR / "app0").is_dir() else []:
        if asset.is_file():
            source = asset
            if asset.name == "shaders.pak":
                source = BUILD / "app0" / asset.relative_to(VITA_DIR / "app0")
                n.build(outputs=source, rule="vita_shader_pack_stamp", inputs=asset,
                        implicit=[generator_header, Path("tools/vita_shader_pack.py")])
            assets.append(f"-a {_quote(source)}={_quote(asset.relative_to(VITA_DIR / 'app0'))}")
            app0_files.append(source)
    n.build(outputs=vpk, rule="vita_vpk", inputs=[eboot, sfo], implicit=app0_files,
            variables={"sfo": str(sfo), "eboot": str(eboot), "assets": " ".join(assets)})
    n.build(outputs="vita", rule="phony", inputs=[vpk])
    n.newline()
