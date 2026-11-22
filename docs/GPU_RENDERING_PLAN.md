# Moving the map to the GPU — plan

## Why

Everything is drawn in software: every tile, sprite, effect and the light layer is
an `SDL_BlitSurface` (or a hand-written pixel loop) into one CPU-side surface
(`vulture_screen`), which is then copied to the window. At 1920×1080 a frame costs
about 17 ms in the off-screen probe (map tiles ~6 ms, light layer ~8–10 ms), plus
the window copy in real play. That caps animation smoothness and makes input feel
heavy. A GPU draws the same frame in well under 1 ms.

## What changes, in three steps

1. **SDL_Renderer for the map (biggest win, moderate work).**
   - Upload each tile / sprite once as an `SDL_Texture` (cached beside the
     existing `vulture_tile` cache; flipped and scaled sprites become
     `SDL_RenderCopyEx` with flip / scale / angle — no more `vtx_blit`).
   - Draw the map with `SDL_RenderCopy` into a render target the size of the map.
   - The light layer becomes a small texture (the existing coarse grid,
     `vl_low`) stretched by the GPU with linear filtering and blended in one
     call — this removes `vl_apply` (~8 ms) entirely.
   - Glow dots, rings, shadows, puffs: `SDL_RenderGeometry` / pre-made textures
     with additive or alpha blending.
   - The UI stays exactly as it is (software surface); its surface is uploaded
     as one texture and drawn over the map, re-uploaded only when it changes.
   - **Loses** the per-pixel normal-mapped lighting (SDL_Renderer has no
     shaders) until step 2. Everything else stays.
2. **OpenGL (or SDL3 GPU) shader for the map lighting.**
   - One fragment shader does: tile colour × light grid (bilinear) + normal map
     · light direction + glint. The normal buffer becomes a second render
     target written by the same draw calls.
   - Restores normal maps at near-zero cost and allows real point lights
     (per-pixel, not a coarse grid), soft shadows, bloom on fire and magic.
3. **Clean-up.** Remove the software paths (`vl_apply`, `vl_relief`, `vl_nmap`,
   the CPU normal buffer) once the GPU path is the default; keep a
   `--software` switch for machines without a usable GPU.

## Effort and risk

- Step 1: the map draw in `levelwin::draw` (about 900 lines) and the effect
  helpers move to the renderer; the tile cache gains textures. Several sessions.
  Risk: Vulture's windows assume they draw into `vulture_screen`; the map has to
  be isolated from the UI first.
- Step 2: a GL context and two shaders; moderate once step 1 is done.
- Both need the same off-screen probes, but the dummy SDL driver has no GPU, so
  probes would run the software fallback; GPU checks need a real window.

## Recommendation

Do step 1 first and ship it with the normal maps temporarily off (or keep the
software path as a toggle) — the speed-up is the point. Then step 2 to bring the
normal maps back better than before.
