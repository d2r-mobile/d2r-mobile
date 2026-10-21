# HUD-to-swapchain presenter (#199)

`dm_touch_hud_swapchain` is a bounded owned presenter for the accepted HUD
geometry. It records an actual `vkCmdDraw` into the acquired swapchain image's
existing render pass, framebuffer and command buffer, then leaves submit and
present to the accepted `dm_vk_swapchain_*` path.

- Create takes the chain's render pass/format as the pipeline identity and
  creates one host-visible vertex buffer per swapchain image. Each image's
  buffer is safe to rewrite only after `dm_vk_swapchain_acquire` returned that
  image (the chain's present-fence proof has completed for it).
- `record` validates the snapshot and requires its pixel extent to exactly
  equal the chain extent before building the accepted HUD vertices. It binds
  the dynamic viewport/scissor, uses alpha blending, and records the draw into
  `chain->commands[chain->image_index]` with the chain clear value.
- The default clear-only command path and public swapchain API are unchanged.
  HUD is an explicit caller opt-in.
- Partial creation failures (either shader, pipeline layout/pipeline, vertex
  array allocation, or a later per-image buffer) return the original error and
  clean only resources actually created. The cleanup loop is guarded by the
  allocated vertex-buffer array, so a failure before that allocation cannot
  dereference a null/small pointer; no double-destroy path is used.
- Reconfigure is rejected while any acquire/submit/present fence is pending.
  Destroy requires the caller to have completed the chain's presentation
  retirement; no `vkDeviceWaitIdle` is introduced.
- Unsupported chain format/render pass, invalid snapshot, wrong extent, or
  unavailable resources return explicit failure with no draw and no fallback.

`dm_touch_hud_build_vertices` is the shared geometry extraction used by both the
accepted offscreen HUD renderer and this presenter. Offscreen pixel checks are
supporting evidence only; they do not prove swapchain pixels or device output.

The NativeActivity caller is intentionally not wired in this slice (H3 owns
native.c/IME). The next integration point is the existing explicit diagnostic
WSI mode; parent #34/#18 remain open for real-window/device presentation.
