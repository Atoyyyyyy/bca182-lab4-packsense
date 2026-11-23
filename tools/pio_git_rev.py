"""PlatformIO pre-script: embed the git revision in the firmware.

Defines PACKSENSE_GIT_REV, e.g. "620246d" or "620246d-dirty", so every boot log
names the exact source it was built from.
"""

import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def git_rev(project_dir):
    try:
        out = subprocess.check_output(
            ["git", "describe", "--always", "--dirty", "--abbrev=7"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        )
        return out.decode("ascii", "replace").strip() or "unknown"
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


rev = git_rev(env.subst("$PROJECT_DIR"))  # noqa: F821
env.Append(CPPDEFINES=[("PACKSENSE_GIT_REV", '\\"%s\\"' % rev)])  # noqa: F821
print("PackSense git revision: %s" % rev)
