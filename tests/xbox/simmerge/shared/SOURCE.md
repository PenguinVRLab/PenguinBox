# The shared merge vectors, vendored

SPDX-FileCopyrightText: 2026 Patrick Carey
SPDX-License-Identifier: MIT

`vectors.yaml` is the Sim Configurator's `tests/fixtures/merge/vectors.yaml` (MIT, the same author), copied unchanged
from its branch `dm4800/feat/same-update-binding-order-20261115` at `a0bd2ae` (2026-11-15). That commit is on the app's
main since `8de80c3`, where the file has these same bytes (checked 2026-11-15): sha256
`effa21d49e5749ce694cef99ddbe25ecef5c231a0361e96396eff8e8a27d002c`, 117 vectors (the S-74 keys on an axis, the S-75
descriptor invert and shared stay-put position, and the owner's "binding order" for keys first seen pressed in the same
update: `b2a-last-pressed-wins-same-step-binding-order`). They are built from this core's XB-6
rules (the app's decisions O-07, O-15, O-16, O-23, O-47) and both sides test against them: the app's
`tests/test_merge_vectors.py`, and part F of `../simmerge-test.cc` here. `make check` refuses a file whose sha256 is
not `vectors.sha256`. Refresh from the app's main in one commit, with this note and the sha; never edit it here.
