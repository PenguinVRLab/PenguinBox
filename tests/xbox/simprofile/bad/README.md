# Bad fixtures: one change each (work/0039)

Every file here is a copy of ONE file in `../good/` with ONE change, and must be refused with ONE error code. To use a
row of `MANIFEST.tsv`: load the good set with `file` IN PLACE OF `replaces`, validate, and expect an ERROR with `code`
(other errors may follow from it; the code must be among them). `good_sha256` is the good file the row was derived
from: when it no longer matches, the row is stale and is made again from the new good file.

The same rows are for the emulator's reader (xemu_vr's B-SHIP-1 XB-5 vendors `../good/` and this folder with their
hashes), so the app and the emulator refuse the same documents for the same reasons. `34_parse_error.yaml` is the raw
cut of `bindings_wheel.yaml` mid-entry: a reader refuses it as a parse error, never half-loads it.
