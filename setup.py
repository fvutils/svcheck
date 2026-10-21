#*****************************************************************************
#* setup.py
#*
#* svcheck: builds libsvcheck and the executable with CMake (through
#* ivpm-build's CmakeBuilder), then the two extensions on the library
#* (svcheck-plan P2.6b): svcheck.sem, the model's module, which CMake's
#* configure generates (scripts/gen_sem_model.py, gen-pyext), and
#* svcheck.core. The package holds svcheck/bin/svcheck and libsvcheck.
#*
#* The extensions deliberately link nothing of svcheck's: both reach
#* libsvcheck at runtime through a ctypes-loaded *_getFactory entry point
#* returning a pure-virtual interface (core.pyx, and the generated
#* sem.pyx), so they carry no undefined symbols from it (pssparser's way).
#*****************************************************************************
import os
import shutil
import sys

from setuptools import Extension, setup, find_packages
from setuptools.command.build_ext import build_ext as _build_ext
from setuptools.command.build_py import build_py as _build_py

proj_dir = os.path.dirname(os.path.abspath(__file__))
exe = "svcheck.exe" if sys.platform == "win32" else "svcheck"
if sys.platform == "win32":
    lib, lib_dir = "svcheck.dll", "bin"
elif sys.platform == "darwin":
    lib, lib_dir = "libsvcheck.dylib", "lib"
else:
    lib, lib_dir = "libsvcheck.so", "lib"
build_dir = os.path.join(proj_dir, "build")
pkg_dir = os.path.join(proj_dir, "python", "svcheck")
# gen-pyext's output (build/src/ast/gen/pyext), copied into the package.
pyext_dir = os.path.join(build_dir, "src", "ast", "gen", "pyext")
PYEXT = ["sem.pyx", "sem.pxd", "sem_decl.pxd", "sem.pyi", "PyBaseVisitor.cpp",
         "PyBaseVisitor.h"]

version = {}
with open(os.path.join(proj_dir, "python", "svcheck", "__version__.py")) as f:
    exec(f.read(), version)


skills = {}
with open(os.path.join(proj_dir, "python", "svcheck", "skills.py")) as f:
    exec(f.read(), skills)


_built = False


def build_native():
    """CMake (configure, build, install into build/), once per setup run;
    then the executable, the library and the model's module into the
    package."""
    global _built
    if _built:
        return
    # SVCHECK_PREBUILT=1: build/ holds the native build already (one
    # CMake build serves every CPython's wheel, scripts/ci/build-wheel.sh).
    if os.environ.get("SVCHECK_PREBUILT", "") not in ("1", "y", "Y"):
        from ivpm_build.cmake.cmake_builder import CmakeBuilder

        debug = os.environ.get("DEBUG", "") in ("1", "y", "Y")
        CmakeBuilder(proj_dir, debug=debug).run()

    os.makedirs(os.path.join(pkg_dir, "bin"), exist_ok=True)
    shutil.copy2(os.path.join(build_dir, "bin", exe), os.path.join(pkg_dir, "bin", exe))
    shutil.copy2(os.path.join(build_dir, lib_dir, lib), os.path.join(pkg_dir, lib))
    for f in PYEXT:
        shutil.copy2(os.path.join(pyext_dir, f), os.path.join(pkg_dir, f))
    _built = True


class BuildPy(_build_py):
    """Build the native code first: the package's files come from it."""

    def run(self):
        build_native()
        super().run()

        # The agent skills (svcheck/skills.py), into the built package only:
        # a checkout and an editable install read <repo>/skills directly.
        for name in skills["SHIPPED"]:
            dst = os.path.join(self.build_lib, "svcheck", "share", "skills", name)
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(os.path.join(proj_dir, "skills", name), dst)


class BuildExt(_build_ext):
    """The extensions compile against the generated model's headers and
    sem.pyx, which CMake makes: build it first (an editable install runs
    build_ext without build_py)."""

    def run(self):
        build_native()
        # A module's C++ is regenerated when its .pyx changes, and when a .pxd
        # it cimports changes only if Cython's scan finds the cimport, which
        # it does not for core.pyx's `from svcheck cimport sem`. sem.pxd is
        # CMake's, so a model change would leave core built against the old
        # layout: drop generated C++ older than any of the module's depends.
        for e in self.extensions:
            newest = max(os.path.getmtime(d) for d in e.depends)
            for s in e.sources:
                cpp = os.path.splitext(s)[0] + ".cpp"
                if s.endswith(".pyx") and os.path.isfile(cpp) \
                        and os.path.getmtime(cpp) < newest:
                    os.remove(cpp)
        super().run()


def ext(name, sources, depends):
    return Extension(
        "svcheck.%s" % name,
        [os.path.join("python", "svcheck", s) for s in sources],
        depends=[os.path.join("python", "svcheck", d) for d in depends],
        include_dirs=[
            os.path.join(proj_dir, "python"),
            pkg_dir,
            os.path.join(build_dir, "src", "ast", "gen", "include"),
            os.path.join(proj_dir, "src", "api", "include"),
        ],
        language="c++",
        extra_compile_args=[] if sys.platform == "win32" else ["-std=c++20"],
    )


setup(
    name="svcheck",
    version=version["version"],
    author="Matthew Ballance",
    author_email="matt.ballance@gmail.com",
    license="Apache-2.0",
    description="A SystemVerilog checker whose rules are data",
    long_description=open(os.path.join(proj_dir, "README.md"), encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    url="https://dvkit.org/fvutils/svcheck/",
    project_urls={
        "Documentation": "https://dvkit.org/fvutils/svcheck/",
        "Source": "https://github.com/fvutils/svcheck",
        "Changelog": "https://github.com/fvutils/svcheck/blob/main/CHANGELOG.md",
    },
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: C++",
        "Programming Language :: Python :: 3",
        "Topic :: Scientific/Engineering :: Electronic Design Automation (EDA)",
        "Topic :: Software Development :: Quality Assurance",
    ],
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    package_data={"svcheck": ["bin/*", lib, "*.pxd", "*.pyi"]},
    ext_modules=[
        ext("sem", ["sem.pyx", "PyBaseVisitor.cpp"],
            ["sem.pxd", "sem_decl.pxd", "PyBaseVisitor.h"]),
        ext("core", ["core.pyx"],
            ["core.pxd", "core_decl.pxd", "sem.pxd", "sem_decl.pxd"]),
    ],
    cmdclass={"build_py": BuildPy, "build_ext": BuildExt},
    entry_points={
        "console_scripts": [
            "svcheck = svcheck.native:main",
        ],
        # One entry point per skill: IVPM names a skill after its entry point.
        "agent.skills": [
            "svcheck-rules = svcheck.skills:rules_skill",
        ],
    },
    python_requires=">=3.10",
)
