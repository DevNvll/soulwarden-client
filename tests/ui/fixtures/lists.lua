-- The dynamic list fixture. The original ReusableScoreboard screen (reusablescoreboard.luac, spreadsheet.luac,
-- spreadsheetdatarow.luac) shows the test data of its own script (GetDummyColumnData, GetDummyRowsData) in its dynamic
-- scroll box: the scroll box asks the script for the number of rows, sizes its content for all of them, and puts
-- copies of the row prototype only at the visible places; the script fills each copy that becomes visible. Then the
-- wheel scrolls the list: other rows become visible and the copies move. Pictures: lists-top, lists-scrolled.
local canvas = screen("LyShineUI/ReusableScoreboard/ReusableScoreboard.uicanvas", 60)
local root = swcui.canvas_get(canvas, "root")
local board = script_of(find(root, "ReusableScoreboard"), "reusablescoreboard.luac")
check(board ~= nil, "the screen has its script")
LyShineManagerBus.Broadcast.SetState(Math.CreateCrc32("ReusableScoreboard"))
frames(0.5)
check(swcui.canvas_get(canvas, "enabled"), "the screen shows in its state")

local box = find(root, "SpreadsheetScrollBox")
local content = Swc.IState(box).scrollBox.contentEntity
local function pick(data)
  if type(data) == "table" and data[1] == nil and type(data.OPR) == "table" and data.OPR[1] ~= nil then
    return data.OPR
  end
  return data
end
if board then
  -- OnTransitionIn stops in PopulateTabs (no game mode in this run: its tabs come from the game mode data); the
  -- screen shows itself as at the end of OnTransitionIn.
  Swc.Call(board.SetIsVisible, board, true)
  local columns = pick(board:GetDummyColumnData("OPR"))
  local rows = pick(board:GetDummyRowsData("OPR"))
  -- More rows than the view shows (the test data has a few; the list repeats them).
  local many = {}
  for i = 1, 60 do
    local row = {}
    for k, v in pairs(rows[(i - 1) % #rows + 1]) do
      row[k] = v
    end
    many[i] = row
  end
  board:SetColumnData(columns)
  board:SetRowsData(many)
  frames(0.3)
  local d = Swc.IState(box).dynamicScrollBox
  equal(#d.list, 60, "the scroll box asks the script for the number of rows")
  local _, ct, _, cb = swcui.rect(content)
  near(cb - ct, 60 * d.itemSize, 0.5, "the content has the size of all the rows")
  local shown = 0
  local first = math.huge
  for i, e in pairs(d.displayed) do
    shown = shown + 1
    first = math.min(first, i)
    check(swcui.enabled(e.entity), "a displayed row is on")
  end
  check(shown > 0 and shown < 60, "only the visible rows have a copy (" .. shown .. ")")
  equal(first, 1, "the first visible row is the first row")
  local row = d.displayed[1] and script_of(d.displayed[1].entity, "spreadsheetdatarow.luac")
  check(row ~= nil and row.data == many[1], "the script filled the first copy with the first row")
  picture("lists-top")

  move_to(box)
  for _ = 1, 5 do
    wheel(-120)
  end
  frames(0.2)
  local after = math.huge
  for i in pairs(d.displayed) do
    after = math.min(after, i)
  end
  check(after > 1, "the wheel scrolls: the first visible row is " .. after)
  local copy = d.displayed[after]
  if copy then
    local rowAfter = script_of(copy.entity, "spreadsheetdatarow.luac")
    check(rowAfter ~= nil and rowAfter.data == many[after], "the copy at the new place has its row")
  end
  picture("lists-scrolled")
end
