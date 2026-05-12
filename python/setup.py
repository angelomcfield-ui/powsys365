from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import sys
import os

class get_pybind_include(object):
    def __init__(self, user=False):
        self.user = user

    def __str__(self):
        import pybind11
        return pybind11.get_include(self.user)

ext_modules = [
    Extension(
        'powsy365._core',
        ['bindings.cpp'],
        include_dirs=[
            get_pybind_include(),
            get_pybind_include(user=True),
            '../core/include',
            '../core/commons',
        ],
        language='c++',
        extra_compile_args=['-std=c++20'],
    ),
]

setup(
    name='powsys365',
    version='0.1.0',
    author='POWSYS365 Team',
    description='Power System Analysis Platform',
    packages=['powsy365'],
    ext_modules=ext_modules,
    install_requires=[
        'numpy>=2.3',
        'scipy>=1.15',
        'pandas>=2.0',
        'pandapower>=3.0',
        'pybind11>=2.10',
    ],
    cmdclass={'build_ext': build_ext},
    zip_safe=False,
)