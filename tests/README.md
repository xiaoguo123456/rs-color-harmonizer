# Landsat multi-scene regression

`landsat_multiscene_regression.py` is the small, deterministic kernel fixture
for radiometric normalization. It does not commit or download Landsat raster
pixels. Three 192 x 192 scenes contain four Landsat-like surface-reflectance
bands (B2/B3/B4/B5), 30 m spacing, 128/64 pixel overlaps, cloud masks, a
scene-specific gain/offset, and a low-frequency illumination gradient. The
scene names and metadata are tied to real Collection 2 Level-2 Landsat 8/9
STAC records, so an implementation can replay the same checks with real data
when a requester-pays AWS credential is available.

## Reproduce the check

From the repository root:

```bash
python tests/landsat_multiscene_regression.py \
  --generate /tmp/landsat-fixture
python tests/landsat_multiscene_regression.py \
  --run /tmp/landsat-fixture
```

The run writes `regression_results.json` and exits non-zero if any threshold
fails. On the reference fixture (seed `20240624`) the expected envelope is:

- at least 10,000 valid pixels in every reference overlap;
- maximum post/pre overlap MAE ratio <= 0.35;
- maximum post-harmonization overlap RMSE <= 0.012 reflectance units.

The reference implementation fits a per-band affine transform on the 2--98th
percentile of valid overlap pixels. It excludes `cloud_mask`/invalid pixels,
clips output to [0, 1], and leaves georeferencing fields in the manifest. A
production C++/GDAL test can replace that fit with the application output and
reuse the same overlap metrics. Keep a fixed seed and fixture version so a
change in metrics is attributable to code rather than regenerated data.

## Optional official metadata

To fetch only three small STAC Features (no imagery):

```bash
python tests/landsat_multiscene_regression.py \
  --generate /tmp/landsat-fixture --stac-metadata
```

The JSON files are useful for checking platform, path/row, CRS, transform,
cloud cover, and COG asset links. The data-source and access caveats are in
`landsat_data_sources.md`.
