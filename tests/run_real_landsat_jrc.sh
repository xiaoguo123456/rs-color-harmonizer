#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-/tmp/rsch-jrc-landsat}"
mkdir -p "$WORK"
URL='https://ies-ows.jrc.ec.europa.eu/iforce/impact/download.py?dataset=Landsat_test_data_1990_2000_2014_Brazil.zip'
if [[ ! -f "$WORK/brazil.zip" ]]; then
  curl -L --fail --max-time 900 -o "$WORK/brazil.zip" "$URL"
fi
unzip -o "$WORK/brazil.zip" 'Landsat_test_data/*_calrefbyt.tif' -d "$WORK/extracted" >/dev/null
mkdir -p "$WORK/rgb" "$WORK/harmonized"
for f in "$WORK/extracted/Landsat_test_data"/*_calrefbyt.tif; do
  name="$(basename "$f" _calrefbyt.tif)"
  gdal_translate -q -of GTiff -b 3 -b 2 -b 1 -co COMPRESS=NONE -co TILED=NO "$f" "$WORK/rgb/${name}_rgb.tif"
done
cmake -S "$ROOT" -B "$WORK/build" -DRSCH_BUILD_QT=OFF -DRSCH_USE_GDAL=OFF -DRSCH_USE_OPENCV=OFF
cmake --build "$WORK/build" --parallel
"$WORK/build/rsch" harmonize "$WORK/harmonized" "$WORK/rgb"/*_rgb.tif --reference 2 --strength 1
printf '真实 Landsat 三景处理完成，输出目录：%s\n' "$WORK/harmonized"
