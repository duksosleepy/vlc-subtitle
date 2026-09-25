#!/usr/bin/env python3
#
# Helper script to set up MSVC environment and run Windows CI actions
# without external third-party GitHub Actions.
#

import argparse
import os
import subprocess
import sys
from pathlib import Path


def run(cmd, **kwargs):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    kwargs.setdefault("check", True)
    return subprocess.run(cmd, **kwargs)


def import_vs_env(arch="x64"):
    """Locate Visual Studio using vswhere and export environment variables to GITHUB_ENV."""
    vs_arch = "x86" if arch in ("x86", "Win32") else "x64"

    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere_path = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"

    if not vswhere_path.exists():
        sys.exit(f"Error: vswhere.exe not found at: {vswhere_path}")

    installation_path = run(
        [str(vswhere_path), "-latest", "-property", "installationPath"],
        capture_output=True,
        text=True,
    ).stdout.strip()

    if not installation_path:
        sys.exit("Error: Could not locate Visual Studio installation path.")

    vsdevcmd = Path(installation_path) / "Common7" / "Tools" / "vsdevcmd.bat"
    if not vsdevcmd.exists():
        sys.exit(f"Error: vsdevcmd.bat not found at: {vsdevcmd}")

    comspec = os.environ.get("COMSPEC", r"C:\Windows\system32\cmd.exe")
    cmd = f'"{comspec}" /s /c ""{vsdevcmd}" -arch={vs_arch} -no_logo && set"'

    output = run(cmd, capture_output=True, text=True).stdout

    github_env = os.environ.get("GITHUB_ENV")
    if not github_env:
        print("Warning: GITHUB_ENV is not set. Environment variables will only be printed.")
        print(output[:500] + "...")
        return

    with open(github_env, "a", encoding="utf-8") as env_file:
        for line in output.splitlines():
            if "=" not in line:
                continue
            name, value = line.split("=", 1)
            # Skip cmd-internal variables
            if name.startswith(("=", "PROMPT")):
                continue
            if "\n" in value or "\r" in value:
                delimiter = "__EOF_VS_ENV__"
                env_file.write(f"{name}<<{delimiter}\n{value}\n{delimiter}\n")
            else:
                env_file.write(f"{name}={value}\n")

    print(f"Successfully exported Visual Studio ({vs_arch}) environment to GITHUB_ENV.")


def print_tool_info():
    """Print information about installed compiler and tools."""
    print("--- Tool Information ---")
    try:
        run(["cmake", "--version"])
    except Exception as e:
        print(f"cmake not found: {e}")
    try:
        run(["cl.exe"], check=False)
    except Exception as e:
        print(f"cl.exe not found: {e}")


def main():
    parser = argparse.ArgumentParser(description="Windows CI helper")
    parser.add_argument("step", choices=["import_vs_env", "print_tool_info"],
                        help="Action to perform")
    parser.add_argument("arch", nargs="?", default="x64",
                        help="Target architecture (x64 or x86)")
    args = parser.parse_args()

    if args.step == "import_vs_env":
        import_vs_env(args.arch)
    elif args.step == "print_tool_info":
        print_tool_info()


if __name__ == "__main__":
    main()
