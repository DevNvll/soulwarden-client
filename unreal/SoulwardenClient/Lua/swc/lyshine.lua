-- The LyShine buses on the element model (Source/SwcUi Model and its natives). The method names and arguments are
-- the ones that the scripts of the game call (research ui.md 3.2); the behavior follows LyShine 1.12 (O3DE LyShine as
-- the reference, Apache-2.0 OR MIT, facts only). Vectors that the engine returns are Lua tables (1-based), as the
-- scripts read them (#list, list[i]).

local Swc = Swc
local V2 = function(x, y) return Swc.Vector2.new({x = x or 0, y = y or 0}) end

local function valid(id)
  return id ~= nil and type(id) == "userdata" and swcui.exists(id)
end

local function color_of(r, g, b, a)
  if r == nil then
    return nil
  end
  return Swc.Color.new({r = r, g = g, b = b, a = a or 1})
end

local function unpack_color(c)
  if c == nil then
    return 1, 1, 1, 1
  end
  return c.r or 1, c.g or 1, c.b or 1, c.a or 1
end

-- ------------------------------------------------------------------------------------------------- UiElementBus

Swc.Define("UiElementBus", "Event", {
  GetName = function(id) return swcui.name(id) end,
  SetName = function(id, name) swcui.set_name(id, name) end,
  GetCanvas = function(id)
    if id == nil or Swc.unlinked[tostring(id)] then
      return EntityId()
    end
    return swcui.canvas_of(id)
  end,
  GetParent = function(id) return swcui.parent(id) end,
  GetChildren = function(id) return swcui.children(id) end,
  GetChild = function(id, index) return swcui.child(id, index) end,
  GetNumChildElements = function(id) return swcui.num_children(id) end,
  GetIndexOfChildByEntityId = function(id, child) return swcui.index_of_child(id, child) end,
  GetIndexOfChild = function(id, child) return swcui.index_of_child(id, child) end,
  FindChildByName = function(id, name) return swcui.find_child(id, name) end,
  FindDescendantByName = function(id, name) return swcui.find_descendant(id, name) end,
  IsEnabled = function(id) return swcui.enabled(id) end,
  SetIsEnabled = function(id, enabled) swcui.set_enabled(id, enabled and true or false) end,
  GetAreElementAndAncestorsEnabled = function(id) return swcui.enabled_in_hierarchy(id) end,
  IsRenderEnabled = function(id) return swcui.enabled(id) end,
  SetIsRenderEnabled = function(id, enabled) swcui.set_enabled(id, enabled and true or false) end,
  Reparent = function(id, parent, before) return swcui.reparent(id, parent, before) end,
  ReparentByIndex = function(id, parent, index)
    local children = swcui.children(parent)
    return swcui.reparent(id, parent, children[(index or 0) + 1])
  end,
  DestroyElement = function(id) Swc.DestroyElement(id) end,
  GetRenderPriority = function(id) return swcui.get(id, "element.renderPriority") end,
  SetRenderPriority = function(id, priority) swcui.set(id, "element.renderPriority", priority) end,
  GetElementId = function(id) return 0 end,
})

-- ----------------------------------------------------------------------------------------------- the transforms

local function local_position(id)
  local x, y = swcui.local_position(id)
  return x, y
end

Swc.Define("UiTransformBus", "Event", {
  GetLocalPosition = function(id) return V2(local_position(id)) end,
  SetLocalPosition = function(id, p) swcui.set_local_position(id, p.x, p.y) end,
  GetLocalPositionX = function(id) local x = local_position(id) return x end,
  GetLocalPositionY = function(id) local _, y = local_position(id) return y end,
  SetLocalPositionX = function(id, x) local _, y = local_position(id) swcui.set_local_position(id, x, y) end,
  SetLocalPositionY = function(id, y) local x = local_position(id) swcui.set_local_position(id, x, y) end,
  MoveLocalPositionBy = function(id, d) local x, y = local_position(id) swcui.set_local_position(id, x + d.x, y + d.y) end,
  GetViewportPosition = function(id) return V2(swcui.viewport_position(id)) end,
  SetViewportPosition = function(id, p) swcui.set_viewport_position(id, p.x, p.y) end,
  MoveViewportPositionBy = function(id, d) local x, y = swcui.viewport_position(id) swcui.set_viewport_position(id, x + d.x, y + d.y) end,
  GetCanvasPosition = function(id) return V2(swcui.canvas_position(id)) end,
  SetCanvasPosition = function(id, p) swcui.set_canvas_position(id, p.x, p.y) end,
  -- The fork: a viewport point in the local space of the element (canvas units from the top left of its rect, no
  -- rotation or scale; tooltip.luac places the tooltip at the cursor with it).
  ViewportPointToLocalPoint = function(id, p)
    local x, y = swcui.to_rect_space(id, p.x, p.y)
    local l, t = swcui.rect(id)
    return V2(x - l, y - t)
  end,
  GetPivot = function(id) return V2(swcui.get(id, "transform.pivot")) end,
  SetPivot = function(id, p) swcui.set(id, "transform.pivot", p.x, p.y) end,
  GetPivotX = function(id) local x = swcui.get(id, "transform.pivot") return x end,
  GetPivotY = function(id) local _, y = swcui.get(id, "transform.pivot") return y end,
  SetPivotX = function(id, v) local _, y = swcui.get(id, "transform.pivot") swcui.set(id, "transform.pivot", v, y) end,
  SetPivotY = function(id, v) local x = swcui.get(id, "transform.pivot") swcui.set(id, "transform.pivot", x, v) end,
  GetScale = function(id) return V2(swcui.get(id, "transform.scale")) end,
  SetScale = function(id, s) swcui.set(id, "transform.scale", s.x, s.y) end,
  GetScaleX = function(id) local x = swcui.get(id, "transform.scale") return x end,
  GetScaleY = function(id) local _, y = swcui.get(id, "transform.scale") return y end,
  SetScaleX = function(id, v) local _, y = swcui.get(id, "transform.scale") swcui.set(id, "transform.scale", v, y) end,
  SetScaleY = function(id, v) local x = swcui.get(id, "transform.scale") swcui.set(id, "transform.scale", x, v) end,
  GetZRotation = function(id) return swcui.get(id, "transform.rotation") end,
  SetZRotation = function(id, v) swcui.set(id, "transform.rotation", v) end,
  GetScaleToDevice = function(id) return swcui.get(id, "transform.scaleToDevice") end,
  SetScaleToDevice = function(id, v) swcui.set(id, "transform.scaleToDevice", v and true or false) end,
  GetComputeTransformWhenHidden = function(id) return true end,
  SetComputeTransformWhenHidden = function(id, v) end,
  SetRecomputeFlags = function(id, flags) end,
  GetViewportSpaceRect = function(id)
    -- The axis-aligned bounds of the four corners in the viewport (a table with left, top, right, bottom and the size).
    local x1, y1, x2, y2, x3, y3, x4, y4 = swcui.viewport_points(id)
    local left, right = math.min(x1, x2, x3, x4), math.max(x1, x2, x3, x4)
    local top, bottom = math.min(y1, y2, y3, y4), math.max(y1, y2, y3, y4)
    return {left = left, top = top, right = right, bottom = bottom, GetWidth = function() return right - left end,
            GetHeight = function() return bottom - top end, GetCenterX = function() return (left + right) / 2 end,
            GetCenterY = function() return (top + bottom) / 2 end,
            GetCenter = function() return V2((left + right) / 2, (top + bottom) / 2) end,
            GetSize = function() return V2(right - left, bottom - top) end}
  end,
  GetViewportSpacePoints = function(id)
    local x1, y1, x2, y2, x3, y3, x4, y4 = swcui.viewport_points(id)
    return {pt = {V2(x1, y1), V2(x2, y2), V2(x3, y3), V2(x4, y4)}, TopLeft = function() return V2(x1, y1) end,
            TopRight = function() return V2(x2, y2) end, BottomRight = function() return V2(x3, y3) end,
            BottomLeft = function() return V2(x4, y4) end}
  end,
  IsPointInRect = function(id, p)
    local x1, y1, x2, y2, x3, y3, x4, y4 = swcui.viewport_points(id)
    local left, right = math.min(x1, x2, x3, x4), math.max(x1, x2, x3, x4)
    local top, bottom = math.min(y1, y2, y3, y4), math.max(y1, y2, y3, y4)
    return p.x >= left and p.x <= right and p.y >= top and p.y <= bottom
  end,
})

Swc.Define("UiTransform2dBus", "Event", {
  GetAnchors = function(id) return UiAnchors(swcui.get(id, "transform.anchors")) end,
  SetAnchors = function(id, a) swcui.set(id, "transform.anchors", a.left, a.top, a.right, a.bottom) end,
  SetAnchorsScript = function(id, a) swcui.set(id, "transform.anchors", a.left, a.top, a.right, a.bottom) end,
  GetOffsets = function(id) return UiOffsets(swcui.get(id, "transform.offsets")) end,
  SetOffsets = function(id, o) swcui.set(id, "transform.offsets", o.left, o.top, o.right, o.bottom) end,
  GetLocalWidth = function(id) local w = swcui.local_size(id) return w end,
  GetLocalHeight = function(id) local _, h = swcui.local_size(id) return h end,
  SetLocalWidth = function(id, w) swcui.set_local_width(id, w) end,
  SetLocalHeight = function(id, h) swcui.set_local_height(id, h) end,
})

-- ----------------------------------------------------------------------------------------------------- the image

local function image(id)
  return id ~= nil and swcui.has(id, "image")
end

Swc.Define("UiImageBus", "Event", {
  GetColor = function(id) if image(id) then return color_of(swcui.get(id, "image.color")) end end,
  SetColor = function(id, c) if image(id) then swcui.set(id, "image.color", unpack_color(c)) end end,
  GetAlpha = function(id) if image(id) then return swcui.get(id, "image.alpha") end end,
  SetAlpha = function(id, a) if image(id) then swcui.set(id, "image.alpha", a) end end,
  GetSpritePathname = function(id) if image(id) then return swcui.get(id, "image.sprite") end end,
  -- A value that is not a text (data of a stubbed game bus) gives no sprite.
  SetSpritePathname = function(id, path) if image(id) then swcui.set(id, "image.sprite", type(path) == "string" and path or "") end end,
  SetSpritePathnameIfExists = function(id, path)
    if image(id) and path and swcui.texture_exists(path) then
      swcui.set(id, "image.sprite", path)
      return true
    end
    return false
  end,
  -- The fork: a path and a sprite cache pool (the pool is a memory budget of the client).
  SetSpritePathnamePool = function(id, path) if image(id) then swcui.set(id, "image.sprite", path or "") end end,
  UnloadTexture = function(id) end,
  GetImageType = function(id) if image(id) then return swcui.get(id, "image.type") end end,
  SetImageType = function(id, v) if image(id) then swcui.set(id, "image.type", v) end end,
  GetFillType = function(id) if image(id) then return swcui.get(id, "image.fillType") end end,
  SetFillType = function(id, v) if image(id) then swcui.set(id, "image.fillType", v) end end,
  GetFillAmount = function(id) if image(id) then return swcui.get(id, "image.fillAmount") end end,
  SetFillAmount = function(id, v) if image(id) then swcui.set(id, "image.fillAmount", v) end end,
  GetRadialFillStartAngle = function(id) if image(id) then return swcui.get(id, "image.fillStartAngle") end end,
  SetRadialFillStartAngle = function(id, v) if image(id) then swcui.set(id, "image.fillStartAngle", v) end end,
  GetEdgeFillOrigin = function(id) if image(id) then return swcui.get(id, "image.fillEdgeOrigin") end end,
  SetEdgeFillOrigin = function(id, v) if image(id) then swcui.set(id, "image.fillEdgeOrigin", v) end end,
  GetCornerFillOrigin = function(id) if image(id) then return swcui.get(id, "image.fillCornerOrigin") end end,
  SetCornerFillOrigin = function(id, v) if image(id) then swcui.set(id, "image.fillCornerOrigin", v) end end,
  GetFillClockwise = function(id) if image(id) then return swcui.get(id, "image.fillClockwise") end end,
  SetFillClockwise = function(id, v) if image(id) then swcui.set(id, "image.fillClockwise", v and true or false) end end,
  GetFillCenter = function(id) if image(id) then return swcui.get(id, "image.fillCenter") end end,
  SetFillCenter = function(id, v) if image(id) then swcui.set(id, "image.fillCenter", v and true or false) end end,
  GetImageIndex = function(id) if image(id) then return swcui.get(id, "image.index") end end,
  SetImageIndex = function(id, v) if image(id) then swcui.set(id, "image.index", v) end end,
  GetImageIndexCount = function(id) if image(id) then return swcui.get(id, "image.cells") end end,
  -- The fork: a part of the texture (four numbers: left, top, right, bottom in 0..1, or a table with them).
  SetUVOverrides = function(id, a, b, c, d)
    if not image(id) then
      return
    end
    if type(a) == "number" then
      swcui.set(id, "image.uv", a, b, c, d)
    elseif a ~= nil and type(a) ~= "number" and a.left ~= nil then
      swcui.set(id, "image.uv", a.left, a.top, a.right, a.bottom)
    elseif a ~= nil and b ~= nil and a.x ~= nil then
      swcui.set(id, "image.uv", a.x, a.y, b.x, b.y)
    else
      Swc.Stub("argument", "UiImageBus.Event.SetUVOverrides")
    end
  end,
  ClearUVOverrides = function(id) if image(id) then swcui.set(id, "image.uv", nil) end end,
})

-- ------------------------------------------------------------------------------------------------------ the text

local function text(id)
  return id ~= nil and swcui.has(id, "text")
end

local function set_text(id, value, flags)
  if not text(id) then
    return
  end
  value = tostring(value or "")
  -- Bit 1: localize (a "@key" text); the runtime also localizes a "@key" text of the canvas data.
  if flags == nil or flags >= 2 or value:sub(1, 1) == "@" then
    value = swcui.localize(value)
  end
  swcui.set(id, "text.text", value)
end

Swc.Define("UiTextBus", "Event", {
  GetText = function(id) if text(id) then return swcui.get(id, "text.text") end end,
  SetText = function(id, value) set_text(id, value, 0) end,
  SetTextWithFlags = function(id, value, flags) set_text(id, value, flags or 0) end,
  SetTextWithFlagsAndPronounType = function(id, value, flags) set_text(id, value, flags or 0) end,
  GetColor = function(id) if text(id) then return color_of(swcui.get(id, "text.color")) end end,
  SetColor = function(id, c) if text(id) then swcui.set(id, "text.color", unpack_color(c)) end end,
  GetAlpha = function(id) if text(id) then return swcui.get(id, "text.alpha") end end,
  SetAlpha = function(id, a) if text(id) then swcui.set(id, "text.alpha", a) end end,
  GetFont = function(id) if text(id) then return swcui.get(id, "text.font") end end,
  SetFont = function(id, font) if text(id) and font then swcui.set(id, "text.font", font) end end,
  GetFontSize = function(id) if text(id) then return swcui.get(id, "text.size") end end,
  SetFontSize = function(id, v) if text(id) then swcui.set(id, "text.size", v) end end,
  GetFontEffect = function(id) if text(id) then return swcui.get(id, "text.effect") end end,
  SetFontEffect = function(id, v) if text(id) then swcui.set(id, "text.effect", v) end end,
  SetFontEffectByName = function(id, name) if text(id) then swcui.set(id, "text.effectName", name) end end,
  GetCharacterSpacing = function(id) if text(id) then return swcui.get(id, "text.charSpacing") end end,
  SetCharacterSpacing = function(id, v) if text(id) then swcui.set(id, "text.charSpacing", v) end end,
  GetLineSpacing = function(id) if text(id) then return swcui.get(id, "text.lineSpacing") end end,
  SetLineSpacing = function(id, v) if text(id) then swcui.set(id, "text.lineSpacing", v) end end,
  GetHorizontalTextAlignment = function(id) if text(id) then return swcui.get(id, "text.hAlign") end end,
  SetHorizontalTextAlignment = function(id, v) if text(id) then swcui.set(id, "text.hAlign", v) end end,
  GetVerticalTextAlignment = function(id) if text(id) then return swcui.get(id, "text.vAlign") end end,
  SetVerticalTextAlignment = function(id, v) if text(id) then swcui.set(id, "text.vAlign", v) end end,
  GetIsMarkupEnabled = function(id) if text(id) then return swcui.get(id, "text.markup") end end,
  SetIsMarkupEnabled = function(id, v) if text(id) then swcui.set(id, "text.markup", v and true or false) end end,
  GetWrapText = function(id) if text(id) then return swcui.get(id, "text.wrap") end end,
  SetWrapText = function(id, v) if text(id) then swcui.set(id, "text.wrap", v) end end,
  GetShrinkToFit = function(id) if text(id) then return swcui.get(id, "text.shrink") end end,
  SetShrinkToFit = function(id, v) if text(id) then swcui.set(id, "text.shrink", v) end end,
  GetMinimumShrinkScale = function(id) if text(id) then return swcui.get(id, "text.minShrink") end end,
  SetMinimumShrinkScale = function(id, v) if text(id) then swcui.set(id, "text.minShrink", v) end end,
  GetOverflowMode = function(id) if text(id) then return swcui.get(id, "text.overflow") end end,
  SetOverflowMode = function(id, v) if text(id) then swcui.set(id, "text.overflow", v) end end,
  GetTextCase = function(id) if text(id) then return swcui.get(id, "text.case") end end,
  SetTextCase = function(id, v) if text(id) then swcui.set(id, "text.case", v) end end,
  GetTextWidth = function(id) if text(id) then local w = swcui.get(id, "text.size2d") return w end end,
  GetTextHeight = function(id) if text(id) then local _, h = swcui.get(id, "text.size2d") return h end end,
  GetTextSize = function(id) if text(id) then return V2(swcui.get(id, "text.size2d")) end end,
})

-- ------------------------------------------------------------------------------------------- fader, desaturator

Swc.Define("UiFaderBus", "Event", {
  GetFadeValue = function(id) return swcui.get(id, "fader.fade") end,
  SetFadeValue = function(id, v) swcui.set(id, "fader.fade", v) end,
  Fade = function(id, target, speed)
    -- LyShine Fade: to the target at `speed` units each second (0: at once).
    local current = swcui.get(id, "fader.fade") or 1
    if not speed or speed <= 0 then
      swcui.set(id, "fader.fade", target)
      return
    end
    Swc.TweenValue(id, "fade", current, target, math.abs(target - current) / speed, function(v) swcui.set(id, "fader.fade", v) end)
  end,
  IsFading = function(id) return Swc.TweenIsRunning(id, "fade") end,
})

Swc.Define("UiDesaturatorBus", "Event", {
  GetSaturationValue = function(id) return swcui.get(id, "desaturator.saturation") end,
  SetSaturationValue = function(id, v) swcui.set(id, "desaturator.saturation", v) end,
})

Swc.Define("UiImageRepeaterBus", "Event", {
  GetNumRepeats = function(id) return swcui.get(id, "repeater.repeats") end,
  SetNumRepeats = function(id, v) swcui.set(id, "repeater.repeats", v) end,
})

-- --------------------------------------------------------------------------------------------------- the canvas

Swc.canvasNames = {}   -- tostring(canvas id) -> the canvas name with the case of the load path

Swc.Define("UiCanvasBus", "Event", {
  RecomputeChangedLayouts = function() swcui.update_layouts() end,
  GetCanvasName = function(canvas)
    return Swc.canvasNames[tostring(canvas)] or swcui.canvas_get(canvas, "name")
  end,
  GetPathname = function(canvas) return swcui.canvas_get(canvas, "path") end,
  GetCanvasSize = function(canvas) return V2(swcui.canvas_get(canvas, "size")) end,
  GetAuthoredCanvasSize = function(canvas) return V2(swcui.canvas_get(canvas, "authoredSize")) end,
  GetDrawOrder = function(canvas) return swcui.canvas_get(canvas, "drawOrder") end,
  SetDrawOrder = function(canvas, v) swcui.canvas_set(canvas, "drawOrder", v) end,
  GetEnabled = function(canvas) return swcui.canvas_get(canvas, "enabled") end,
  SetEnabled = function(canvas, v) swcui.canvas_set(canvas, "enabled", v and true or false) end,
  GetRootElement = function(canvas) return swcui.canvas_get(canvas, "root") end,
  GetChildElements = function(canvas) return swcui.children(swcui.canvas_get(canvas, "root")) end,
  GetChildElement = function(canvas, index) return swcui.child(swcui.canvas_get(canvas, "root"), index) end,
  GetNumChildElements = function(canvas) return swcui.num_children(swcui.canvas_get(canvas, "root")) end,
  FindElementByName = function(canvas, name) return swcui.find_descendant(swcui.canvas_get(canvas, "root"), name) end,
  CloneElement = function(canvas, source, parent, before)
    if parent == nil or not parent:IsValid() then
      parent = swcui.canvas_get(canvas, "root")
    end
    return Swc.CloneElement(source, parent, before)
  end,
  ForceHoverInteractable = function() end,
  StopMultithread = function() end,
  GetIsPixelAligned = function() return true end,
  SetIsPixelAligned = function() end,
  -- The canvas takes the pointer (IsPosInputSupported of the canvas file; the tooltip canvas does not).
  GetIsPositionalInputSupported = function(canvas) return swcui.canvas_get(canvas, "posInput") end,
  SetIsPositionalInputSupported = function(canvas, v) swcui.canvas_set(canvas, "posInput", v and true or false) end,
})

Swc.Define("UiCanvasManagerBus", "Broadcast", {
  LoadCanvas = function(path) return Swc.LoadCanvas(path) end,
  UnloadCanvas = function(canvas) Swc.UnloadCanvas(canvas) end,
  FindLoadedCanvasByPathName = function(path) return swcui.find_canvas(path) end,
  GetLoadedCanvases = function() return swcui.canvases() end,
  SetNumScreensToPreload = function() end,
  GetElementThreshold = function() return 1000000 end,
  GetTotalElementCount = function() return 0 end,
})

Swc.Handled("UiCanvasNotificationBus", "UiCanvasSizeNotificationBus")

-- ------------------------------------------------------------------------------------- spawner, flipbook, sequence

Swc.Define("UiSpawnerBus", "Event", {
  SpawnSlice = function(spawner) return Swc.SpawnSlice(spawner, swcui.get(spawner, "spawner.slice")) end,
  SpawnSlicePath = function(spawner, path) return Swc.SpawnSlice(spawner, path) end,
  SpawnSliceRelative = function(spawner, relative) return Swc.SpawnSlice(spawner, swcui.get(spawner, "spawner.slice")) end,
})

Swc.Define("UiFlipbookAnimationBus", "Event", {
  Start = function(id) if swcui.has(id, "flipbook") then Swc.FlipbookStart(id) end end,
  Stop = function(id) if swcui.has(id, "flipbook") then Swc.FlipbookStop(id) end end,
  IsPlaying = function(id) return Swc.FlipbookIsPlaying(id) end,
  GetCurrentFrame = function(id) return swcui.get(id, "flipbook.frame") end,
  SetCurrentFrame = function(id, v) swcui.set(id, "flipbook.frame", v) end,
  GetStartFrame = function(id) return swcui.get(id, "flipbook.start") end,
  SetStartFrame = function(id, v) swcui.set(id, "flipbook.start", v) end,
  GetEndFrame = function(id) return swcui.get(id, "flipbook.end") end,
  SetEndFrame = function(id, v) swcui.set(id, "flipbook.end", v) end,
  GetLoopStartFrame = function(id) return swcui.get(id, "flipbook.loopStart") end,
  SetLoopStartFrame = function(id, v) swcui.set(id, "flipbook.loopStart", v) end,
  GetLoopType = function(id) return swcui.get(id, "flipbook.loopType") end,
  SetLoopType = function(id, v) swcui.set(id, "flipbook.loopType", v) end,
  GetFramerate = function(id) return swcui.get(id, "flipbook.rate") end,
  SetFramerate = function(id, v) swcui.set(id, "flipbook.rate", v) end,
})

Swc.Define("UiImageSequenceBus", "Event", {
  GetImageIndex = function(id) return swcui.get(id, "sequence.index") end,
  SetImageIndex = function(id, v) swcui.set(id, "sequence.index", v) end,
  GetImageIndexCount = function(id) return swcui.get(id, "sequence.count") end,
  ReloadTextures = function() end,
  UnloadTextures = function() end,
})

-- -------------------------------------------------------------------------------------- layouts, interactables

-- The values of the interactables that the runtime does not use yet stay for the scripts that read them back.
local stored = {}
local function store(id)
  local k = tostring(id)
  local t = stored[k]
  if not t then
    t = {}
    stored[k] = t
  end
  return t
end

-- The layouts (the runtime computes them: Source/SwcUi/Private/Layout.cpp). A cell value that the element does not
-- override is -1 (LyShine UiLayoutCellUnspecifiedSize); a negative value set removes the override.
local function cell_accessors()
  local out = {}
  for _, name in ipairs({"MinWidth", "MinHeight", "TargetWidth", "TargetHeight", "ExtraWidthRatio", "ExtraHeightRatio"}) do
    local key = "cell." .. string.lower(name:sub(1, 1)) .. name:sub(2)
    out["Get" .. name] = function(id) if not valid(id) then return -1 end return swcui.get(id, key) end
    out["Set" .. name] = function(id, v) if valid(id) then swcui.set(id, key, v) end end
  end
  return out
end
Swc.Define("UiLayoutCellBus", "Event", cell_accessors())
Swc.Define("UiLayoutCellDefaultBus", "Event", {
  GetMinWidth = function(id) return swcui.get(id, "cell.defaultMinWidth") end,
  GetMinHeight = function(id) return swcui.get(id, "cell.defaultMinHeight") end,
  GetTargetWidth = function(id) return swcui.get(id, "cell.defaultTargetWidth") end,
  GetTargetHeight = function(id) return swcui.get(id, "cell.defaultTargetHeight") end,
  GetExtraWidthRatio = function(id) return swcui.get(id, "cell.defaultExtraWidthRatio") end,
  GetExtraHeightRatio = function(id) return swcui.get(id, "cell.defaultExtraHeightRatio") end,
})
Swc.Define("UiLayoutFitterBus", "Event", {
  GetHorizontalFit = function(id) return valid(id) and swcui.get(id, "fitter.horizontal") or false end,
  SetHorizontalFit = function(id, v) if valid(id) then swcui.set(id, "fitter.horizontal", v) end end,
  GetVerticalFit = function(id) return valid(id) and swcui.get(id, "fitter.vertical") or false end,
  SetVerticalFit = function(id, v) if valid(id) then swcui.set(id, "fitter.vertical", v) end end,
})

local function padding_of(id)
  local l, t, r, b = swcui.get(id, "layout.padding")
  return UiPadding(l or 0, t or 0, r or 0, b or 0)
end

local function line_layout(extra)
  local out = {
    GetPadding = function(id) return padding_of(id) end,
    SetPadding = function(id, p) swcui.set(id, "layout.padding", p.left, p.top, p.right, p.bottom) end,
    GetSpacing = function(id) return swcui.get(id, "layout.spacing") or 0 end,
    SetSpacing = function(id, v) swcui.set(id, "layout.spacing", v) end,
    GetOrder = function(id) return swcui.get(id, "layout.order") or 0 end,
    SetOrder = function(id, v) swcui.set(id, "layout.order", v) end,
    GetChildHorizontalAlignment = function(id) return swcui.get(id, "layout.hAlign") or 0 end,
    SetChildHorizontalAlignment = function(id, v) swcui.set(id, "layout.hAlign", v) end,
    GetChildVerticalAlignment = function(id) return swcui.get(id, "layout.vAlign") or 0 end,
    SetChildVerticalAlignment = function(id, v) swcui.set(id, "layout.vAlign", v) end,
    GetIgnoreDefaultLayoutCells = function(id) return swcui.get(id, "layout.ignoreDefaultCells") end,
    SetIgnoreDefaultLayoutCells = function(id, v) swcui.set(id, "layout.ignoreDefaultCells", v) end,
  }
  for k, v in pairs(extra or {}) do
    out[k] = v
  end
  return out
end
Swc.Define("UiLayoutRowBus", "Event", line_layout())
Swc.Define("UiLayoutColumnBus", "Event", line_layout())
Swc.Define("UiLayoutGridBus", "Event", line_layout({
  GetSpacing = function(id) return V2(swcui.get(id, "layout.spacing")) end,
  SetSpacing = function(id, v) swcui.set(id, "layout.spacing", v.x, v.y) end,
  GetCellSize = function(id) return V2(swcui.get(id, "layout.cellSize")) end,
  SetCellSize = function(id, v) swcui.set(id, "layout.cellSize", v.x, v.y) end,
  GetHorizontalOrder = function(id) return swcui.get(id, "layout.horizontalOrder") or 0 end,
  SetHorizontalOrder = function(id, v) swcui.set(id, "layout.horizontalOrder", v) end,
  GetVerticalOrder = function(id) return swcui.get(id, "layout.order") or 0 end,
  SetVerticalOrder = function(id, v) swcui.set(id, "layout.order", v) end,
  GetStartingDirection = function(id) return swcui.get(id, "layout.startingWith") or 0 end,
  SetStartingDirection = function(id, v) swcui.set(id, "layout.startingWith", v) end,
}))
-- The interactables, buttons, check boxes, radio buttons, sliders, scroll boxes, text inputs, and drop-downs: swc/interactables.lua.
Swc.Define("UiDraggableBus", "Event", {
  IsProxy = function() return false end,
  SetDragState = function(id, v) store(id).dragState = v end,
  GetDragState = function(id) return store(id).dragState or 0 end,
})
Swc.Define("UiDropTargetBus", "Event", {
  SetDropState = function(id, v) store(id).dropState = v end,
  GetDropState = function(id) return store(id).dropState or 0 end,
})
Swc.Define("UiMaskBus", "Event", {
  GetIsMaskingEnabled = function(id) return swcui.get(id, "mask.enabled") end,
  SetIsMaskingEnabled = function(id, v) swcui.set(id, "mask.enabled", v and true or false) end,
})

-- ---------------------------------------------------------------------------------------------------- Entity

-- The class Entity of the scripts (Entity(entityId)): the scripts use only GetName and SetName (cacappearancetab.luac reads the name of
-- the chosen radio button of a list, "Male" or "Female", with Entity(id):GetName(); AST-267: with no class the name was nil, and the
-- change of the body type stopped with the buttons of the appearance step off).
local EntityClass = {}
EntityClass.__index = EntityClass
function EntityClass:GetName() return swcui.name(self.id) end
function EntityClass:SetName(name) swcui.set_name(self.id, name) end
function EntityClass:GetId() return self.id end
function Entity(id)
  return setmetatable({id = id}, EntityClass)
end
