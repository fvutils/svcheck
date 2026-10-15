# The version, in one place: setup.py and CMake (for the native --version) read
# it. BASE is the release number and the tag must match it (`v` + BASE); CI
# fills SUFFIX on branch builds with a PEP 440 dev + local segment
# (.dev<run>+gh.g<sha>), which PyPI rejects, so a CI wheel can never be
# released.
BASE = "0.0.0"
SUFFIX = ""
version = BASE + SUFFIX
