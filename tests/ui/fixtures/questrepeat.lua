-- The quest repeat fixture (stage Q8, Linear AST-243; docs/QUESTS.md "The repeat"): a Repeatable row of the data (0699_0102_Barkimedes2_v2,
-- Daily, completion achievement 0699_0102_Barkimedes2_done) and the schedules of quests.json. Backend 1.8d keeps the completion achievement
-- to the start of the next cycle: with it set, the classifier answers OnCooldown 8 (javelin.enable-repeatable-quest-cooldowns on), the
-- result has the dialogue ObjectiveOnCooldownResponse and no accept option, and a non-repeatable row is completedBefore. The schedule bus:
-- the next start of Hourly, Daily (05:00 UTC), and Weekly (Tuesday 05:00 UTC), the description key, a window of dates.
-- Needs gamedata/quests.json with "schedules" (stage Q8).
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(60, 0)
frames(0.3)

local C = Swc.conversation
local Q = Swc.quests
local D = Q.D()
check(D.schedules and D.schedules.daily ~= nil, "quests.json has the schedules")
local row = D.objectives["0699_0102_barkimedes2_v2"]
check(row ~= nil, "the repeatable row 0699_0102_Barkimedes2_v2")
local function bits(...)
  local out = {}
  for _, id in ipairs({...}) do
    local index = Swc.AchievementIndex(id)
    if index then out[#out + 1] = tostring(index) end
  end
  Swc.HostOwnership(table.concat(out, ","), "", true)
end

bits()
equal(C.unavailable(row), nil, "no completion achievement: offered")
equal(C.classify(row, "", false), C.RESPONSE.proposal, "the classifier: Proposal 3")
bits(row.cells.AchievementId)
equal(C.unavailable(row), "onCooldown", "the completion achievement of a repeat: onCooldown")
equal(C.classify(row, "", false), C.RESPONSE.cooldown, "the classifier: OnCooldown 8")
Swc.config["javelin.enable-repeatable-quest-cooldowns"] = false
equal(C.unavailable(row), "completedBefore", "with the cooldowns off: completedBefore (the row is hidden)")
Swc.config["javelin.enable-repeatable-quest-cooldowns"] = true
local plain = D.objectives["06_getoffbeach"]
bits(plain.cells.AchievementId)
equal(C.unavailable(plain), "completedBefore", "a row that is not repeatable: completedBefore")

-- The conversation result of a state that offers the row: the cooldown dialogue, no option of the row.
bits(row.cells.AchievementId)
-- The state 0699_03_Barkimedes_v2 of conversations.json offers the row.
C.stateId, C.npcId, C.topics = "0699_03_Barkimedes_v2", "", {}
C.build_result()
equal(C.result.responseType, C.RESPONSE.cooldown, "the result: OnCooldown 8")
equal(C.result.dialogue, row.cells.ObjectiveOnCooldownResponse, "the result: ObjectiveOnCooldownResponse")
equal(#C.result.options, 1, "the result: Leave only (no details, no accept)")

-- The objective data of the journal: repeatable and the schedule id.
local objectiveData = ObjectivesDataManagerBus.Broadcast.GetObjectiveData(row.crc)
check(objectiveData.repeatable == true, "the objective data: repeatable")
equal(objectiveData.scheduleId, swcui.crc32("daily"), "the objective data: the schedule id (the CRC-32 of daily)")

-- The schedule bus with fixed times (2026-10-27 is a Tuesday).
-- Unix seconds of a UTC time (the civil date rule; the os.time of the runtime takes no date table).
local function at(y, m, d, h, mi)
  y = m <= 2 and y - 1 or y
  local era = math.floor(y / 400)
  local yoe = y - era * 400
  local doy = math.floor((153 * (m + (m > 2 and -3 or 9)) + 2) / 5) + d - 1
  local days = era * 146097 + yoe * 365 + math.floor(yoe / 4) - math.floor(yoe / 100) + doy - 719468
  return days * 86400 + h * 3600 + mi * 60
end
equal(at(2026, 10, 27, 0, 0), 1793059200, "the test clock: 2026-10-27 00:00 UTC")
local tuesday0400, tuesday0600 = at(2026, 10, 27, 4, 0), at(2026, 10, 27, 6, 0)
equal(Q.next_schedule_start("Daily", tuesday0400), at(2026, 10, 27, 5, 0), "Daily before 05:00 UTC: the same day 05:00")
equal(Q.next_schedule_start("Daily", tuesday0600), at(2026, 10, 28, 5, 0), "Daily after 05:00 UTC: the next day 05:00")
equal(Q.next_schedule_start("Weekly", tuesday0400), at(2026, 10, 27, 5, 0), "Weekly on Tuesday before 05:00 UTC: the same Tuesday")
equal(Q.next_schedule_start("Weekly", tuesday0600), at(2026, 11, 3, 5, 0), "Weekly after Tuesday 05:00 UTC: the next Tuesday")
equal(Q.next_schedule_start("Hourly", at(2026, 10, 27, 4, 30)), at(2026, 10, 27, 5, 0), "Hourly: the next full hour")
equal(Q.next_schedule_start(swcui.crc32("daily"), tuesday0400), at(2026, 10, 27, 5, 0), "a schedule id (CRC) works too")
equal(Q.next_schedule_start("EventSpring", tuesday0400), nil, "a window in the past: no next start")
equal(ScheduleDataManagerBus.Broadcast.GetScheduleDescription(swcui.crc32("daily")), "@objective_daily_description", "the description key")
check(ScheduleDataManagerBus.Broadcast.GetSecondsRemainingUntilNextScheduleStart(swcui.crc32("hourly")) <= 3600, "the seconds to the next hour")
check(not ScheduleDataManagerBus.Broadcast.IsKeyInScheduleNow("EventSpring"), "a past window is not now")
check(not ScheduleDataManagerBus.Broadcast.IsKeyInScheduleNow("LegacyOfCrassus"), "an unknown key is not now")
