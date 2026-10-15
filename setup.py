#*****************************************************************************
#* setup.py
#*
#* svcheck: builds the native executable with CMake (through ivpm-build's
#* CmakeBuilder) and ships it inside the package as svcheck/bin/svcheck.
#*****************************************************************************
import os
import shutil
import sys

from setuptools import setup, find_packages
from setuptools.command.build_py import build_py as _build_py
from setuptools.dist import Distribution
try:
    from setuptools.command.bdist_wheel import bdist_wheel as _bdist_wheel
except ImportError:  # setuptools < 70.1
    from wheel.bdist_wheel import bdist_wheel as _bdist_wheel

proj_dir = os.path.dirname(os.path.abspath(__file__))
exe = "svcheck.exe" if sys.platform == "win32" else "svcheck"

version = {}
with open(os.path.join(proj_dir, "python", "svcheck", "__version__.py")) as f:
    exec(f.read(), version)


class BuildPy(_build_py):
    """Build the native executable, then copy it into the package."""

    def run(self):
        from ivpm_build.cmake.cmake_builder import CmakeBuilder

        debug = os.environ.get("DEBUG", "") in ("1", "y", "Y")
        CmakeBuilder(proj_dir, debug=debug).run()

        dst_dir = os.path.join(proj_dir, "python", "svcheck", "bin")
        os.makedirs(dst_dir, exist_ok=True)
        shutil.copy2(os.path.join(proj_dir, "build", "bin", exe), os.path.join(dst_dir, exe))
        super().run()


class BdistWheel(_bdist_wheel):
    """Platform-specific, but not tied to a Python version or ABI: the wheel
    holds an executable, not an extension module."""

    def get_tag(self):
        _, _, plat = super().get_tag()
        return "py3", "none", plat


class BinaryDistribution(Distribution):
    """The wheel carries a native executable, so it is platform-specific."""

    def has_ext_modules(self):
        return True


setup(
    name="svcheck",
    version=version["version"],
    author="Matthew Ballance",
    author_email="matt.ballance@gmail.com",
    license="Apache-2.0",
    description="A SystemVerilog checker whose rules are data",
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    package_data={"svcheck": ["bin/*"]},
    cmdclass={"build_py": BuildPy, "bdist_wheel": BdistWheel},
    distclass=BinaryDistribution,
    entry_points={
        "console_scripts": [
            "svcheck = svcheck.native:main",
        ],
    },
    python_requires=">=3.10",
)
