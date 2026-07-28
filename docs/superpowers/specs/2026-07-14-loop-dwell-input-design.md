# Loop Result Dwell Numeric Input Design

> 历史设计：记录 2026-07-14 循环停留时间输入框决策，正文保留英文原始语义；当前循环执行规则见 `../../TASK_GROUPS.md`。


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
