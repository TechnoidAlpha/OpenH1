# msdfgen (translated to C)

Multi-channel signed distance field generator by Viktor Chlumsky, from his
thesis "Shape Decomposition for Multi-channel Distance Fields" (Czech
Technical University in Prague, 2015).

- Upstream: https://github.com/Chlumsky/msdfgen, at commit
  1c106ed8117893bf943e577f62eb0665fb271e46.
- Licence: MIT. `LICENSE.txt` is upstream's, unchanged (SHA-256
  `32a94b25ad7072a78457d35019b32d73d2c265c806b0b76e9638fe800330bbe4`).
- What is here: `msdf_gen.h`, which is not a copy of upstream's files. It is
  a C translation of these parts of msdfgen's C++, with their logic kept:
  - `core/equation-solver.cpp`: `solveQuadratic`, `solveCubicNormed`,
    `solveCubic`;
  - `core/edge-segments.cpp`: the linear and quadratic segments'
    `signedDistance` and `distanceToPseudoDistance`;
  - `core/edge-coloring.cpp`: `edgeColoringSimple`, `switchColor`,
    `isCorner`, `symmetricalTrichotomy` and the splitting into thirds;
  - the legacy error correction's `detectClash`.

  The rest is OpenH1's: the shape built from stb_truetype's outline, the
  cubic curves as quadratic ones, the sign of each texel from the outline's
  non-zero winding, the alpha channel's true distance, and the 8-bit
  output. Since it is not a verbatim copy, there is no upstream file whose
  checksum it matches; the commit above is the source it was translated
  from.
- What uses it: `port/linux/src/text_hires.c` (the high-res text's
  glyphs made at run time, in every platform's game) and
  `tools/msdf_atlas/bake.c` (the baked atlas,
  `port/assets/fonts/msdf_atlas.zlib`). The releases carry the licence as
  `msdfgen-LICENSE.txt` (`tools/linux_package.py`, `tools/frame_package.py`).
