# rapidyaml 0.12.1 (vendored)

The YAML parser the vr-mods file reader uses (`hw/xbox/nv2a/pgraph/vr_mods_files.cc`): the same library and version
the PS2 core (pcsx2-VR) reads the same files with (its `build-dependencies-qt.sh` pins `RAPIDYAML=0.12.1`), so both
emulators accept and refuse the same files.

- `rapidyaml-0.12.1.hpp`: the release's single header,
  `https://github.com/biojppm/rapidyaml/releases/download/v0.12.1/rapidyaml-0.12.1.hpp`,
  sha256 `b75a78428f5a6f3abd18e3b7746ad1d246a059bf7d8dc19424e692a05086bd00`. Checked 2026-10-22: byte-identical
  to `tools/amalgamate.py` run on `rapidyaml-0.12.1-src.tgz` (sha256
  `e9efcdd17f86287748793cf21d106e461fcad8d103a3e5a23632afe93828660d`, the value pcsx2-VR pins). Not edited.
- `ryml.cc`: the one translation unit with `RYML_SINGLE_HDR_DEFINE_NOW` (the header's documented use).
- Licences: rapidyaml `LICENSE.txt` (MIT); c4core, amalgamated in, `c4core-LICENSE.txt` (MIT) and
  `c4core-LICENSE-BOOST.txt`; fast_float, amalgamated in, `fast_float-LICENSE-{MIT,APACHE,BOOST}` (any one of the three).
