# cgltf 1.14 (vendored)

The glTF 2.0 reader the near-field layer's hand loader uses (`hw/xbox/nv2a/pgraph/vk/vr_hands_gltf.c`): a
single-file C99 parser with no dependencies.

- `cgltf.h`: the v1.14 tag's header, `https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.14/cgltf.h`,
  sha256 `19db3bdf85f6aa991a3b9d056641bcb39a2ef11cd5211e7a9f6b6c307d9ebc64`. Not edited.
- `cgltf.c`: the one translation unit with `CGLTF_IMPLEMENTATION`.
- Licence: MIT, `LICENSE` (the repository's, sha256
  `f619925f80ef862497aaf8e8155ef218fa6a2190055129523ca3df9119a9ba95`); the same notice closes `cgltf.h`. The JSON
  tokenizer amalgamated into it, jsmn, is MIT as well (its notice is in the header).
