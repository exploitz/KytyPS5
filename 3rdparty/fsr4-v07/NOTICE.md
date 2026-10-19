# Third-party notices

This folder holds the Vulkan provider of an experimental FSR 4 "v07" upscaler, taken from the Q2RTX FSR Vulkan
port: `src/` and `include/`.

- `LICENSE.txt` (MIT, Q2RTX FSR Vulkan contributors) applies to the provider and its portability layer.
- `include/ffx_vk_fsr4_v07_types.h`, and the provider code built on its definitions, were extracted from the AMD
  FidelityFX SDK 2.0.0, released by AMD under the MIT license: AMD's notice is in `LICENSE-AMD-FidelityFX-SDK.txt`.

This folder holds no shader binaries, model initializers or weights. The emulator loads the v07 model files at run
time from `deps\fsr4` next to it; a package that includes them carries their own notice there
(`LICENSE-FSR4-v07.txt`, with their source and license), and its distributor is responsible for their provenance.

This is an older community-hosted FSR 4 source, not AMD's official FSR 4 release: do not describe it as AMD FSR 4,
FSR 4.1.1, Ray Regeneration or ML Frame Generation.
