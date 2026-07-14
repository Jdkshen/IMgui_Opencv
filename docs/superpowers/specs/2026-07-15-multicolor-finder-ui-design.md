# MultiColorFinder UI Optimization Design

## Scope

Optimize only the MultiColorFinder tool card in `UI/ToolsWindow.cpp`. The recognition algorithm, `ColorPoint` structure, recipe schema, reference-image capture flow, ROI behavior, and execution semantics remain unchanged.

## Goals

- Keep the reference preview readable with dozens of color points.
- Make one point easy to locate and edit without scrolling through an unbounded list.
- Reduce per-frame ImGui work for large point collections.
- Preserve compatibility with existing recipes and tool instances.

## Interaction Design

### Reference Preview

- Keep the existing fixed-size reference preview and click-to-add behavior.
- Draw every point as a compact cross marker.
- Draw a numeric label only for the selected point or the point currently hovered in the preview.
- Highlight the selected point with a distinct outline and color.
- Clicking an existing marker selects it instead of adding a duplicate point at that location. Clicking elsewhere continues to add a new point and selects it.
- The tooltip reports the selected point number, relative offset, BGR value, and tolerance when hovering a marker.

### Point Controls

- Keep the point count and unified tolerance control above the list.
- Replace the vertically expanding controls with a fixed-height scrollable ImGui table.
- Use columns for selection/number, color swatch, relative offset, BGR value, tolerance, and delete.
- Use a compact numeric tolerance editor rather than a full-width slider for every row.
- Clicking a row selects that point and highlights the matching preview marker.
- Use `ImGuiListClipper` so only visible rows are submitted when the point count is large.
- Provide `Select all` only where it has a concrete operation; this version keeps `Clear all` and does not add unused multi-selection state.

## State And Data Flow

- Store only a selected point index per tool instance as transient UI state in `ToolsWindow.cpp`.
- Clamp or clear the selected index whenever points are reset, cleared, removed, or replaced.
- Point edits continue to mutate `MultiColorFinder::points` directly and request the existing live rerun.
- Deleting the anchor point retains the current established vector behavior; this UI change does not redefine anchor coordinates or rewrite remaining offsets.
- No UI selection state is serialized. Existing recipe save/load remains byte-compatible at the schema level.

## Edge Cases

- Empty point list: show the existing capture guidance and no table.
- One point: identify it as the anchor and keep selection valid.
- Deleted selected point: select the nearest remaining row, or clear selection when the list becomes empty.
- Dense or overlapping points: choose the nearest marker within a small screen-space hit radius.
- Narrow tool panel: keep stable column widths for swatch and actions; allow offset/BGR text to clip with a tooltip rather than overlap adjacent controls.

## Verification

- Add points through the preview and verify the new point becomes selected.
- Select a row and verify only its preview label is shown and highlighted.
- Select a preview marker and verify the matching row becomes selected and visible.
- Edit unified and per-point tolerance and verify the existing rerun request still occurs.
- Delete first, middle, last, and selected points without stale indexing or crashes.
- Load an existing MultiColorFinder recipe and verify all points, colors, offsets, and tolerances are unchanged.
- Run the Release x64 build and regression test executable.
