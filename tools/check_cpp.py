#!/usr/bin/env python3
"""
check_cpp.py - syntax-check engine C++ with clang on Linux and diff the errors against a
baseline commit.

The engine only builds with MSVC on Windows. This is the next best thing: clang -fsyntax-only
in MSVC-compatibility mode against the mingw-w64 Windows headers and the repository's
DirectX SDK headers. It cannot link, and it reports plenty of errors MSVC would not
(environment noise), so the useful output is the DIFF: errors present after a change that were
not there before it. A clean diff means the change introduced no syntax, type or name errors
that clang can see.

How it works
  1. `git worktree`-free: `git archive` the two commits into a scratch folder.
  2. Make a case-folded mirror of each: every path lowercased, every #include line lowercased
     with backslashes turned into slashes (Windows include paths are case-insensitive and use
     backslashes; Linux paths are neither).
  3. clang++ -fsyntax-only each requested translation unit with the project's include dirs and
     defines, in both trees.
  4. Compare error messages per file with line numbers removed (so code that merely moved does
     not count as new).

Requirements: clang++, git, and mingw-w64 with its C++ library headers
(apt install g++-mingw-w64-x86-64-posix, which pulls in mingw-w64-x86-64-dev).

Usage
  python tools/check_cpp.py --base 5dffea1 --head HEAD --project R4 \
      src/Layers/xrRenderPC_R4/r4.cpp src/Layers/xrRender/R_sun.cpp ...
  python tools/check_cpp.py --base HEAD~1 --changed      # every .cpp changed since --base,
                                                         # compiled for every project using it
  --dry-run lists what would be compiled.

Known limits / caveats
  - Errors caused by MSVC-only extensions or missing Windows SDK pieces appear in both trees and
    cancel out; but a NEW error on a line that touches such an API can be noise too. Read new
    errors, don't trust the count blindly.
  - clang stops reporting after -ferror-limit errors per file (set high below); a file that
    fails very early (missing header) checks nothing after that point.
  - Templates are only checked where instantiated in the TU.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

MINGW_INCLUDE = "/usr/x86_64-w64-mingw32/include"  # only checked for presence
# Stand-ins for MSVC-only library headers (ppl.h, concurrent_*.h, hash_map, hash_set)
SHIMS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cpp_shims")
CLANG = "clang++"
JOBS = os.cpu_count() or 4

# Per renderer project: include dirs (relative to the repository root) and defines, taken from
# the .vcxproj files (Release). Projects that share a source file get it checked once per
# project, because USE_DX10 / USE_DX11 change what compiles.
# The vcxproj files define XRCORE_PROFILER (= 1, Optick), which includes the optick-git
# submodule. `git archive` never contains submodule files, so every TU stopped at that fatal
# include and the before/after counts compared nothing past the precompiled header. The
# profiler macros only wrap events/tags, so the no-op backend is used for syntax checks.
PROFILER_DEFINE = "XRCORE_PROFILER=0"

PROJECTS = {
    "R4": {
        "dir": "src/Layers/xrRenderPC_R4",
        "includes": ["src/Layers/xrRenderPC_R4", "src/xrCore", "src", "sdk/include",
                     "sdk/include/dxsdk", "src/3rd party/imgui", "src/3rd party/luajit-2/src",
                     "src/3rd party/luabind", "src/3rd party", "sdk/include/nvapi",
                     "sdk/include/OpenAutomate"],
        "defines": ["USE_DX11", "WIN32", "NDEBUG", "_WINDOWS", "_USRDLL", "XRRENDER_R4_EXPORTS",
                    "_SECURE_SCL=0", PROFILER_DEFINE],
    },
    # xrGame (and the xrServerEntities sources it builds): include paths and defines from
    # src/xrGame/xrGame.vcxproj (Release|x64 / ProfiledDX11). Without this, xrGame files were
    # compiled with the renderer's settings, stopped at "'smart_cast.h' file not found" and
    # checked nothing past their precompiled header.
    "xrGame": {
        "dir": "src/xrGame",
        "includes": ["src/xrGame", "src", "src/xrCore", "src/xrServerEntities",
                     "src/3rd party/luajit-2/src", "src/3rd party/luabind", "sdk/include",
                     "src/3rd party/CxImage", "src/3rd party", "src/3rd party/imgui"],
        "defines": ["WIN32", "NDEBUG", "_WINDOWS", "_USRDLL", "XRGAME_EXPORTS", "dSINGLE", "MSVC",
                    "_SECURE_SCL=0", PROFILER_DEFINE],
    },
    "R3": {
        "dir": "src/Layers/xrRenderPC_R3",
        "includes": ["src/Layers/xrRenderPC_R3", "src/xrCore", "src", "sdk/include",
                     "sdk/include/dxsdk", "src/3rd party/imgui", "src/3rd party/luajit-2/src",
                     "src/3rd party/luabind", "src/3rd party", "sdk/include/nvapi",
                     "sdk/include/OpenAutomate"],
        "defines": ["USE_DX10", "WIN32", "NDEBUG", "_WINDOWS", "_USRDLL", "XRRENDER_R3_EXPORTS",
                    "_SECURE_SCL=0", PROFILER_DEFINE],
    },
}

# GNU-ABI Windows target: it brings mingw's libstdc++ and matching intrinsics headers. The MSVC
# target has no C++ standard library on Linux (fatal "'typeinfo' file not found", which stops
# the check before any engine code). Only -fms-extensions: -fms-compatibility and a defined
# _MSC_VER send clang's own intrinsics headers down MSVC-only paths (thousands of errors).
# The engine picks its MSVC code paths from M_VISUAL (xrCore/vector.h), defined directly.
CLANG_FLAGS = [
    "-fsyntax-only", "--target=x86_64-w64-mingw32", "-fms-extensions",
    "-fdelayed-template-parsing", "-std=gnu++17", "-ferror-limit=0", "-Wno-everything",
    "-DM_VISUAL", "-D_M_X64=100", "-D_WIN64",
]

# Errors clang reports for code MSVC accepts, verified as environmental (they hit every
# matching line of the original code too). A NEW error matching one of these is reported as
# "ignored (environment)" instead of failing the check. Each entry: (regex, why).
IGNORE_PATTERNS = [
    (r"cannot initialize a parameter of type 'IConsole_Command \*' with an rvalue of type 'CCC_",
     "console command classes do not resolve as IConsole_Command-derived under clang; every CMD1-4 "
     "registration in xrRender_console.cpp fails the same way, before and after any change"),
]

# ------------------------------------------------------------------------------------------

WIDE_STRINGIZE_RE = re.compile(r'\bL#\s*(\w)')
INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*)([<"])([^>"]+)([>"])', re.M)
TEXT_EXT = {".h", ".hpp", ".inl", ".c", ".cpp", ".cxx", ".hxx", ".ipp", ".tcc", ""}


def fold_tree(src, dst):
    """Copy src to dst with lowercased paths and lowercased, slash-normalised #include lines."""
    for root, dirs, files in os.walk(src):
        if ".git" in dirs:
            dirs.remove(".git")
        rel = os.path.relpath(root, src).lower()
        out_dir = os.path.join(dst, rel)
        os.makedirs(out_dir, exist_ok=True)
        for f in files:
            sp = os.path.join(root, f)
            dp = os.path.join(out_dir, f.lower())
            ext = os.path.splitext(f)[1].lower()
            if ext in TEXT_EXT:
                with open(sp, "rb") as fh:
                    data = fh.read().decode("utf-8", "surrogateescape")
                # MSVC accepts L#x (wide stringize, PIX_EVENT); standard C++ needs L"" #x
                data = WIDE_STRINGIZE_RE.sub(r'L"" #\1', data)
                data = INCLUDE_RE.sub(lambda m: m.group(1) + m.group(2) + m.group(3).replace("\\", "/").lower() + m.group(4), data)
                with open(dp, "wb") as fh:
                    fh.write(data.encode("utf-8", "surrogateescape"))
            # other files (binaries, resources) are not needed for syntax checks


def export_commit(repo, rev, dst, paths):
    os.makedirs(dst, exist_ok=True)
    a = subprocess.run(["git", "-C", repo, "archive", rev, "--"] + paths, capture_output=True, check=True)
    subprocess.run(["tar", "-x", "-C", dst], input=a.stdout, check=True)


def compile_tu(tree, mingw, project, tu):
    p = PROJECTS[project]
    args = [CLANG] + CLANG_FLAGS + ["-isystem", SHIMS]
    for inc in p["includes"]:
        args += ["-I", os.path.join(tree, inc.lower())]
    for d in p["defines"]:
        args += ["-D", d]
    path = os.path.join(tree, tu.lower())
    if not os.path.exists(path):
        return None
    r = subprocess.run(args + [path], capture_output=True, text=True, errors="replace",
                       cwd=os.path.join(tree, p["dir"].lower()))
    errs = []
    for line in r.stderr.splitlines():
        m = re.match(r"(.+?):\d+:\d+: (fatal )?error: (.*)", line)
        if m:
            f = os.path.relpath(m.group(1), tree) if m.group(1).startswith(tree) else os.path.basename(m.group(1))
            errs.append(f"{f}: {'fatal: ' if m.group(2) else ''}{m.group(3)}")
    return errs


def projects_for(tu):
    if "xrRenderPC_R4" in tu:
        return ["R4"]
    if "xrRenderPC_R3" in tu:
        return ["R3"]
    if "/xrRender/" in tu or "/xrRenderDX10/" in tu:
        return ["R4", "R3"]
    if "/xrGame/" in tu or "/xrServerEntities/" in tu:
        return ["xrGame"]
    return ["R4"]


def multiset_diff(after, before):
    left = list(before)
    new = []
    for e in after:
        if e in left:
            left.remove(e)
        else:
            new.append(e)
    return new, left


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tus", nargs="*", help="translation units (.cpp) relative to the repo root")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--base", required=True, help="baseline commit")
    ap.add_argument("--head", default="HEAD", help="commit to check (default HEAD); 'WORKTREE' = uncommitted files")
    ap.add_argument("--project", choices=sorted(PROJECTS), help="force one project for all TUs")
    ap.add_argument("--changed", action="store_true", help="add every .cpp changed between --base and --head")
    ap.add_argument("--show", type=int, default=8, help="new errors to print per TU")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    repo = os.path.abspath(a.repo)
    tus = list(a.tus)
    if a.changed:
        rng = [a.base] if a.head == "WORKTREE" else [a.base, a.head]
        out = subprocess.run(["git", "-C", repo, "diff", "--name-only"] + rng, capture_output=True, text=True, check=True).stdout
        tus += [l for l in out.splitlines() if l.endswith(".cpp") and l.startswith("src/")]
    tus = sorted(set(tus))
    jobs = [(tu, pr) for tu in tus for pr in ([a.project] if a.project else projects_for(tu))]
    if not jobs:
        sys.exit("nothing to check")
    if a.dry_run:
        for tu, pr in jobs:
            print(f"would check [{pr}] {tu}  ({a.base} vs {a.head})")
        return 0
    for tool in (CLANG, "git"):
        if not shutil.which(tool):
            sys.exit(f"error: {tool} not found")
    if not os.path.isdir(MINGW_INCLUDE):
        sys.exit(f"error: mingw-w64 headers not found at {MINGW_INCLUDE}")

    with tempfile.TemporaryDirectory() as tmp:
        raw = {k: os.path.join(tmp, "raw_" + k) for k in ("base", "head")}
        fold = {k: os.path.join(tmp, "fold_" + k) for k in ("base", "head")}
        export_commit(repo, a.base, raw["base"], ["src", "sdk/include"])
        if a.head == "WORKTREE":
            shutil.copytree(os.path.join(repo, "src"), os.path.join(raw["head"], "src"))
            shutil.copytree(os.path.join(repo, "sdk", "include"), os.path.join(raw["head"], "sdk", "include"))
        else:
            export_commit(repo, a.head, raw["head"], ["src", "sdk/include"])
        # git archive leaves submodules empty; both trees get the working copy's checkout
        # (run `git submodule update --init` first). They are dependencies, not what is compared.
        # -z: "key\nvalue\0" records. Keys and paths can contain spaces ("src/3rd party/...").
        sub = subprocess.run(["git", "-C", repo, "config", "-z", "--file", ".gitmodules", "--get-regexp", r"\.path$"],
                             capture_output=True, text=True).stdout
        for rec in filter(None, sub.split("\0")):
            path = rec.split("\n", 1)[1]
            src_path = os.path.join(repo, path)
            if not (os.path.isdir(src_path) and os.listdir(src_path)):
                print(f"WARNING: submodule {path} is not checked out; files including it will stop early")
                continue
            for k in raw:
                dst = os.path.join(raw[k], path)
                shutil.rmtree(dst, ignore_errors=True)
                shutil.copytree(src_path, dst, ignore=shutil.ignore_patterns(".git"))
        for k in raw:
            fold_tree(raw[k], fold[k])
        mingw = None  # clang locates the mingw sysroot itself for this target

        # Baseline for files that do not exist in --base (new files): a translation unit that only
        # includes the project's precompiled header, i.e. exactly the header noise every file has
        for pr in PROJECTS:
            stub = os.path.join(fold["base"], PROJECTS[pr]["dir"].lower(), "__check_cpp_stub.cpp")
            if os.path.isdir(os.path.dirname(stub)):
                with open(stub, "w") as fh:
                    fh.write('#include "stdafx.h"\n')

        def work(job):
            tu, pr = job
            before = compile_tu(fold["base"], mingw, pr, tu)
            if before is None:
                before = compile_tu(fold["base"], mingw, pr, PROJECTS[pr]["dir"] + "/__check_cpp_stub.cpp")
                before = [e for e in (before or []) if "__check_cpp_stub" not in e]
            return job, before, compile_tu(fold["head"], mingw, pr, tu)

        total_new = 0
        any_fatal = False
        with ThreadPoolExecutor(JOBS) as ex:
            for (tu, pr), before, after in ex.map(work, jobs):
                if after is None:
                    print(f"[{pr}] {tu}: missing in head")
                    continue
                before = before or []
                any_fatal |= any("fatal:" in e for e in after + before)
                new, fixed = multiset_diff(after, before)
                ignored = [e for e in new if any(re.search(p, e) for p, _ in IGNORE_PATTERNS)]
                new = [e for e in new if e not in ignored]
                total_new += len(new)
                tag = "OK" if not new else f"{len(new)} NEW"
                if ignored:
                    tag += f", {len(ignored)} ignored (environment, see IGNORE_PATTERNS)"
                print(f"[{pr}] {tu}: {tag}  (errors: before {len(before)}, after {len(after)}, gone {len(fixed)})")
                for e in new[: a.show]:
                    print("    + " + e)
        print(f"\nnew errors: {total_new}")
        if any_fatal:
            print("WARNING: a fatal error stopped at least one file early; its check is incomplete")
        return 1 if total_new else 0


if __name__ == "__main__":
    sys.exit(main())
