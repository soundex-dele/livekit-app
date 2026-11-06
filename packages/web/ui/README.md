# Shared Web UI

The three Web applications use this DOM/TypeScript layer with the existing Vite
build. Import `style.css` once from the application's stylesheet; no framework,
icon package or remote font service is required.

- `createButton(text, variant)` creates a semantic button (`primary`,
  `secondary`, `danger` or `ghost`).
- `createDialog({title, description, id})` exposes `content`, `actions`, `error`,
  `open(initialFocus)` and `setBusy()`. The caller owns form submission and
  request state. Dialogs contain Tab focus, accept Escape when idle and restore
  focus to their trigger. Append them to the document before calling `open()`.
- `confirmAction(title, description, confirmText)` resolves a boolean. Cancel is
  initially focused; closing or Escape resolves `false`.
- `emptyState(title, description)` and `observeStatus(element, classify)` keep
  display state separate from the application's media/session state.
- Common `.av-card`, `.av-page-header`, `.av-topbar`, `.av-nav`, `.av-alert` and
  `.av-badge` classes use shared semantic CSS variables. An explicit
  `data-theme="dark"` region supplies the dark palette; this is not a user theme
  setting. Feedback uses `data-tone="info|success|warning|error|neutral"`.

`mountAccountPanel()` in the adjacent `control.ts` owns the existing account and
organization flow. Its optional third argument accepts `registration`, an
existing `trigger` button and `dialogId`; the return value exposes `element`,
`open()` and `refresh()`. Registration remains opt-in. Initial account discovery
and account/organization changes invoke its change callback; opening or cancelling
the dialog does not end an active session. Remote assistance continues to use a connection code
without requiring this account panel.

Each application's `npm run test:ui` runs Playwright headlessly against its built
`dist` and mocks only the business API. The live-studio suite also exercises the
shared component source in light/dark themes at 1×/2× pixel density. It checks
contrast, modal focus containment/restoration and safe cancellation. Screenshots
are written to `livekit-app/out/ui-refresh/web/`. Video samples are synthetic;
these checks do not assert real device or end-to-end media acceptance.
