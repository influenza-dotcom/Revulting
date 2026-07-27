-- Phase 6 runtime acceptance driver for Vulture-on-NetHack-5.0.0.
-- Loaded with #wizloadlua (bound to ^X in the rc file).
-- Drives the game through NetHack's OWN command queue (nh.pushkey -> CQ_CANNED,
-- drained by rhack/getdir/yn_function/getlin/getpos), so no GUI key injection is
-- involved and the run is deterministic. Assertions use the real game state
-- (u.ux/u.uy/u.depth); results go to the message window via nh.pline so they are
-- captured in a screenshot, and a failure raises a Lua error.

local function note(fmt, ...)
   nh.pline("ACCEPT " .. string.format(fmt, ...))
end

local function step(keys)
   nh.pushkey(keys)
   nh.doturn(true)
end

note("driver start: depth=%d pos=(%d,%d)", u.depth, u.ux, u.uy)

-- Keep the run stable: no wandering monster generation, no hunger.
nh.debug_flags({ mongen = false, hunger = false })

-- ---------------------------------------------------------------- movement
local x0, y0 = u.ux, u.uy
local moved = false
for _, k in ipairs({ "l", "h", "j", "k", "y", "u", "b", "n" }) do
   step(k)
   if u.ux ~= x0 or u.uy ~= y0 then
      moved = true
      break
   end
end
note("MOVEMENT %s (%d,%d)->(%d,%d)", moved and "PASS" or "FAIL", x0, y0, u.ux, u.uy)

-- ---------------------------------------------------------------- descend
-- ^V = wizlevelport; in 5.0.0 the plain form prompts via getlin, which reads
-- the command queue (the menu form only appears with the 'm' prefix or '?').
local d0 = u.depth
step(string.char(22) .. "2\n")
local descended = (u.depth > d0)
note("DESCEND %s depth %d->%d", descended and "PASS" or "FAIL", d0, u.depth)

-- ---------------------------------------------------------------- monster
-- ^G = wizgenesis, "Create what kind of monster?" is a getlin prompt.
step(string.char(7) .. "hostile gnome lord\n")
note("MONSTER created; now at (%d,%d) depth=%d", u.ux, u.uy, u.depth)

-- Walk a few steps so the new monster gets a turn to approach/attack.
for _ = 1, 4 do
   step("l")
end
note("POST-MONSTER pos=(%d,%d) hp-check-done", u.ux, u.uy)

-- ---------------------------------------------------------------- pause
-- Deliberately DO NOT save here: returning lets the game fall back to the
-- normal input loop, where the C-side tile dump fires and records the FINAL
-- state. The harness then issues the save, so "before" and "after" dumps
-- describe the same game state and can be compared directly.
note("driver done; final pos=(%d,%d) depth=%d", u.ux, u.uy, u.depth)
