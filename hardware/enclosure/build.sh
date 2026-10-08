#!/bin/bash
# Vyrenderuje vsechny dily krabicky do STL (potreba OpenSCAD 2021+).
#   ./build.sh            -> stl/*.stl
# Parametry (rozmery displeje, baterie...) uprav na zacatku cyklocomp_case.scad.
set -e
cd "$(dirname "$0")"
mkdir -p stl
for p in base lid tray cleat garmin_test; do
  echo "== $p"
  openscad -o "stl/$p.stl" -D "part=\"$p\"" cyklocomp_case.scad
done
