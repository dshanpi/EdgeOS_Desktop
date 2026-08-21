---
name: dshanpi-edgeos-ui-design
description: Design, implement, review, or refine the DshanPI EdgeOS Desktop UI for the CanMV-K230 V3 LVGL application. Use for desktop layout, application pages, settings, dialogs, touch interaction, navigation, localization, CJK fonts, camera/media UI, UART or Cloud Model screens, OTA states, and UI consistency or defect reviews in dshanpi_aimodel.
---

# DshanPI EdgeOS UI Design

Use this skill to keep UI work consistent with the product language and runtime constraints already established by DshanPI EdgeOS Desktop.

## Establish context

1. Locate the repository root by walking upward from this skill until `apps/main.c`, `middleware/`, and `system/` are present.
2. Inspect the existing page and its neighboring page patterns before changing code.
3. Treat 640 × 480 landscape touch as the default target unless the user explicitly requests another display.
4. Identify whether the task is a desktop, settings, tool, camera/media, long-running operation, or external-app launch flow.
5. Preserve the dependency direction `apps -> middleware -> system`; keep LVGL out of `middleware/` and `system/`.

Read [design-philosophy.md](references/design-philosophy.md) before proposing a new page, application family, or visual direction.

Read [visual-system-and-components.md](references/visual-system-and-components.md) when creating or restyling layouts, cards, buttons, headers, dialogs, status indicators, or desktop icons.

Read [interaction-and-lifecycle.md](references/interaction-and-lifecycle.md) when changing navigation, scrolling, dropdowns, timers, workers, camera/media resources, overlays, or asynchronous operations.

Read [localization-and-validation.md](references/localization-and-validation.md) when adding visible text, changing language behavior, updating fonts, reviewing accessibility, or preparing a release.

## Follow the implementation workflow

### 1. Define the user outcome

- State the primary action, secondary action, exit path, success state, empty state, busy state, and recoverable failure state.
- Keep the most important action visible without scrolling when practical.
- Expose only implemented applications. Do not add placeholder desktop entries.

### 2. Select an established page pattern

- Desktop: status bar plus vertically scrollable three-column app grid.
- Settings: master navigation plus focused detail page, with a reliable back path.
- Tool: compact header, working area, controls, and persistent status.
- Camera/media: content-first full screen with overlay controls and explicit resource teardown.
- Long operation: guarded action, progress, precise failure detail, and retry or exit.
- External app: save required desktop state, display a launch overlay, transfer resources, and restore cleanly on return.

Do not create a new navigation model when one of these patterns fits.

### 3. Build for touch

- Make interactive targets at least 44 × 44 px; prefer 48–56 px for back and destructive controls.
- Expand top-left back-button hit areas to include the display edges.
- Use `LV_EVENT_CLICKED` for normal actions. Use `LV_EVENT_PRESSED` only when immediate capture or release suppression is required and document why.
- Add the existing tap-movement guard to buttons inside scrollable containers.
- Disable click/focus behavior on decorative child objects so the parent receives the gesture.
- Provide visible pressed, selected, disabled, loading, success, and error feedback.

### 4. Manage overlays and resources explicitly

- Prefer pre-created full-screen overlays that are hidden and shown over repeated delete/recreate cycles.
- Before hiding a view, close screen-level popups, stop timers/workers, release media or UART resources, dismiss dialogs, and handle the active input release.
- Explicitly close every open LVGL dropdown before hiding its owning page. A dropdown list is attached to the screen, not the hidden page.
- Reset transient state on entry so interrupted or historical state cannot leak into the next session.
- Keep one owner for each camera, display, encoder, UART, and long-running OTA operation.

### 5. Localize all visible states

- Support Simplified Chinese, Traditional Chinese, English, and Japanese in the existing enum order.
- Route every visible string through the page's localization helper or language table, including errors, placeholders, dropdown choices, dialogs, and Toast messages.
- Use Montserrat for English and the generated CJK font for Chinese and Japanese unless a component explicitly needs another font.
- Regenerate the CJK subset after adding any non-ASCII character; never assume the source font alone makes a glyph available in firmware.
- Design against the longest translation and test all four languages on the real 640 × 480 layout.

### 6. Validate proportionally

- Compile changed C/C++ with the project's `-Wall -Wextra -Werror` configuration.
- Exercise entry, primary action, cancellation, failure, return, and re-entry.
- Test scroll-versus-tap behavior and edge hit targets on hardware.
- Open each dropdown and return immediately; confirm no popup remains on the desktop.
- Repeat camera/media/tool entry and exit to detect resource leaks or stale overlays.
- Check all four languages for missing glyph boxes, truncation, overlap, and inconsistent font selection.
- For release work, validate the complete image on DshanPI CanMV-K230 V3 hardware.

## Review priorities

Report issues in this order:

1. Blocked navigation, unsafe destructive actions, stale top-layer objects, resource leaks, deadlocks, or crashes.
2. Missing failure recovery, incorrect state feedback, accidental touches, unreadable or missing text.
3. Inconsistent hierarchy, spacing, color, typography, or component behavior.
4. Cosmetic polish that does not affect task completion.

Tie findings to a concrete user journey and source location. Prefer the smallest change that restores the system rule rather than adding page-specific exceptions.

## Preserve these invariants

- The desktop remains usable at 640 × 480 without a mouse or keyboard.
- Every page has a visible and reliable route back.
- Scrolling never launches an item accidentally.
- Hiding a page never leaves a dropdown, dialog, timer, or hardware session active.
- Language changes never produce square glyphs or mixed-language system states.
- Busy and destructive operations cannot be started twice.
- Failures preserve the current bootable system and explain what the user can do next.
- Private keys, passwords, tokens, and device-local configuration never enter UI source or assets.
