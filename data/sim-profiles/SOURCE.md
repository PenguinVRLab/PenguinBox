# The Sim Configurator's shipped documents, vendored

SPDX-FileCopyrightText: 2026 Patrick Carey
SPDX-License-Identifier: MIT

Copied unchanged from the sim-configurator repo (MIT, the same author) at its `main` `80d47c2072fe25498f6806c3cdf389d4bd590329` (2026-11-15),
`src/sim_configurator/profile/data/*.yaml`: the app's package data (targets, the shipped devices, the VR catalogue),
which the app loads before a person's profile folder. PenguinBox's profile reader (`ui/xemu-sim-profile-reader.cc`)
loads them the same way. Refresh them from the app in one commit, with this table; never edit them here.

(Until 2026-11-15 the set was from `7bcd3c8`, then three files from the app's branch commit `a293ba6`; main now carries
those: two header comments reworded for this repo's public-copy check, the DIY box's labels, and `returns_to_rest:
false` on the Steel Battalion aiming lever, the app's S-74.)

| File | sha256 |
|---|---|
| `device_diy_button_box_6.yaml` | `9938e4dbed3b6da5da1cecc880548b1c020b157077cc595d10fddd09cd702dd3` |
| `device_driving_force_shifter.yaml` | `be9cec67c7f86a65ad400c757a6034a59c0afb05a2def4f13ba11cec0866c077` |
| `device_g923_pedals.yaml` | `9048683566851589cba73190714fc763f14bcda4654f3ac2ecb18be7bf6a6dde` |
| `device_g923_ps.yaml` | `94e3807ca9b0e8e7063a87c269c970af0c49bb870806a3e4df6169f1d4e8c7c6` |
| `device_g923_xbox.yaml` | `5b793b2bb49d8b511b7fe7c776b381cce6384e090dddb0554b9ca214689473ec` |
| `device_keyboard_104.yaml` | `f6a6e7c87e2542ee3636e44b7276404ba417d7a0dc0cb7af01e73494d52e6fb8` |
| `device_mouse.yaml` | `8226d98199f8016797bc42538303c439a34407d32c79863ee48e173983fdc92b` |
| `device_t16000m.yaml` | `61a28039466aaa73e4e1ce6be503d8beb16d6c495fdf6e485cf256a268aa5240` |
| `device_t16000m_left.yaml` | `82fb0759671eed190f58784d17e5bc8c57d447536a66d4a42c245c85142e1844` |
| `device_warthog_stick.yaml` | `f2ff6dbf885e78bf693ad1bc88b1b500041640148f1b3ee0e94d8a34efd5b344` |
| `device_warthog_throttle.yaml` | `ed0d2d9dc8814735ae9323bb0517a3fc641674aa37d63e50887b9291951fd059` |
| `target_cyberpuck.yaml` | `11c9bd6c36582f4f0b80bcaf50ce1f9765180a7b5f21a5aa10a8d17c9e871f4d` |
| `target_steel_battalion.yaml` | `ea1b9b9716450904bdfa6487148648a3219d282f6811a3a10982d1fff60fe0b8` |
| `target_xbox_duke.yaml` | `6c1e893a9cfcbb12ef5b7a50ba317c3bc98b76ea9e0831e0299fd9facc6d224b` |
| `vr_catalogue.yaml` | `29682541a91aafcab14f64058f75c134300db202a592aeeaf197bca906d34468` |
