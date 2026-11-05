# Vendored PipeWire resampler

The files `resample.h`, `resample-native-impl.h`, `resample-native.c`,
`resample-native-c.c` and `dbesi0.c` are copied unmodified from PipeWire:

- Source: https://github.com/PipeWire/pipewire/tree/2173ca6a92fc117a457ad2598961f61485854fad/spa/plugins/audioconvert
- License: MIT (PipeWire, Copyright Wim Taymans), see the SPDX header in each file.
  `dbesi0.c` is Takuya Ooura's Bessel function, used under its own permissive notice.

Only the portable C kernels (`resample-native-c.c`) are built. The CPU-specific
kernels, precomputed filter tables and PipeWire's SPA logging are not included.
`RESAMPLE_DISABLE_PRECOMP` is defined for the ALSA driver target, so filters are
computed at runtime.

`spa-shim/` provides the handful of `spa/...` declarations those files use. It
replaces PipeWire's SPA headers, which are not vendored.

`pipewire_resample_bridge.{h,c}` is a small C API over the upstream
`struct resample`. `JackResampler.{h,cpp}` is the C++ interface that the rest of
JACK uses. It is injected through `JackResamplerFactory`; PipeWire is the only
built-in implementation.
