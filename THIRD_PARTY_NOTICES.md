# Third-party notices

The dependency-free MVP uses only the C++20 standard library.

Optional integrations are discovered by CMake:

* GDAL (MIT/X11-style GDAL license) for future production windowed I/O and extended GeoTIFF/COG support.
* OpenCV (Apache-2.0) for future accelerated image kernels.
* Qt 6 (LGPLv3/commercial, depending on the distribution) for the optional Qt Quick shell.

When distributing a binary with an optional dependency enabled, include that dependency's license and notices as required by its distribution terms.
