-- The visuals fixture: the fills and the sprite masks of original HUD canvases (WeaponQuickslots, ReticlesCanvas,
-- Vitals2024, Compass) with their scripts, drawn by the rules of LyShine (O3DE UiImageComponent): a radial fill on one
-- quad goes along the border from its start angle; on a sliced image it clips the slices by angles from the center; a
-- corner fill turns about a corner, an edge fill about the middle of an edge; a linear fill moves the far edge. The
-- parts that need game events (a channel, a turret, the combo points, a heart rune charge) are hidden: the fixture
-- shows them and sets the amounts through UiImageBus, as their scripts do. Checks: the area of each fill mesh at
-- amounts (O3DE geometry), and the mask texture on the meshes under a sprite mask. Pictures: visuals-fills-0, -25, -50,
-- -75, -100, visuals-masks.
local quick = screen("LyShineUI/HUD2024/WeaponQuickslots/WeaponQuickslots.uicanvas", 10, true)
local reticles = screen("LyShineUI/HUD2024/Reticles/ReticlesCanvas.uicanvas", 11, true)
local vitals = screen("LyShineUI/HUD2024/Vitals/Vitals2024.uicanvas", 12, true)
local compass = screen("LyShineUI/Compass/Compass.uicanvas", 13, true)
LyShineManagerBus.Broadcast.SetState(2702338936)   -- the HUD state (CRC-32 of "hud")
frames(0.5)
check(swcui.canvas_get(quick, "enabled"), "the HUD screens show in the HUD state")

local function root_of(canvas)
  return swcui.canvas_get(canvas, "root")
end

--- The element at a chain of names: each name is the first descendant with that name below the one before.
local function at(canvas, ...)
  local id = root_of(canvas)
  for _, name in ipairs({...}) do
    id = id and find(id, name)
  end
  check(id ~= nil, "the canvas has " .. table.concat({...}, "/"))
  return id
end

--- Shows an element: it and its parents enabled, their faders at full.
local function reveal(id)
  local e = id
  while e and e:IsValid() do
    UiElementBus.Event.SetIsEnabled(e, true)
    if swcui.has(e, "fader") then
      UiFaderBus.Event.SetFadeValue(e, 1)
    end
    e = swcui.parent(e)
  end
end

local fills = {
  {name = "radial", id = at(quick, "ChargedPointsDisplayType3", "Fill")},            -- chargepiefill6, from the top
  {name = "radial from 60 degrees", id = at(quick, "ChargedPointsDisplayType3", "FillSegment")},
  {name = "radial counter-clockwise", id = at(vitals, "AbilityChannelRadial", "MeterFill")},
  {name = "sliced radial", id = at(quick, "HeartGemChargeHint", "HintProgressBar")}, -- hintholdfill (9 slices)
  {name = "corner counter-clockwise", id = at(reticles, "HeatMeter", "MainFill")},   -- 8thringfilldivided
  {name = "linear", id = at(reticles, "TurretHealthMeter", "MainFill")},
  {name = "edge", id = at(reticles, "AimPositioner", "ReloadAnim")},
}
-- No original canvas has an edge fill: the reload ring of the aim gets one (from the bottom edge), through the bus.
UiImageBus.Event.SetFillType(fills[7].id, eUiFillType_RadialEdge)
UiImageBus.Event.SetEdgeFillOrigin(fills[7].id, eUiFillEdgeOrigin_Bottom)
for _, f in ipairs(fills) do
  reveal(f.id)
end
-- The other parts of the turret details and of the channel: as their scripts show them (the meters behind the fills).
reveal(at(reticles, "HeatMeter", "UnderFill"))

-- The box of each fill in viewport pixels (tools/test-ui.sh --preview crops the pictures with it: visuals-sheet.png).
for _, f in ipairs(fills) do
  local p = {swcui.viewport_points(f.id)}
  note(string.format("box %s: %.0f %.0f %.0f %.0f", f.name, math.min(p[1], p[3], p[5], p[7]), math.min(p[2], p[4], p[6], p[8]),
    math.max(p[1], p[3], p[5], p[7]), math.max(p[2], p[4], p[6], p[8])))
end

local amounts = {0, 0.25, 0.5, 0.75, 1}
local areas = {}
for _, amount in ipairs(amounts) do
  for _, f in ipairs(fills) do
    UiImageBus.Event.SetFillAmount(f.id, amount)
  end
  UiImageBus.Event.SetFillAmount(at(reticles, "HeatMeter", "UnderFill"), math.min(1, amount + 0.1))
  frames(1 / 60)
  -- The vitals script hides the channel ring again when no channel runs: show it for the draw of this frame.
  reveal(fills[3].id)
  for _, f in ipairs(fills) do
    areas[f.name] = areas[f.name] or {}
    areas[f.name][amount] = swcui.mesh_area(f.id)
  end
  picture("visuals-fills-" .. math.floor(amount * 100 + 0.5))
end

local function ratio(name, amount)
  local full = areas[name][1]
  return full > 0 and areas[name][amount] / full or -1
end
for _, f in ipairs(fills) do
  check(areas[f.name][1] > 0, f.name .. ": the full fill has a mesh")
  equal(areas[f.name][0], 0, f.name .. ": no mesh at the amount 0")
  for i = 2, #amounts do
    check(areas[f.name][amounts[i]] > areas[f.name][amounts[i - 1]], f.name .. ": the area grows with the amount " .. amounts[i])
  end
  note(string.format("%s: area ratios %.3f %.3f %.3f", f.name, ratio(f.name, 0.25), ratio(f.name, 0.5), ratio(f.name, 0.75)))
end
-- One quad: each edge is a quarter of the walk and a quarter of the area, so the area follows the amount; a corner
-- fill moves along two edges, each one half of the area; a linear fill moves the far edge.
for _, name in ipairs({"radial", "radial from 60 degrees", "radial counter-clockwise", "corner counter-clockwise", "linear"}) do
  for _, amount in ipairs({0.25, 0.5, 0.75}) do
    near(ratio(name, amount), amount, 0.01, name .. ": area at " .. amount)
  end
end
-- Sliced: the line at 180 degrees cuts the rect through its center (one half); at 90 degrees one quarter.
near(ratio("sliced radial", 0.5), 0.5, 0.01, "sliced radial: area at 0.5")
near(ratio("sliced radial", 0.25), 0.25, 0.01, "sliced radial: area at 0.25 (a square rect)")
-- Edge: three triangles from the middle of the edge; the middle one is half of the rect.
near(ratio("edge", 0.5), 0.5, 0.01, "edge: area at 0.5")

-- The sprite masks: each mesh under a mask element has the mask texture and mask UVs.
local function masked(parent, part)
  local count = 0
  local function walk(id)
    for _, child in ipairs(swcui.children(id)) do
      local area, _, mask = swcui.mesh_area(child)
      if area > 0 and string.find(mask, part, 1, true) then
        count = count + 1
      end
      walk(child)
    end
  end
  walk(parent)
  return count
end
local hexagon = at(quick, "HeartGemContainer", "Mask")
reveal(hexagon)
for _, child in ipairs(swcui.children(hexagon)) do
  reveal(child)
end
local strip = at(compass, "Compass", "Mask")
reveal(strip)
frames(0.1)
check(masked(hexagon, "hexagon_mask") > 0, "the heart rune hexagon mask is on the meshes under it")
check(masked(strip, "compassmask") > 0, "the compass mask is on the meshes of its strip")
note(string.format("masked meshes: hexagon %d, compass %d", masked(hexagon, "hexagon_mask"), masked(strip, "compassmask")))
picture("visuals-masks")
