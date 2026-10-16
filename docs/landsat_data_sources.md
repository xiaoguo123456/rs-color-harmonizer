# Landsat 8/9 data sources and small-scene plan

## Authoritative sources

* **USGS Landsat Collection 2 Level-2:**
  <https://www.usgs.gov/landsat-missions/landsat-collection-2-level-2-science-products>
  The optical surface-reflectance assets are COG GeoTIFFs; USGS states that
  there are no restrictions on use. Cite the USGS Landsat Collection 2 Level-2
  dataset (DOI `10.5066/P9OGBGM6` for Landsat 8--9).
* **USGS Landsat STAC API:**
  <https://landsatlook.usgs.gov/stac-server/>. The query endpoint supports
  spatial/time filtering. The regression script queries
  `collections/landsat-c2l2-sr/items/{id}` and validates the response before
  writing it. STAC metadata is small and safe for CI; it never downloads a
  scene automatically.
* **AWS Open Data mirror:**
  <https://registry.opendata.aws/usgs-landsat/>. Collection 2 is in
  `s3://usgs-landsat/collection02/` in `us-west-2`. The bucket is
  **requester-pays**: a signed AWS request and the caller's AWS account are
  required, and the caller pays transfer/request charges. Do not put AWS
  credentials in this repository or CI logs. Direct COG links are supplied in
  each STAC Feature's assets.
* **Earth Search STAC index:**
  <https://earth-search.aws.element84.com/v1/>. The `landsat-c2-l2` collection
  is convenient for discovery and points at the authoritative USGS assets. It
  is an index, not a license bypass; the USGS requester-pays rules still apply.
* **Google Earth Engine (optional):** public catalog IDs are
  `LANDSAT/LC08/C02/T1_L2` and `LANDSAT/LC09/C02/T1_L2`. EE access/export
  requires an authenticated project and quota, so it is intentionally not a
  CI dependency. Export only a small AOI/window (for example 256 x 256 pixels)
  when a real-scene regression is needed.

## Recommended three-scene metadata sample

These records are near the San Francisco Bay AOI (EPSG:32610), same row and
adjacent WRS path where applicable, and use the same Collection 2 Level-2
surface-reflectance product:

| id | platform | WRS path/row | acquisition | cloud cover (land) |
|---|---|---|---|---:|
| `LC08_L2SP_045034_20240624_20240708_02_T1_SR` | Landsat 8 | 045/034 | 2024-06-24 | 7.78% |
| `LC09_L2SP_045034_20240616_20240617_02_T1_SR` | Landsat 9 | 045/034 | 2024-06-16 | 3.33% |
| `LC09_L2SP_044034_20240625_20240626_02_T1_SR` | Landsat 9 | 044/034 | 2024-06-25 | 21.39% |

The first two records have the same footprint and are useful for a temporal
normalization check; the third tests an adjacent-path overlap. Cloud masks and
changed land cover must be excluded from color statistics. The synthetic
fixture uses the same four optical bands and intentionally makes the scene
footprints smaller so it is fast and deterministic.

## Real-data replay (manual, bounded)

1. Use the STAC query to inspect item geometry, `landsat:cloud_cover_land`,
   `proj:transform`, and asset links.
2. Choose a 256 x 256 or smaller window common to at least two scenes. With a
   configured, signed requester-pays AWS client, issue HTTP range/COG reads for
   only B2, B3, B4, B5 and `QA_PIXEL`; do not download full 7,000-pixel scenes.
3. Apply Collection 2 Level-2 scale/offset before statistics where required by
   the product guide (`reflectance = DN * 0.0000275 - 0.2` for SR optical
   bands), then mask fill, cloud, cloud shadow, cirrus, snow and dilated cloud
   QA bits.
4. Record item IDs, asset hrefs, AOI/window, QA expression, scale/offset,
   checksums, and software version beside the output. Compare overlap MAE,
   RMSE, mean/standard-deviation ratios and seam jumps against the synthetic
   thresholds first; tighten thresholds after establishing a real-scene
   baseline.

No real COGs are bundled because USGS's binary assets redirect to login and the
AWS mirror is requester-pays. This keeps the test legal, reproducible offline,
and free of accidental large downloads while preserving an explicit path for a
credentialed real-scene replay.
