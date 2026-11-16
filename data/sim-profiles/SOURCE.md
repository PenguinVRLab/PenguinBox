# The Sim Configurator's shipped documents, vendored

SPDX-FileCopyrightText: 2026 Patrick Carey
SPDX-License-Identifier: MIT

Copied unchanged from the sim-configurator repo (MIT, the same author) at its `main`
`ee9e736288b733250867c79c1603052d2efb8a8c` (2026-11-15; it carries the branch
`dm4800/feat/contract-levers-hats-20261115`, first vendored here from its tip `8515ae4`). At that commit
`tests/fixtures/merge/vectors.yaml` has sha256 `03e6a916445ee6f0ab517e36eb30ae5a42123d8eae24b50fb51f559cd4d1c02e` and
`tests/fixtures/bad/MANIFEST.tsv` sha256 `84b97f09edc092e12c4a68df31cdaca667309207e7a7b62c83457afef57512ab`.
`src/sim_configurator/profile/data/*.yaml`, 16 files: the app's package data (targets, the shipped devices, the VR
catalogue), which the app loads before a person's profile folder. PenguinBox's profile reader
(`ui/xemu-sim-profile-reader.cc`) loads them the same way. Refresh them from the app in one commit, with this table;
never edit them here.

Since the app's `main` `80d47c2`: the steel_battalion target's levers declare their positions (the gear lever 254, 255,
1-5; S-79); the G923 pedals and both T.16000M files declare each unit's own ranges (Device Studio, regenerated); and the
real Steel Battalion controller's device file (`device_steel_battalion_controller.yaml`, `source: hid_usb`) is new. This
reader loads a hid_usb device document as data only.

| File | sha256 |
|---|---|
| `device_diy_button_box_6.yaml` | `9938e4dbed3b6da5da1cecc880548b1c020b157077cc595d10fddd09cd702dd3` |
| `device_driving_force_shifter.yaml` | `be9cec67c7f86a65ad400c757a6034a59c0afb05a2def4f13ba11cec0866c077` |
| `device_g923_pedals.yaml` | `f205ef1dea4b16d2222e1eef35c188c177f6e0fb1228ac8e432834976a576a5a` |
| `device_g923_ps.yaml` | `94e3807ca9b0e8e7063a87c269c970af0c49bb870806a3e4df6169f1d4e8c7c6` |
| `device_g923_xbox.yaml` | `5b793b2bb49d8b511b7fe7c776b381cce6384e090dddb0554b9ca214689473ec` |
| `device_keyboard_104.yaml` | `f6a6e7c87e2542ee3636e44b7276404ba417d7a0dc0cb7af01e73494d52e6fb8` |
| `device_mouse.yaml` | `8226d98199f8016797bc42538303c439a34407d32c79863ee48e173983fdc92b` |
| `device_steel_battalion_controller.yaml` | `4b3b14e51006735df0f88255f484db356218d6d5f8001af8ec8a1b443d6d51c4` |
| `device_t16000m.yaml` | `52c55a3e773a9ad4832ee971f648120aa5230a36569431ce762d499687f75743` |
| `device_t16000m_left.yaml` | `fdefeb0fcb6812a970a694ea0d9324a7533e169b30c5cc3023e31801958f2730` |
| `device_warthog_stick.yaml` | `f2ff6dbf885e78bf693ad1bc88b1b500041640148f1b3ee0e94d8a34efd5b344` |
| `device_warthog_throttle.yaml` | `ed0d2d9dc8814735ae9323bb0517a3fc641674aa37d63e50887b9291951fd059` |
| `target_cyberpuck.yaml` | `11c9bd6c36582f4f0b80bcaf50ce1f9765180a7b5f21a5aa10a8d17c9e871f4d` |
| `target_steel_battalion.yaml` | `a2bedcb75440f0d5c98563208effcb6346bb930bfd153e7b56f76a911802df35` |
| `target_xbox_duke.yaml` | `6c1e893a9cfcbb12ef5b7a50ba317c3bc98b76ea9e0831e0299fd9facc6d224b` |
| `vr_catalogue.yaml` | `29682541a91aafcab14f64058f75c134300db202a592aeeaf197bca906d34468` |
