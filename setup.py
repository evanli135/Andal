from setuptools import setup, Extension

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

fastevents_ext = Extension(
    name="fastevents",
    sources=sources,
    extra_compile_args=["-std=c11", "-O2", "-Wall"],
)

setup(
    name="fastevents",
    version="0.1.0",
    description="Embedded columnar event store",
    ext_modules=[fastevents_ext],
)
