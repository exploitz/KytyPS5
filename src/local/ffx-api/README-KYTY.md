AMD FidelityFX SDK 1.1.4 API headers (ffx-api/include/ffx_api), MIT licence (LICENSE.txt), for
`src/local/fsr-frame-gen.cpp`. The only edit: `ffx_api.h` no longer declares the entry points
`__declspec(dllexport)` (they are loaded from the prebuilt `amd_fidelityfx_vk.dll` at run time).
