# Pins the PlatformIO Core inside pioarduino's penv (~/.platformio/penv) to 6.1.19.
#
# When it rebuilds the memory-tuned core ([mem_tuned] in platformio.ini), the
# pioarduino platform finishes the build with a second `pio run` taken from that
# penv. A new penv gets the newest `pioarduino` from PyPI, because the platform
# only asks for >=6.1.19. Version 6.2.x fails that build at the link with
# "No module named 'SCons.Tool.FortranCommon'" (CI, 2026-11-20). The platform
# keeps a penv that already satisfies >=6.1.19, so creating it first with 6.1.19
# is enough.
#
# Run it with the interpreter that runs `pio`, before `pio run`. The platform
# recreates a penv built with another Python minor version.
#
#   python3 scripts/pin_penv_core.py
import os
import subprocess
import sys
from pathlib import Path

CORE = "6.1.19"
PROBE = "import sys, platformio; print('%d.%d' % sys.version_info[:2], platformio.__version__)"


def penv_python(penv):
    return penv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def probe(python):
    # (python "major.minor", core version) of the penv, or None if it cannot answer.
    if not python.is_file():
        return None
    result = subprocess.run([str(python), "-c", PROBE], capture_output=True, text=True)
    if result.returncode != 0:
        return None
    return tuple(result.stdout.split())


def main():
    core_dir = Path(os.environ.get("PLATFORMIO_CORE_DIR") or Path.home() / ".platformio")
    penv = core_dir / "penv"
    python = penv_python(penv)
    want = ("%d.%d" % sys.version_info[:2], CORE)

    state = probe(python)
    if state == want:
        print(f"penv already pins PlatformIO Core {CORE} (Python {want[0]}): {penv}")
        return 0
    if state is None or state[0] != want[0]:
        subprocess.check_call([sys.executable, "-m", "venv", "--clear", str(penv)])
    subprocess.check_call([
        str(python), "-m", "pip", "install",
        "--disable-pip-version-check", "--quiet", f"pioarduino=={CORE}",
    ])

    state = probe(python)
    if state != want:
        print(f"error: penv reports {state}, expected {want}", file=sys.stderr)
        return 1
    print(f"penv pinned to PlatformIO Core {CORE} (Python {want[0]}): {penv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
