# Architecture

## Layers

1. UI layer: Qt Quick/QML. Handles ribbon, dock panels, layer tree, map canvas, comparison views and parameter forms.
2. Application layer: C++ services for project files, task scheduling, command history, progress and cancellation.
3. Processing layer: C++ algorithms for radiometric normalization, overlap statistics, color transfer, seamline and feathering.
4. Data layer: GDAL for GeoTIFF/COG/JP2, CRS, reprojection, resampling, NoData and block I/O.
5. Parallel layer: TBB/OpenMP first; CUDA only for measured hotspots.

## Constraints

- Block/window processing only. Never load a full large scene by default.
- Preserve CRS, geotransform, NoData, band descriptions and source provenance.
- Every output records input checksums, parameters, software version and processing time.
- Treat cloud, shadow, snow and changed land cover as masks, not color statistics.
