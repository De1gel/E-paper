#include "app/CalendarRefreshPlanner.h"

#include "app/RefreshPolicy.h"

namespace appfw {
namespace {

bool bucketChanged(int32_t minute_key, int32_t last_render_minute_key, uint32_t time_refresh_sec) {
  if (time_refresh_sec == 0u || minute_key < 0 || last_render_minute_key < 0) {
    return false;
  }
  return refreshBucketKey(minute_key, time_refresh_sec) !=
         refreshBucketKey(last_render_minute_key, time_refresh_sec);
}

void setSingleDirtyRect(CalendarDirtyRegionSet &dirty, const calendar::Rect &rect,
                        bool full_screen, CalendarDirtyRegionKind kind) {
  dirty = CalendarDirtyRegionSet{};
  if (rect.w == 0 || rect.h == 0) {
    return;
  }
  dirty.full_screen = full_screen;
  dirty.count = 1;
  dirty.kinds[0] = kind;
  dirty.rects[0] = rect;
}

void appendDirtyRect(CalendarDirtyRegionSet &dirty, const calendar::Rect &rect,
                     CalendarDirtyRegionKind kind) {
  if (rect.w == 0 || rect.h == 0 || dirty.count >= CalendarDirtyRegionSet::kMaxRegions) {
    return;
  }
  dirty.kinds[dirty.count] = kind;
  dirty.rects[dirty.count] = rect;
  ++dirty.count;
}

}  // namespace

CalendarRefreshPlan planCalendarRefresh(const CalendarRefreshInputs &inputs) {
  CalendarRefreshPlan plan;

  const bool day_changed =
      inputs.time_valid &&
      inputs.last_day_key != -1 &&
      inputs.day_key != -1 &&
      inputs.day_key != inputs.last_day_key;
  if (inputs.force_full_refresh) {
    plan.mode = CalendarRefreshMode::Full;
    plan.reason = CalendarRefreshReason::ForcedFull;
    setSingleDirtyRect(plan.dirty, inputs.full_screen_rect, true,
                       CalendarDirtyRegionKind::FullScreen);
    return plan;
  }
  if (day_changed) {
    plan.mode = CalendarRefreshMode::Full;
    plan.reason = CalendarRefreshReason::DayChanged;
    setSingleDirtyRect(plan.dirty, inputs.full_screen_rect, true,
                       CalendarDirtyRegionKind::FullScreen);
    return plan;
  }
  if (inputs.partial_before_full > 0u &&
      inputs.partial_refresh_count >= inputs.partial_before_full) {
    plan.mode = CalendarRefreshMode::Full;
    plan.reason = CalendarRefreshReason::PartialBudgetExceeded;
    setSingleDirtyRect(plan.dirty, inputs.full_screen_rect, true,
                       CalendarDirtyRegionKind::FullScreen);
    return plan;
  }

  const bool time_only_refresh =
      inputs.time_valid &&
      inputs.minute_key != -1 &&
      inputs.minute_key != inputs.last_render_minute_key &&
      bucketChanged(inputs.minute_key, inputs.last_render_minute_key, inputs.time_refresh_sec);
  const bool time_only_header_change =
      !inputs.body_changed &&
      inputs.header_time_changed &&
      !inputs.header_weather_changed &&
      !inputs.header_sensors_changed;
  if (time_only_header_change) {
    plan.mode = CalendarRefreshMode::Partial;
    plan.reason = time_only_refresh ? CalendarRefreshReason::TimeTick
                                    : CalendarRefreshReason::PartialCompatibilityPath;
    plan.time_only_refresh = time_only_refresh;
    plan.dirty = CalendarDirtyRegionSet{};
    appendDirtyRect(plan.dirty, inputs.header_time_rect, CalendarDirtyRegionKind::HeaderTime);
    if (plan.dirty.count == 0u) {
      setSingleDirtyRect(plan.dirty, inputs.full_screen_rect, true,
                         CalendarDirtyRegionKind::FullScreen);
      plan.reason = CalendarRefreshReason::PartialCompatibilityPath;
    }
    return plan;
  }

  plan.mode = CalendarRefreshMode::Partial;
  plan.reason = CalendarRefreshReason::PartialCompatibilityPath;
  setSingleDirtyRect(plan.dirty, inputs.full_screen_rect, true,
                     CalendarDirtyRegionKind::FullScreen);
  return plan;
}

const char *calendarRefreshReasonName(CalendarRefreshReason reason) {
  switch (reason) {
    case CalendarRefreshReason::ForcedFull:
      return "forced_full";
    case CalendarRefreshReason::DayChanged:
      return "day_changed";
    case CalendarRefreshReason::PartialBudgetExceeded:
      return "partial_budget_exceeded";
    case CalendarRefreshReason::TimeTick:
      return "time_tick";
    case CalendarRefreshReason::HeaderFieldsChanged:
      return "header_fields_changed";
    case CalendarRefreshReason::PartialCompatibilityPath:
      return "partial_compat";
    default:
      return "none";
  }
}

}  // namespace appfw
