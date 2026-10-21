# The version, in one place: setup.py and CMake (for the native --version) read
# it. BASE is the active version: the release main is working towards. A
# release is the tag `v` + BASE (CI fails a tag that disagrees); after it,
# raise BASE to the next version. CI fills SUFFIX on branch builds with a
# PEP 440 dev + local segment (.dev<run>+gh.g<sha>), which PyPI rejects, so a
# CI wheel can never be released.
BASE = "0.1.0"
SUFFIX = ""
version = BASE + SUFFIX
