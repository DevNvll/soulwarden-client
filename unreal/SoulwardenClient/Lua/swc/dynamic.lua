-- The dynamic lists of LyShine (O3DE UiDynamicLayoutComponent, UiDynamicScrollBoxComponent: facts only):
-- * the dynamic layout keeps its first child as a prototype, makes the number of copies that the script asks for, and
--   gives itself the size of its layout for them;
-- * the dynamic scroll box shows a long list with a few elements: it asks the scripts for the number of elements (and the
--   sections, and the sizes of the elements that vary), sizes its content for all of them, and puts copies of its
--   prototype only at the visible places; each copy that becomes visible for an index goes to the scripts
--   (UiDynamicScrollBoxElementNotificationBus OnElementBecomingVisible). The elements go back to a pool when they leave
--   the view.

local Swc = Swc

local function valid(id)
  return id ~= nil and type(id) == "userdata" and id:IsValid() and swcui.exists(id)
end

-- The prototypes leave the tree of their list (LyShine RemoveChild): they wait under a hidden holder of the canvas.
local function holder_of(canvas)
  Swc.prototypeHolders = Swc.prototypeHolders or {}
  local k = tostring(canvas)
  local h = Swc.prototypeHolders[k]
  if not valid(h) then
    h = swcui.create("Prototypes", swcui.canvas_get(canvas, "root"), nil)
    swcui.set_enabled(h, false)
    Swc.prototypeHolders[k] = h
  end
  return h
end

--- @return The first answer of a handler of a bus for an id (a data bus that the scripts implement), or the default.
local function ask(bus, id, method, default, ...)
  for _, h in ipairs(Swc.Handlers(bus, id)) do
    local fn = h.table[method]
    if type(fn) == "function" then
      local ok, value = Swc.Call(fn, h.table, ...)
      if ok and value ~= nil then
        return value
      end
    end
  end
  return default
end

local function notify(id, event, ...)
  Swc.Notify("UiDynamicScrollBoxElementNotificationBus", id, event, ...)
end

-- --------------------------------------------------------------------------------------------- the dynamic layout

local function dynamic_layout(id)
  local s = Swc.IState(id)
  return s.dynamicLayout
end

local function resize_layout(id)
  local d = dynamic_layout(id)
  if not d or not d.prototype then
    return
  end
  -- A parent layout that controls the element sizes it (O3DE: no resize then).
  local parent = swcui.parent(id)
  if valid(parent) and swcui.get(parent, "layout.kind") then
    return
  end
  local n = swcui.num_children(id)
  local w, h = swcui.size_to_fit(id, d.prototypeSize[1], d.prototypeSize[2], n)
  local l, t, r, b = swcui.get(id, "transform.offsets")
  local px, py = swcui.get(id, "transform.pivot")
  local cl, ct, cr, cb = swcui.rect(id)
  local dw, dh = w - (cr - cl), h - (cb - ct)
  if math.abs(dw) > 0.05 or math.abs(dh) > 0.05 then
    swcui.set(id, "transform.offsets", l - dw * px, t - dh * py, r + dw * (1 - px), b + dh * (1 - py))
  end
end

--- UiDynamicLayoutBus SetNumChildElements: copies of the prototype to the count, or the last children go.
function Swc.DynamicLayoutSetCount(id, count)
  local d = dynamic_layout(id)
  if not d or not d.prototype then
    return
  end
  local n = swcui.num_children(id)
  if n < count then
    swcui.set_enabled(d.prototype, true)
    for _ = 1, count - n do
      Swc.CloneElement(d.prototype, id, nil)
    end
    swcui.set_enabled(d.prototype, false)
  elseif n > count then
    local children = swcui.children(id)
    for i = n, count + 1, -1 do
      Swc.DestroyElement(children[i])
    end
  end
  resize_layout(id)
end

function Swc.InitDynamicLayout(id)
  local d = dynamic_layout(id)
  local children = swcui.children(id)
  if #children > 0 then
    d.prototype = children[1]
    local l, t, r, b = swcui.rect(d.prototype)
    d.prototypeSize = {r - l, b - t}
    for i = #children, 2, -1 do
      Swc.DestroyElement(children[i])
    end
    swcui.reparent(d.prototype, holder_of(swcui.canvas_of(id)), nil)
    swcui.set_enabled(d.prototype, false)
  end
  Swc.DynamicLayoutSetCount(id, d.count or 0)
end

Swc.Define("UiDynamicLayoutBus", "Event", {
  SetNumChildElements = function(id, n) Swc.DynamicLayoutSetCount(id, n) end,
})

-- ------------------------------------------------------------------------------------------ the dynamic scroll box

local function box_state(id)
  return Swc.IState(id).dynamicScrollBox
end

local function content_of(id)
  local b = Swc.IState(id).scrollBox
  return b and b.contentEntity
end

local function axis_size(d, id)
  local l, t, r, b = swcui.rect(id)
  return d.vertical and (b - t) or (r - l)
end

--- O3DE PrepareListForDisplay: the prototypes (the item prototype, else the first child of the content; the header
--- prototype), their sizes along the axis, and an empty content.
local function prepare(id)
  local d = box_state(id)
  if d.prepared then
    return true
  end
  local content = content_of(id)
  if not valid(content) then
    return false
  end
  local box = Swc.IState(id).scrollBox
  d.vertical = box.vertical ~= false
  local children = swcui.children(content)
  d.item = valid(d.prototypeEntity) and d.prototypeEntity or children[1]
  d.header = d.sections and valid(d.headerPrototypeEntity) and d.headerPrototypeEntity or nil
  if not valid(d.item) then
    return false
  end
  d.itemSize = axis_size(d, d.item)
  d.headerSize = d.header and axis_size(d, d.header) or 0
  local holder = holder_of(swcui.canvas_of(id))
  for _, proto in ipairs({d.item, d.header}) do
    if valid(proto) then
      local l, t, r, b = swcui.get(proto, "transform.anchors")
      if d.vertical then
        swcui.set(proto, "transform.anchors", l, 0, r, 0)
      else
        swcui.set(proto, "transform.anchors", 0, t, 0, b)
      end
    end
  end
  for i = #children, 1, -1 do
    local child = children[i]
    if child == d.item or child == d.header then
      swcui.reparent(child, holder, nil)
      swcui.set_enabled(child, false)
    else
      Swc.DestroyElement(child)
    end
  end
  d.displayed = {}   -- element index -> entity
  d.pool = {item = {}, header = {}}
  d.prepared = true
  return true
end

--- The list: for sections, a header then the items of each section. Each entry: kind, section, index in the section.
local function entries(id)
  local d = box_state(id)
  local list = {}
  if d.sections then
    local sections = math.max(1, ask("UiDynamicScrollBoxDataBus", id, "GetNumSections", d.defaultSections or 1))
    for s = 0, sections - 1 do
      list[#list + 1] = {kind = "header", section = s, index = -1}
      local n = ask("UiDynamicScrollBoxDataBus", id, "GetNumElementsInSection", d.defaultCount or 0, s)
      for i = 0, n - 1 do
        list[#list + 1] = {kind = "item", section = s, index = i}
      end
    end
  else
    local n = ask("UiDynamicScrollBoxDataBus", id, "GetNumElements", d.defaultCount or 0)
    for i = 0, n - 1 do
      list[#list + 1] = {kind = "item", section = -1, index = i}
    end
  end
  return list
end

--- The size of an entry along the axis: the prototype size, or the size of the scripts (variable sizes), or the
--- target size of a copy that the scripts prepare (auto size).
local function entry_size(id, e, position)
  local d = box_state(id)
  local variable = e.kind == "item" and d.variableSize or (e.kind == "header" and d.variableHeaderSize)
  local fixed = e.kind == "header" and d.headerSize or d.itemSize
  if not variable then
    return fixed
  end
  local auto = e.kind == "item" and d.autoSize or d.autoHeaderSize
  if auto then
    local proto = e.kind == "header" and d.header or d.item
    swcui.set_enabled(proto, true)
    if e.kind == "header" then
      notify(id, "OnPrepareSectionHeaderForSizeCalculation", proto, e.section)
    elseif e.section >= 0 then
      notify(id, "OnPrepareElementInSectionForSizeCalculation", proto, e.section, e.index)
    else
      notify(id, "OnPrepareElementForSizeCalculation", proto, e.index)
    end
    swcui.compute_layout(proto)
    local size = swcui.get(proto, d.vertical and "cell.defaultTargetHeight" or "cell.defaultTargetWidth")
    swcui.set_enabled(proto, false)
    return size or fixed
  end
  local method
  if e.kind == "header" then
    method = d.vertical and "GetSectionHeaderHeight" or "GetSectionHeaderWidth"
    return ask("UiDynamicScrollBoxDataBus", id, method, fixed, e.section)
  elseif e.section >= 0 then
    method = d.vertical and "GetElementInSectionHeight" or "GetElementInSectionWidth"
    return ask("UiDynamicScrollBoxDataBus", id, method, fixed, e.section, e.index)
  end
  method = d.vertical and "GetElementHeight" or "GetElementWidth"
  return ask("UiDynamicScrollBoxDataBus", id, method, fixed, e.index)
end

local function release_all(id)
  local d = box_state(id)
  for _, e in pairs(d.displayed) do
    local pool = d.pool[e.kind]
    swcui.set_enabled(e.entity, false)
    pool[#pool + 1] = e.entity
  end
  d.displayed = {}
end

local function take(id, kind)
  local d = box_state(id)
  local pool = d.pool[kind]
  if #pool > 0 then
    local entity = table.remove(pool)
    swcui.set_enabled(entity, true)
    return entity
  end
  local proto = kind == "header" and d.header or d.item
  swcui.set_enabled(proto, true)
  local entity = Swc.CloneElement(proto, content_of(id), nil)
  swcui.set_enabled(proto, false)
  return entity
end

--- O3DE UpdateElementVisibility: the entries in the visible area of the content parent get an element; the others go
--- back to the pool. An element that becomes visible for an entry goes to the scripts.
function Swc.DynamicScrollBoxUpdate(id)
  local d = box_state(id)
  if not d or not d.prepared or not d.list then
    return
  end
  local content = content_of(id)
  local parent = valid(content) and swcui.parent(content)
  if not valid(parent) then
    return
  end
  local cl, ct = swcui.rect(content)
  local pl, pt, pr, pb = swcui.rect(parent)
  local start = d.vertical and (pt - ct) or (pl - cl)
  local finish = start + (d.vertical and (pb - pt) or (pr - pl))
  local wanted = {}
  for i, e in ipairs(d.list) do
    if d.offsets[i] + d.sizes[i] > start and d.offsets[i] < finish then
      wanted[i] = e
    end
  end
  for i, shown in pairs(d.displayed) do
    if not wanted[i] then
      swcui.set_enabled(shown.entity, false)
      local pool = d.pool[shown.kind]
      pool[#pool + 1] = shown.entity
      d.displayed[i] = nil
    end
  end
  for i, e in pairs(wanted) do
    if not d.displayed[i] then
      local entity = take(id, e.kind)
      if valid(entity) then
        d.displayed[i] = {entity = entity, kind = e.kind}
        local l, t, r, b = swcui.get(entity, "transform.offsets")
        if d.vertical then
          swcui.set(entity, "transform.offsets", l, d.offsets[i], r, d.offsets[i] + d.sizes[i])
        else
          swcui.set(entity, "transform.offsets", d.offsets[i], t, d.offsets[i] + d.sizes[i], b)
        end
        if e.kind == "header" then
          notify(id, "OnSectionHeaderBecomingVisible", entity, e.section)
        elseif e.section >= 0 then
          notify(id, "OnElementInSectionBecomingVisible", entity, e.section, e.index)
        else
          notify(id, "OnElementBecomingVisible", entity, e.index)
        end
      end
    end
  end
end

--- UiDynamicScrollBoxBus RefreshContent: the list again from the scripts, the content size, the visible elements.
function Swc.DynamicScrollBoxRefresh(id)
  local d = box_state(id)
  if not d or not prepare(id) then
    return
  end
  release_all(id)
  d.list = entries(id)
  d.offsets, d.sizes = {}, {}
  local total = 0
  for i, e in ipairs(d.list) do
    d.offsets[i] = total
    d.sizes[i] = entry_size(id, e)
    total = total + d.sizes[i]
  end
  -- The content takes the total size along the axis (its pivot keeps its place).
  local content = content_of(id)
  local l, t, r, b = swcui.get(content, "transform.offsets")
  local px, py = swcui.get(content, "transform.pivot")
  local cl, ct, cr, cb = swcui.rect(content)
  if d.vertical then
    local diff = total - (cb - ct)
    swcui.set(content, "transform.offsets", l, t - diff * py, r, b + diff * (1 - py))
  else
    local diff = total - (cr - cl)
    swcui.set(content, "transform.offsets", l - diff * px, t, r + diff * (1 - px), b)
  end
  -- The scroll box keeps its content over its parent.
  local box = Swc.IState(id).scrollBox
  Swc.ScrollBoxSetOffset(id, Swc.Vector2.new({x = box.offset[1], y = box.offset[2]}))
  Swc.DynamicScrollBoxUpdate(id)
end

local function child_index(id, child)
  local d = box_state(id)
  for i, shown in pairs(d and d.displayed or {}) do
    if shown.entity == child then
      return i, d.list[i]
    end
  end
  return nil
end

Swc.Define("UiDynamicScrollBoxBus", "Event", {
  RefreshContent = function(id) Swc.DynamicScrollBoxRefresh(id) end,
  AddElementsToEnd = function(id, n, scrollToEnd)
    Swc.DynamicScrollBoxRefresh(id)
    if scrollToEnd then
      UiScrollBoxBus.Event.SetScrollOffsetY(id, -1e9)
    end
  end,
  RemoveElementsFromFront = function(id) Swc.DynamicScrollBoxRefresh(id) end,
  InsertElementsIntoSection = function(id) Swc.DynamicScrollBoxRefresh(id) end,
  RemoveElementsFromSection = function(id) Swc.DynamicScrollBoxRefresh(id) end,
  RemoveElementAtIndexFromSection = function(id) Swc.DynamicScrollBoxRefresh(id) end,
  ScrollToEnd = function(id)
    local d = box_state(id)
    if d and d.vertical == false then
      UiScrollBoxBus.Event.SetScrollOffsetX(id, -1e9)
    else
      UiScrollBoxBus.Event.SetScrollOffsetY(id, -1e9)
    end
  end,
  GetElementIndexOfChild = function(id, child)
    local _, e = child_index(id, child)
    return e and e.index or -1
  end,
  GetSectionIndexOfChild = function(id, child)
    local _, e = child_index(id, child)
    return e and e.section or -1
  end,
  GetChildAtElementIndex = function(id, index)
    local d = box_state(id)
    for i, shown in pairs(d and d.displayed or {}) do
      local e = d.list[i]
      if e.kind == "item" and e.section < 0 and e.index == index then
        return shown.entity
      end
    end
    return EntityId()
  end,
  GetChildAtSectionAndElementIndex = function(id, section, index)
    local d = box_state(id)
    for i, shown in pairs(d and d.displayed or {}) do
      local e = d.list[i]
      if e.kind == "item" and e.section == section and e.index == index then
        return shown.entity
      end
    end
    return EntityId()
  end,
  GetEntityIdAtElementIndexInSection = function(id, section, index)
    return UiDynamicScrollBoxBus.Event.GetChildAtSectionAndElementIndex(id, section, index)
  end,
  GetSectionHeaderEntityId = function(id, section)
    local d = box_state(id)
    for i, shown in pairs(d and d.displayed or {}) do
      local e = d.list[i]
      if e.kind == "header" and e.section == section then
        return shown.entity
      end
    end
    return EntityId()
  end,
  IsElementIndexInSectionVisible = function(id, section, index)
    return UiDynamicScrollBoxBus.Event.GetChildAtSectionAndElementIndex(id, section, index):IsValid()
  end,
  GetPrototypeElement = function(id)
    -- Before the first refresh: the prototype property, else the first child of the content (as O3DE).
    local d = box_state(id)
    if not d then
      return EntityId()
    end
    if d.item then
      return d.item
    end
    if valid(d.prototypeEntity) then
      return d.prototypeEntity
    end
    local content = content_of(id)
    return valid(content) and swcui.children(content)[1] or EntityId()
  end,
  GetVariableSizeElementOffset = function(id, index)
    local d = box_state(id)
    for i, e in ipairs(d and d.list or {}) do
      if e.kind == "item" and e.index == index then
        return d.offsets[i]
      end
    end
    return 0
  end,
})

--- The component part of InGamePostActivate: the prototype of a dynamic layout; the auto refresh of a dynamic scroll box.
function Swc.InitDynamic(id)
  local s = Swc.IState(id)
  if s.dynamicLayout then
    Swc.InitDynamicLayout(id)
  end
  if s.dynamicScrollBox and s.dynamicScrollBox.autoRefresh then
    Swc.DynamicScrollBoxRefresh(id)
  end
end

Swc.Handled("UiDynamicScrollBoxElementNotificationBus")
