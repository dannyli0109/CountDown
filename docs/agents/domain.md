# Domain Documentation

Skills like `improve-codebase-architecture` and `diagnose` read this guide to understand the Playdate countdown timer project's domain language and past decisions.

## Layout

This is a **single-context repo**:

- `CONTEXT.md` at repo root — domain language, conventions, key concepts
- `docs/adr/` at repo root — architecture decision records

No per-module or per-package contexts.

## Reading CONTEXT.md

`CONTEXT.md` should define:

- **What is a flip-card?** The split-flap animation model: frame + digit composited together, two-phase flip (fold down 0–0.5, unfold up 0.5–1)
- **Animation loop** — 30 FPS, delta-time driven, progress tracked per digit
- **Asset model** — 72×96 PNG bitmaps for frame + 0–9 glyphs, colon separator
- **Graphics API** — Playdate C SDK; clip regions + scaled bitmaps; white fill for transparent pixels
- **State machine** — Timer states (READY, RUNNING, PAUSED, FINISHED)

## Reading ADRs

Before proposing architecture changes, check `docs/adr/` for past decisions on:

- Why the current two-phase flip was chosen over alternatives
- Why compositing frame + glyph into `DrawCard` (not separate layers)
- Why digit state is tracked per-position, not globally

## Before asking for refactoring

Provide context:

- What's the problem? (e.g., "rendering is brittle", "hard to test state machine")
- What does "better" look like? (e.g., "separate animation from rendering", "extract timer logic")
- Link to relevant domain concepts in `CONTEXT.md` and ADRs in `docs/adr/`
