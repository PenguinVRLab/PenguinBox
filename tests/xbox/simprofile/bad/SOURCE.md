# The Sim Configurator's bad fixtures, vendored (its work/0039)

SPDX-FileCopyrightText: 2026 Patrick Carey
SPDX-License-Identifier: MIT

Copied unchanged from the sim-configurator repo (MIT, the same author) at its `main` `80d47c2072fe25498f6806c3cdf389d4bd590329` (2026-11-15), `tests/fixtures/bad/`: 46 rows (35-37 the
response-range refusals, 38-46 the S-74 rules).
Each row of `MANIFEST.tsv`: load `../good/` with `file` IN PLACE OF `replaces`; the reader must refuse with `code`
(test part G). `good_sha256` is the good file the row was made from: `make check` fails when it no longer matches the
vendored good file (a stale row). Refresh with good/ in one commit; never edit them here.

| File | sha256 |
|---|---|
| `01_schema_unknown.yaml` | `cdaaa50d532387420b7c0a7d92052a42f496cc796136993947370bc9022dcdd8` |
| `02_kind_unknown.yaml` | `20bba3ae4209b293085a386e8638d474c8abdab56be1ba73b91cd4109b5b6dca` |
| `03_missing_id.yaml` | `a3123cdfe45c55540cce7960c825914daed524b7e764ce1ef42d1ecc03af65c7` |
| `04_source_unknown.yaml` | `d5ecf776d5532bd9460498da610cc711d69b67437a7c909a29b4b6de03ab6d37` |
| `05_inputs_required.yaml` | `c887ce0ca9be418e7512f9ed0e27d6eb0183a118b2f7421445fc46ddbc540fea` |
| `06_descriptor_incomplete.yaml` | `fd56fe21bfac4554406e57ee040b74e06a7d5cd77564d515ab2d48205a46c84b` |
| `07_match_shape.yaml` | `6507ad73e36c7a7cc045a06ec9265338c11766080dd6005b6b0c61eeaa586843` |
| `08_bind_required.yaml` | `c36a0dd4d06db6e785897b4e468a9a5526904c3de4e05ed99021d5f6da014330` |
| `09_unknown_source.yaml` | `0e8e69e7d246b53febffe810838eb48f75822c05d15f0e0f01c7ce9925a406a3` |
| `10_unknown_target_control.yaml` | `d00fb60220f7ef5701e75c6afb7c86d221605bb36f940e3182f989585409088e` |
| `11_unresolved_ref.yaml` | `9f15852828faac9fb986e36cf052fb13242f94ab5c853ea1c0fb1a0804febbb7` |
| `12_hat_direction.yaml` | `283c4d5fd76cda52a2702d151f7224d70161f656c86bb4f828c3b873cdbaf56e` |
| `13_polarity_policy_required.yaml` | `68c9f2d3131258b81b325c09e8c23f3fd25b9cf0dd0c7479ec1034eac99ab070` |
| `14_axis_to_button_threshold.yaml` | `f3b655cae770f67a0f134aa8d651b5a4457e9a4c6be5d99c54db66248667fd86` |
| `15_button_to_axis_value.yaml` | `b877e21fc832ae21d6886d5bc00ade8c7ce4c800104a0e3fc49b41d2f7d0d084` |
| `16_bind_target.yaml` | `42a0c1bf7b960817e2ef7998515276074a94609ab9d0686001fc297bbf46716a` |
| `17_bind_source.yaml` | `70d73cb8a5d4ae7a78ba41d1951ea18534a78ed1f8db0b9de2df80d26bcf8de5` |
| `18_response_on_non_axis.yaml` | `70c348fe1784b20d4c975a5c09f71ad27f67dd21c2c6507a50c595d537dc19c2` |
| `19_invert_type.yaml` | `1a3182c522b75e527aaedc2c4aa41abdabb2d8caae437e7845159c0cfd52e460` |
| `20_duplicate_id.yaml` | `7ee4e433c94153ecf548e6dd89553b9463932d225940b0c6c5ac744b80128d7e` |
| `21_profile_name.yaml` | `1cc3f3a26c624a091c258a75f208300997b9a310ea0e18d1c76040faae925f0c` |
| `22_game_title_id.yaml` | `761c25b7d4baca669098a13c080099b1d227f46cb89bf54adfc989b3cad5b882` |
| `23_bindings_map_shape.yaml` | `d5567422a29f86e869ca36d175c729d2f7198800f7a3bf83f0baa53f3d162118` |
| `24_port_range.yaml` | `f9df43f21c9fa77a230cff30888272d05f2def9f0ac1acc7c4fa3de5ec6fde0a` |
| `25_port_profile_target.yaml` | `27c76aeb5231a558df7fc7e6df68ab9d80ef9e127975b93616339e3af6adc31e` |
| `26_port_path_shape.yaml` | `7b7c6ae90862d5b2fa0b6a067cd27d23059e48cb74c88b79edf83826fbfe8278` |
| `27_port_device.yaml` | `ee02330d047ad8c0b336dace0173554a293899b2ef189745f08a88dd56cf8495` |
| `28_last_used_shape.yaml` | `71f85ae546aecafba39cacf70eca9fd804aaee2d34f54c7d131d8f8d113effbe` |
| `29_port_path_required.yaml` | `928d11dc5e935f9d5e00f2f8ccc8b93f807d64a61234eb2f090260ac25fe7aac` |
| `30_mirror_type.yaml` | `5fa044aa9b54c8a922e02b52c74224a1387fae6948a456ac9c789eff4c199a48` |
| `31_devices_required.yaml` | `20c6b8f49714eaecbfe1815d66959a49aae4afa2502bbf63312f4db1aac8757d` |
| `32_detent_positions.yaml` | `1ad15f7af41914074ecc1bc313025b05fd5fa1b1ec8a560012e01ec62255b812` |
| `33_bindings_target_mismatch.yaml` | `0e1bb1ca95b1feaac386073d95ad19d916df07afe5c5b2f6d0b909219db08b36` |
| `34_parse_error.yaml` | `8a46ac9716dbf3100968ebe571d0d921886f86d9fe3373099c59c1f704bfa403` |
| `35_response_deadzone.yaml` | `1870f82303909ae932dfa7ba08ac79b7270c985446cfc9c0c338c7bb6b0eb226` |
| `36_response_saturation.yaml` | `a4740c05bec38d9d6ba5a03e517f0da56ae3b9339d67829cd2aebec4130cc53e` |
| `37_response_curve.yaml` | `4f7617a0a102bd8fcd9f4004c7e343994f946d84291cacd0dbf19f04127808b5` |
| `38_button_to_axis_value_range.yaml` | `d2d9ed5a2fae1b645269cece169b012ee31e5c69e713c837edb89a92fde37ed2` |
| `39_button_to_axis_ramp.yaml` | `20e70c946c50239ace3424431d20fae416b4014663f248e1fc4e4aa7344d2282` |
| `40_rate_on_spring_back.yaml` | `bbbbd6c5614c0b9d618149cfb50bfa62bbe7455e138f7d67dd95549bd67cb42e` |
| `41_value_on_stay_put.yaml` | `e8d943b3335149793275f0619563630cd95bf8c272ef21c07771f5982e8c8b35` |
| `42_button_to_axis_rate.yaml` | `98a3ea28317802469958ab74c6761d486082233dc8c6d7c16062f7df2e830577` |
| `43_button_to_axis_rate_range.yaml` | `e42e50b749310cfd849a05178096dce1abad8a66745f21e6b172adbe3ed6fc18` |
| `44_ramp_on_stay_put.yaml` | `e924725a37c9b127d2e59fd9dee6b19679b76ff5c14dfb7f1634e1b48f0cd97f` |
| `45_axis_to_button_threshold_range.yaml` | `9b3c5340a43a4dea683d2015a4c46b31c1ec1831b9926498c08af0ffbdac18df` |
| `46_axis_to_button_hysteresis.yaml` | `7c051424ce50e3d39a07d0c738de5799d457b8eb909b18f9edc188da047b6609` |
| `MANIFEST.tsv` | `04cbde3fb8dc0a7efa6c5edbb8ed487c63033607f53dc9ac72e4d80cf6662541` |
