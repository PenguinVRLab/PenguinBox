# The shared merge vectors, vendored

SPDX-FileCopyrightText: 2026 Patrick Carey
SPDX-License-Identifier: MIT

`vectors.yaml` is the Sim Configurator's `tests/fixtures/merge/vectors.yaml` (MIT, the same author). Copied unchanged
from the sim-configurator repo (MIT, the same author) at its `main` `ee9e736288b733250867c79c1603052d2efb8a8c`
(2026-11-15; it carries the branch `dm4800/feat/contract-levers-hats-20261115`, first vendored here from its tip
`8515ae4`). At that commit `tests/fixtures/merge/vectors.yaml` has sha256
`03e6a916445ee6f0ab517e36eb30ae5a42123d8eae24b50fb51f559cd4d1c02e` and `tests/fixtures/bad/MANIFEST.tsv` sha256
`84b97f09edc092e12c4a68df31cdaca667309207e7a7b62c83457afef57512ab`. 130 vectors: the 117 of the app's earlier main
(`a0bd2ae`, sha256 `effa21d49e5749ce694cef99ddbe25ecef5c231a0361e96396eff8e8a27d002c`) byte for byte, plus 13 for the
app's S-80 and S-81 (a lever on a button, a lever on a lever with the same positions, a hat on a button). They are built
from this core's XB-6 rules and the app's decisions, and both sides test against them: the app's
`tests/test_merge_vectors.py`, and part F of `../simmerge-test.cc` here. `make check` refuses a file whose sha256 is not
`vectors.sha256`. Refresh from the app in one commit, with this note and the sha; never edit it here.
