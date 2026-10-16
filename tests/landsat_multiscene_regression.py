#!/usr/bin/env python3
"""Small, deterministic Landsat-like multi-scene regression fixture.

The production application can use the ``.npz`` files written by this script as
an inexpensive kernel test fixture.  The fixture deliberately has three
overlapping scenes, Landsat-8/9-like bands (B2--B5), clouds, a radiometric
gain/offset change and a weak illumination gradient.  No Landsat pixels are
redistributed by the repository; real-scene metadata is queried separately
with ``--stac-metadata``.

Examples
--------
    python tests/landsat_multiscene_regression.py --generate /tmp/landsat-fixture
    python tests/landsat_multiscene_regression.py --run /tmp/landsat-fixture
    python tests/landsat_multiscene_regression.py --generate /tmp/landsat-fixture \
        --stac-metadata

``--run`` is a reference check, not the application's implementation.  It
fits a robust per-band affine transform from valid overlap pixels and verifies
that overlap/seam errors decrease.  A C++/GDAL implementation can consume the
same manifest and replace ``reference_harmonize`` while retaining the metric
thresholds.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import urllib.error
import urllib.request
from typing import Any

import numpy as np


STAC_API = "https://landsatlook.usgs.gov/stac-server"
STAC_COLLECTION = "landsat-c2l2-sr"
SCENE_IDS = [
    "LC08_L2SP_045034_20240624_20240708_02_T1_SR",
    "LC09_L2SP_045034_20240616_20240617_02_T1_SR",
    "LC09_L2SP_044034_20240625_20240626_02_T1_SR",
]

# The records make the synthetic fixture traceable to the type of real data it
# models while keeping CI independent of a large (and requester-pays) bucket.
REAL_SCENES = [
    {"id": SCENE_IDS[0], "platform": "LANDSAT_8", "path": "045", "row": "034"},
    {"id": SCENE_IDS[1], "platform": "LANDSAT_9", "path": "045", "row": "034"},
    {"id": SCENE_IDS[2], "platform": "LANDSAT_9", "path": "044", "row": "034"},
]


def _scene_arrays(seed: int = 20240624) -> list[dict[str, Any]]:
    """Build three deterministic scenes with a shared physical surface."""

    rng = np.random.default_rng(seed)
    channels, height, width = 4, 192, 192
    global_width = 320
    yy, xx = np.mgrid[0:height, 0:global_width]

    # Smooth surface reflectance plus texture. Values are reflectance-like
    # floats in [0, 1], not raw Landsat DN values.
    global_base = np.empty((channels, height, global_width), dtype=np.float32)
    definitions = ((0.06, 0.28, 3.0, 2.0), (0.08, 0.40, 2.0, 4.0),
                   (0.05, 0.32, 4.0, 3.0), (0.10, 0.55, 2.0, 2.0))
    for band, (low, high, fx, fy) in enumerate(definitions):
        texture = rng.normal(0.0, 0.012, (height, global_width))
        smooth = (np.sin(xx / global_width * fx * 2 * np.pi)
                  + np.cos(yy / height * fy * 2 * np.pi)) * 0.02
        trend = (xx / global_width) * 0.35 + (yy / height) * 0.25
        global_base[band] = np.clip(low + (high - low) * (0.40 + trend)
                                    + smooth + texture, 0.01, 0.80)

    # Scene origins overlap by 128 pixels. The second and third scenes have
    # deliberately different gains/offsets, and all scenes have a slight
    # scene-specific illumination gradient.
    origins = (0, 64, 128)
    gains = np.asarray(((1.00, 1.14, 0.93), (1.00, 0.92, 1.11),
                        (1.00, 1.08, 0.95), (1.00, 0.88, 1.07)), dtype=np.float32)
    offsets = np.asarray(((0.000, 0.012, -0.008), (0.000, -0.006, 0.014),
                          (0.000, 0.009, -0.004), (0.000, 0.004, 0.009)), dtype=np.float32)

    records: list[dict[str, Any]] = []
    for index, origin in enumerate(origins):
        data = (global_base[:, :, origin:origin + width]
                * gains[:, index, None, None] + offsets[:, index, None, None])
        gx = (np.arange(width, dtype=np.float32)[None, :] - width / 2) / (width / 2)
        gy = (np.arange(height, dtype=np.float32)[:, None] - height / 2) / (height / 2)
        illumination = 1.0 + (0.04 * index) * gx + (0.02 * index) * gy
        data = np.clip(data * illumination[None, :, :], 0.0, 1.0).astype(np.float32)

        # Two opaque cloud/shadow ellipses per scene. QA masks must be excluded
        # from overlap statistics, as in Landsat QA_PIXEL workflows.
        cloud = (((np.mgrid[0:height, 0:width][1] - (120 + 10 * index)) ** 2 / 20**2
                  + (np.mgrid[0:height, 0:width][0] - (70 + 10 * index)) ** 2 / 15**2) < 1)
        cloud |= (((np.mgrid[0:height, 0:width][1] - (35 + 20 * index)) ** 2 / 10**2
                   + (np.mgrid[0:height, 0:width][0] - (150 - 8 * index)) ** 2 / 20**2) < 1)
        records.append({
            "bands": data,
            "cloud_mask": cloud.astype(bool),
            "valid_mask": (~cloud).astype(bool),
            "origin_x": origin,
            "pixel_size": 30,
            "epsg": 32610,
            "width": width,
            "height": height,
            "channels": channels,
        })
    return records


def _save_record(path: pathlib.Path, record: dict[str, Any]) -> None:
    np.savez_compressed(path, bands=record["bands"],
                        cloud_mask=record["cloud_mask"],
                        valid_mask=record["valid_mask"],
                        origin_x=np.int32(record["origin_x"]))


def generate_fixture(out_dir: pathlib.Path) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    records = _scene_arrays()
    manifest: dict[str, Any] = {
        "format": "rs-color-harmonizer.landsat-regression.v1",
        "description": "Synthetic 3-scene Landsat 8/9-like overlap fixture",
        "seed": 20240624,
        "bands": ["B2", "B3", "B4", "B5"],
        "dtype": "float32 reflectance-like values",
        "crs": "EPSG:32610",
        "pixel_size_m": 30,
        "scene_width": 192,
        "scene_height": 192,
        "scene_files": [],
        "real_scene_references": REAL_SCENES,
        "thresholds": {
            "max_post_to_pre_mae_ratio": 0.35,
            "max_post_overlap_rmse": 0.012,
            "min_overlap_valid_pixels": 10000,
        },
    }
    for index, (record, source) in enumerate(zip(records, REAL_SCENES)):
        filename = f"scene_{index + 1}_{source['id']}.npz"
        _save_record(out_dir / filename, record)
        manifest["scene_files"].append({
            "file": filename,
            "id": source["id"],
            "origin_x": int(record["origin_x"]),
            "cloud_fraction": float(record["cloud_mask"].mean()),
        })
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def _load_fixture(out_dir: pathlib.Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    manifest = json.loads((out_dir / "manifest.json").read_text())
    records: list[dict[str, Any]] = []
    for item in manifest["scene_files"]:
        with np.load(out_dir / item["file"]) as archive:
            records.append({
                "bands": archive["bands"].astype(np.float32),
                "cloud_mask": archive["cloud_mask"].astype(bool),
                "valid_mask": archive["valid_mask"].astype(bool),
                "origin_x": int(archive["origin_x"]),
            })
    return manifest, records


def _overlap(ref: dict[str, Any], src: dict[str, Any]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return reference data, source data and valid mask for the overlap."""
    ref_x, src_x = int(ref["origin_x"]), int(src["origin_x"])
    left, right = max(ref_x, src_x), min(ref_x + ref["bands"].shape[2],
                                         src_x + src["bands"].shape[2])
    if right <= left:
        raise ValueError("scenes have no spatial overlap")
    rw, sw, width = left - ref_x, left - src_x, right - left
    ref_data = ref["bands"][:, :, rw:rw + width]
    src_data = src["bands"][:, :, sw:sw + width]
    mask = (ref["valid_mask"][:, rw:rw + width]
            & src["valid_mask"][:, sw:sw + width])
    return ref_data, src_data, mask


def _fit_affine(source: np.ndarray, target: np.ndarray, mask: np.ndarray) -> tuple[float, float, int]:
    x, y = source[mask].astype(np.float64), target[mask].astype(np.float64)
    if x.size < 100:
        raise ValueError(f"too few valid overlap pixels ({x.size})")
    # Trim extreme values to keep clouds/residual shadows from driving the fit.
    q_low, q_high = np.quantile(x, (0.02, 0.98))
    keep = (x >= q_low) & (x <= q_high)
    x, y = x[keep], y[keep]
    x_mean, y_mean = x.mean(), y.mean()
    denominator = float(np.sum((x - x_mean) ** 2))
    slope = 1.0 if denominator <= np.finfo(float).eps else float(
        np.sum((x - x_mean) * (y - y_mean)) / denominator)
    return slope, float(y_mean - slope * x_mean), int(x.size)


def reference_harmonize(records: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Calibrate scenes 2..N to scene 1; returns outputs and fit metadata."""
    ref = records[0]
    outputs: list[dict[str, Any]] = [dict(ref, bands=ref["bands"].copy())]
    fits: list[dict[str, Any]] = []
    for index, source in enumerate(records[1:], start=1):
        ref_overlap, src_overlap, mask = _overlap(ref, source)
        output = dict(source, bands=source["bands"].copy())
        scene_fit: list[dict[str, Any]] = []
        for band in range(source["bands"].shape[0]):
            slope, intercept, n = _fit_affine(src_overlap[band], ref_overlap[band], mask)
            output["bands"][band] = np.clip(slope * output["bands"][band] + intercept,
                                               0.0, 1.0).astype(np.float32)
            scene_fit.append({"band": band, "slope": slope, "intercept": intercept,
                              "fit_pixels": n})
        outputs.append(output)
        fits.append({"scene_index": index, "fits": scene_fit})
    return outputs, fits


def _pair_metrics(ref: dict[str, Any], src: dict[str, Any]) -> dict[str, Any]:
    ref_data, src_data, mask = _overlap(ref, src)
    pre_mae, post_mae, pre_rmse, post_rmse = [], [], [], []
    # ``src`` is the already harmonized output for post metrics; the caller
    # passes raw source separately under ``src['_raw_bands']``.
    raw = src.get("_raw_bands", src["bands"])
    if raw is not src["bands"]:
        raw_record = dict(src, bands=raw)
        _, raw, _ = _overlap(ref, raw_record)
    for band in range(ref_data.shape[0]):
        diff_pre = raw[band][mask] - ref_data[band][mask]
        diff_post = src_data[band][mask] - ref_data[band][mask]
        pre_mae.append(float(np.mean(np.abs(diff_pre))))
        post_mae.append(float(np.mean(np.abs(diff_post))))
        pre_rmse.append(float(np.sqrt(np.mean(diff_pre ** 2))))
        post_rmse.append(float(np.sqrt(np.mean(diff_post ** 2))))
    return {
        "valid_pixels": int(mask.sum()),
        "pre_mae": pre_mae,
        "post_mae": post_mae,
        "pre_rmse": pre_rmse,
        "post_rmse": post_rmse,
        "post_to_pre_mae_ratio": [post / max(pre, 1e-12)
                                   for pre, post in zip(pre_mae, post_mae)],
    }


def run_regression(out_dir: pathlib.Path) -> dict[str, Any]:
    manifest, raw_records = _load_fixture(out_dir)
    harmonized, fits = reference_harmonize(raw_records)
    results: dict[str, Any] = {"fits": fits, "pairs": []}
    for index in range(1, len(raw_records)):
        # Keep raw data only for metric calculation, never write it to output.
        post = dict(harmonized[index], _raw_bands=raw_records[index]["bands"])
        results["pairs"].append(_pair_metrics(harmonized[0], post))

    thresholds = manifest["thresholds"]
    valid_min = min(pair["valid_pixels"] for pair in results["pairs"])
    max_ratio = max(max(pair["post_to_pre_mae_ratio"]) for pair in results["pairs"])
    max_rmse = max(max(pair["post_rmse"]) for pair in results["pairs"])
    results["summary"] = {"min_overlap_valid_pixels": valid_min,
                           "max_post_to_pre_mae_ratio": max_ratio,
                           "max_post_overlap_rmse": max_rmse}
    failures = []
    if valid_min < thresholds["min_overlap_valid_pixels"]:
        failures.append(f"valid overlap pixels {valid_min} < {thresholds['min_overlap_valid_pixels']}")
    if max_ratio > thresholds["max_post_to_pre_mae_ratio"]:
        failures.append(f"post/pre MAE ratio {max_ratio:.3f} > {thresholds['max_post_to_pre_mae_ratio']}")
    if max_rmse > thresholds["max_post_overlap_rmse"]:
        failures.append(f"post overlap RMSE {max_rmse:.4f} > {thresholds['max_post_overlap_rmse']}")
    results["status"] = "PASS" if not failures else "FAIL"
    results["failures"] = failures
    (out_dir / "regression_results.json").write_text(json.dumps(results, indent=2) + "\n")
    if failures:
        raise AssertionError("; ".join(failures))
    return results


def fetch_stac_metadata(out_dir: pathlib.Path) -> list[pathlib.Path]:
    """Fetch only small STAC JSON records; never downloads imagery."""
    out_dir.mkdir(parents=True, exist_ok=True)
    written: list[pathlib.Path] = []
    for scene_id in SCENE_IDS:
        url = f"{STAC_API}/collections/{STAC_COLLECTION}/items/{scene_id}"
        destination = out_dir / f"{scene_id}.stac.json"
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                payload = response.read()
            # Validate before writing so an HTML login page is never mistaken
            # for metadata.
            item = json.loads(payload)
            if item.get("type") != "Feature" or item.get("id") != scene_id:
                raise ValueError("response is not the expected STAC Feature")
            destination.write_text(json.dumps(item, indent=2) + "\n")
            written.append(destination)
        except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            print(f"STAC metadata unavailable for {scene_id}: {exc}")
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", type=pathlib.Path, metavar="DIR",
                        help="write deterministic NPZ scenes and manifest")
    parser.add_argument("--run", type=pathlib.Path, metavar="DIR",
                        help="run metric regression against a generated fixture")
    parser.add_argument("--stac-metadata", action="store_true",
                        help="fetch three small official USGS STAC JSON records")
    args = parser.parse_args()
    if not args.generate and not args.run and not args.stac_metadata:
        parser.error("choose --generate, --run, or --stac-metadata")
    if args.generate:
        generate_fixture(args.generate)
        print(f"generated fixture: {args.generate}")
    if args.stac_metadata:
        metadata_dir = args.generate or pathlib.Path("tests/data/stac")
        files = fetch_stac_metadata(metadata_dir)
        print(f"fetched {len(files)} STAC metadata records (no imagery)")
    if args.run:
        results = run_regression(args.run)
        print(json.dumps(results["summary"], indent=2))
        print("regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
