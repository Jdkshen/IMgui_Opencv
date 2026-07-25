# Loop Result Dwell Numeric Input Design

> Documentation sync: 2026-07-25. This file has been reviewed against `master` after the `codex/p0-p4-release` merge; see `docs/STATUS_2026-07-25.md` for the consolidated change summary.


## Goal

Make the loop result dwell setting directly editable by keyboard instead of requiring slider dragging.

## UI

- Replace the existing `SliderInt` in `Run > Loop Settings` with `InputInt`.
- Keep the label `结果停留时间` and display milliseconds.
- Provide normal and fast step buttons using 10 ms and 100 ms increments.
- Clamp committed values to the existing `0-3000 ms` range.

## Behavior

- Apply valid edits immediately through `RunSettings::SetLoopResultDwellMs`.
- Save `run_settings.json` when editing is committed or the input loses focus.
- Keep the existing restore-default action and tooltip.
- Do not change loop timing semantics or persistence format.

## Verification

- Build the Release x64 application.
- Run the existing Release regression suite, including run-setting range and persistence tests.
