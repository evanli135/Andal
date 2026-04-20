import sys
from setuptools import setup, Extension, find_packages

# MSVC doesn't accept -std=c11 or -Wall; use equivalent MSVC flags instead.
if sys.platform == "win32":
    compile_args = ["/std:c11", "/O2", "/W3",
                    "/D_CRT_SECURE_NO_WARNINGS",
                    "/D_CRT_NONSTDC_NO_WARNINGS"]
else:
    compile_args = ["-std=c11", "-O2", "-Wall"]

sources = [
    "src/fastevents_module.c",
    "src/coordinator.c",
    "src/disk.c",
    "src/events.c",
    "src/encoding.c",
    "src/partition.c",
    "src/stringdict.c",
    "src/wal.c",
    "src/query.c",
]

andal_ext = Extension(
    name="andal._andal",
    sources=sources,
    include_dirs=["src"],
    extra_compile_args=compile_args,
)

setup(
    name="andal",
    version="0.1.0",
    description="High-performance embedded event store for Python",
    license="MIT",
    packages=find_packages(exclude=["tests", "examples"]),
    ext_modules=[andal_ext],
    python_requires=">=3.8",
)
