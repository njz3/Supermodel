"""Windows release of Supermodel, as an official one: a single supermodel.exe, with SDL2, SDL2_net, zlib and the C
runtime linked in, packaged as the official pkg target does (Makefiles/Rules.inc), in
pkg/supermodel-VERSION-windows.zip.

Usage (from anywhere): python Scripts/release_windows.py [--skip-build]

The build uses MSVC (the newest Visual Studio, found with vswhere, or the VsDevCmd.bat given in VSDEVCMD) and goes
to MSVC/x64/ReleaseStatic/, without touching the projects: SDL2 and SDL2_net are rebuilt as static libraries (their
ConfigurationType overridden, no DLL exports), everything is compiled with /MT, and the version is compiled in as the
Makefiles set it, 0.3a-YYYYMMDD-git-HASH from HEAD. The usual DLL build, in MSVC/x64/Release/, is left alone.
The 32-bit ASPI DLL for Real3D Pro-1000 host programs, wnaspi32.dll, which the official package carries beside
supermodel.exe (Makefiles/Makefile.Win32 builds it with a 32-bit MinGW), is built with MSVC's x86 compiler and its C
run-time linked in, in MSVC/x86/ReleaseStatic/. CHANGES.txt comes from Scripts/changelog.sh, which needs bash (Git
for Windows)."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MSVC = os.path.join(REPO, "MSVC")
OUT = os.path.join(MSVC, "x64", "ReleaseStatic")
OUT32 = os.path.join(MSVC, "x86", "ReleaseStatic")
VSWHERE = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                       "Microsoft Visual Studio", "Installer", "vswhere.exe")
# The system libraries SDL2 and SDL2_net need once linked in
STATIC_LIBS = "setupapi.lib winmm.lib imm32.lib version.lib ws2_32.lib iphlpapi.lib"


def git(*args):
    return subprocess.run(["git", "-C", REPO, *args], capture_output=True, text=True, check=True).stdout.strip()


def version():
    """As Makefiles/Rules.inc's set_version: 0.3a-<date of HEAD>-git-<short hash>."""
    return f"0.3a-{git('show', '-s', '--format=%cd', '--date=format:%Y%m%d', 'HEAD')}-git-{git('rev-parse', '--short', 'HEAD')}"


def vs_environment(arch="amd64"):
    """The environment of a Visual Studio developer prompt for arch (amd64 or x86)."""
    devcmd = os.environ.get("VSDEVCMD")
    if not devcmd:
        # The highest version, whatever the installer thinks of it (-all: an installation it marks incomplete after an
        # interrupted update still builds; -latest is the last installed, not the newest): the projects need Visual
        # Studio 2026's toolset, v145
        found = json.loads(subprocess.run([VSWHERE, "-all", "-products", "*", "-format", "json"],
                                          capture_output=True, text=True, check=True).stdout)
        found = [vs for vs in found
                 if os.path.exists(os.path.join(vs["installationPath"], "Common7", "Tools", "VsDevCmd.bat"))]
        newest = max(found, key=lambda vs: [int(n) for n in vs["installationVersion"].split(".")])
        devcmd = os.path.join(newest["installationPath"], "Common7", "Tools", "VsDevCmd.bat")
    out = subprocess.run(["cmd", "/d", "/c", devcmd, f"-arch={arch}", ">nul", "&&", "set"],
                         capture_output=True, text=True, check=True).stdout
    env = {}
    for line in out.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            env[key] = value
    return env


def msbuild(env, project, extra_env, *props):
    run_env = dict(env)
    run_env.update(extra_env)
    exe = shutil.which("msbuild", path=env.get("Path", env.get("PATH", "")))
    subprocess.run([exe, os.path.join(MSVC, project), "/t:Rebuild", "/m", "/nr:false", "/v:minimal",
                    "/p:Configuration=Release", "/p:Platform=x64", f"/p:SolutionDir={MSVC}{os.sep}",
                    f"/p:OutDir={OUT}{os.sep}", "/p:IntDir=x64\\ReleaseStatic\\",
                    "/p:VCPkgLocalAppDataDisabled=true", *props], env=run_env, check=True)


def build(v):
    env = vs_environment()
    cl = {"CL": f'/DSUPERMODEL_VERSION#\\"{v}\\"'}
    # SDL2 and SDL2_net as static libraries, without their DLL exports
    for project in ("SDL\\SDL.vcxproj", "SDLnet\\SDL_net.vcxproj"):
        msbuild(env, project, {**cl, "_CL_": "/MT /UDLL_EXPORT"}, "/p:ConfigurationType=StaticLibrary")
    # The other libraries are static already
    for project in ("ZLib\\ZLib.vcxproj", "SDLmain\\SDLmain.vcxproj", "Musashi68K\\Musashi68K.vcxproj"):
        msbuild(env, project, {**cl, "_CL_": "/MT"})
    msbuild(env, "Supermodel.vcxproj", {**cl, "_CL_": "/MT", "_LINK_": STATIC_LIBS}, "/p:BuildProjectReferences=false")
    build_wnaspi32()


def build_wnaspi32():
    """wnaspi32.dll, 32-bit, as Makefiles/Makefile.Win32 builds it: its exports from its .def file, only Windows
    system DLLs needed."""
    env = vs_environment("x86")
    src = os.path.join(REPO, "Src", "OSD", "Windows", "wnaspi32")
    if os.path.exists(OUT32):
        shutil.rmtree(OUT32)
    os.makedirs(OUT32)
    exe = shutil.which("cl", path=env.get("Path", env.get("PATH", "")))
    subprocess.run([exe, "/nologo", "/LD", "/MT", "/O2", "/EHsc", "/std:c++17", "/W3", "/I" + os.path.join(REPO, "Src"),
                    os.path.join(src, "wnaspi32.cpp"), os.path.join(src, "ConnectDialog.cpp"), "/Fe:wnaspi32.dll",
                    "/link", "/DEF:" + os.path.join(src, "wnaspi32.def"), "ws2_32.lib", "user32.lib"],
                   cwd=OUT32, env=env, check=True)


def package(v):
    """Staged as Makefiles/Rules.inc's pkg/stage, zipped as the release workflow names it."""
    pkg = os.path.join(REPO, "pkg")
    stage = os.path.join(pkg, f"supermodel-{v}")
    zip_path = os.path.join(pkg, f"supermodel-{v}-windows.zip")
    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(stage)
    shutil.copy2(os.path.join(OUT, "Supermodel.exe"), os.path.join(stage, "supermodel.exe"))
    wnaspi32 = os.path.join(OUT32, "wnaspi32.dll")
    if os.path.exists(wnaspi32):
        shutil.copy2(wnaspi32, stage)
    else:
        print("no MSVC/x86/ReleaseStatic/wnaspi32.dll: the package goes without it", file=sys.stderr)
    for doc in ("README.txt", "LICENSE.txt"):
        shutil.copy2(os.path.join(REPO, "Docs", doc), stage)
    # Config (without the generated Supermodel.ini) and Assets: what git tracks
    for rel in git("ls-files", "Config", "Assets").split():
        dest = os.path.join(stage, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(os.path.join(REPO, rel), dest)
    for folder, note in (("NVRAM", "NVRAM files go here."), ("Saves", "Save states go here."),
                         ("ROMs", "Recommended (but not mandatory) location for ROM sets.")):
        os.makedirs(os.path.join(stage, folder))
        with open(os.path.join(stage, folder, "DIR.txt"), "w", newline="\n") as f:
            f.write(note + "\n")
    bash = shutil.which("bash") or os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "Git", "bin",
                                                "bash.exe")
    if os.path.exists(bash):
        subprocess.run([bash, "Scripts/changelog.sh", f"pkg/supermodel-{v}/CHANGES.txt"], cwd=REPO, check=True)
    else:
        print("bash not found: no CHANGES.txt", file=sys.stderr)
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for root, dirs, files in os.walk(stage):
            dirs.sort()
            for name in sorted(files):
                full = os.path.join(root, name)
                z.write(full, os.path.relpath(full, pkg).replace("\\", "/"))
    shutil.rmtree(stage)
    return zip_path


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--skip-build", action="store_true",
                    help="package the last build of MSVC/x64/ReleaseStatic/ and MSVC/x86/ReleaseStatic/")
    a = ap.parse_args()
    v = version()
    if git("status", "--porcelain", "--untracked-files=no"):
        print(f"warning: the working tree has changes that {v} does not name", file=sys.stderr)
    print("Supermodel", v)
    if not a.skip_build:
        build(v)
    zip_path = package(v)
    print(zip_path, os.path.getsize(zip_path), "bytes")


if __name__ == "__main__":
    main()
