# MVP implementation notes

The executable path is intentionally usable on a clean C++20 machine. CMake probes GDAL, OpenCV and Qt 6, but none is required for the core library or CLI. This is useful for smoke tests and headless production runners; a deployment with GDAL can later replace `readGeoTiff` internals with true windowed/COG access without changing the public API.

## Processing flow

1. Read each scene's dimensions, sample type, coordinate transform, projection, NoData and strip data.
2. Traverse the raster in configurable blocks (default 256×256) and compute Welford mean/variance, ignoring NaN and optional NoData values.
3. Choose the reference scene by minimum sum of per-band mean distances, or accept an explicit index.
4. Apply per-band affine color transfer. `strength=0` is a no-op and `strength=1` matches reference mean and standard deviation.
5. Export an uncompressed GeoTIFF with coordinate tags and provenance software text.

The current fallback deliberately rejects compression and planar-separated TIFFs with a clear error. This avoids silently decoding a file incorrectly. GDAL windowed I/O, cloud masks, overlap-only statistics, seamlines and COG tiling remain production-phase extensions.
