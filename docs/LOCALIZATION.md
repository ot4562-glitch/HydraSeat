# HydraSeat Localization Policy

HydraSeat is English-first internally and multilingual at the user interface and end-user documentation boundary.

## Supported release locales

| Locale | Language | Role |
| --- | --- | --- |
| `en-US` | English | Canonical source language and unconditional fallback |
| `ko-KR` | Korean | Supported release language |
| `zh-CN` | Simplified Chinese | Supported release language |

Additional locales, including Traditional Chinese, may be added later without changing runtime protocols or persisted profile semantics.

## Language selection

- On first run, HydraSeat may map the current Windows UI language to a supported locale.
- Unsupported Windows locales fall back to `en-US`.
- The user can override the locale from the Management Seat control console.
- The selected locale is stored per Windows user.
- Changing language must not restart or alter an active Seat session. Disposable UI/shell clients may reload and relayout.

## Source and protocol language

The following remain English and are never localized:

- source-code comments and developer docstrings;
- protocol message/type names;
- JSON/schema/profile keys;
- CLI switches and machine-readable CLI field names;
- diagnostic/error codes;
- capability/backend/profile/packet identifiers;
- test names and developer-facing implementation notes.

User-facing explanatory text around those stable identifiers may be localized. Diagnostics shown to a user should expose a copyable stable English diagnostic code alongside translated text.

## UI string contract

Shipping user-visible strings use stable message IDs instead of English display text as lookup keys.

Example:

```text
session.start
session.stop_return_to_windows
session.reconfigure
startup.manual
startup.background_idle
startup.auto_activate_validated
recovery.required
recovery.reset
```

Initial neutral resource layout:

```text
resources/i18n/en-US.json
resources/i18n/ko-KR.json
resources/i18n/zh-CN.json
schemas/localization-catalog.schema.json
```

The localization layer must not depend on Qt so the current Win32 UI and a future Qt UI can use the same IDs/catalogs.

Catalog requirements:

- UTF-8;
- explicit schema version and locale;
- bounded message count and message length;
- no duplicate keys;
- valid Unicode;
- named placeholders where substitution is needed;
- required key parity against `en-US`;
- exact placeholder parity against `en-US`;
- deterministic fallback to English when an optional translated message is absent;
- a missing critical English string is a validation/build error.

## Formatting and layout

- Do not concatenate sentence fragments whose grammar depends on English order.
- Prefer named placeholders such as `{seat}`, `{device}`, `{count}`, and `{profile}`.
- Layouts must tolerate longer localized strings without hiding Start, Stop / Return to Windows, Reconfigure, Recovery, or Reset actions.
- Use Windows/system font fallback for Korean and Chinese glyphs; do not bundle private font files merely for glyph coverage.
- Validate at 100%, 125%, 150%, and 200% DPI, including mixed-DPI multi-monitor Management Seats.
- Color is never the only status signal.

## README policy

`README.md` is canonical English and links at the top to:

- `README.ko.md` — Korean;
- `README.zh-CN.md` — Simplified Chinese.

Every localized README links back to all supported README languages. Code blocks, command names, paths, API names, packet IDs, release versions, links, and machine-readable identifiers remain unchanged unless the surrounding prose requires explanation.

When the canonical README changes materially, localized READMEs are updated in the same documentation packet or explicitly marked as temporarily behind. Release validation requires all three versions to agree on:

- product goal and safety/non-goals;
- current implementation status;
- supported locales;
- build/start/test commands;
- roadmap/current packet;
- recovery and compatibility limitations;
- license status;
- release/version links.

## Validation

P7-I18N-01 implements automated catalog validation. P10-UX-01 performs release-level language and documentation acceptance.

Minimum automated checks:

- parse all supported catalogs;
- required message-key parity;
- placeholder parity;
- invalid/future schema rejection;
- unsupported locale fallback;
- locale-setting persistence and corrupt-setting fallback;
- no localization resource changes protocol/schema semantics;
- README language links resolve.

Minimum manual checks:

- Management Seat Start -> Active -> Stop / Return to Windows -> Reconfigure in all three languages;
- recovery/reset workflow in all three languages;
- clean Windows machine Korean/Chinese glyph rendering without custom font install;
- long labels and warnings at mixed DPI and multi-monitor layouts;
- language change while an active session continues unaffected.
