#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/.pio/logic-tests"
mkdir -p "${build_dir}"

g++ -std=c++17 -Wall -Wextra -pedantic \
  -I"${repo_root}/test/support" \
  -I"${repo_root}/src" \
  "${repo_root}/src/app/RefreshPolicy.cpp" \
  "${repo_root}/src/app/CalendarRefreshPlanner.cpp" \
  "${repo_root}/src/display/PartialRefreshGeometry.cpp" \
  "${repo_root}/src/calendar/CalendarLogic.cpp" \
  "${repo_root}/src/system/CalendarStore.cpp" \
  "${repo_root}/src/system/CalendarSyncService.cpp" \
  "${repo_root}/src/system/CalendarSettings.cpp" \
  "${repo_root}/src/system/CalendarEventNormalize.cpp" \
  "${repo_root}/src/system/CalendarIcsCore.cpp" \
  "${repo_root}/test/logic/test_logic.cpp" \
  -o "${build_dir}/logic_tests"

"${build_dir}/logic_tests"
