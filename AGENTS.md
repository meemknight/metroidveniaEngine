This is a 2D metroidvania / platformer engine made in C++, with a small-block world and gameplay rendered through gl2d.


General Instructions:

- comment some basic things in the code, when you add a system describe briefly what it does. When you refactor a system update the comments if needed

- Keep the coding style like mine.

- Prefer using existing helpers and patterns already in the project.

- Prefer resetting struct state with `*this = {};` and then setting only non-default overrides; avoid duplicating member default values in init code.

- Keep the code simple and direct. Prefer plain `struct`s, free functions, and public fields.

- Do not introduce OOP-heavy architecture. Do not add `private:` / `protected:` sections, class hierarchies, abstract interfaces, or getter/setter wrappers unless explicitly asked.

- Do not hold pointers to things just for convenience. Prefer passing data by reference into update/draw helpers, or store the data directly if the editor/system should own its own copy.

- Do not casually change gameplay tuning constants. If a task is not specifically about retuning movement/game feel, preserve the current values.

- Camera behavior is sensitive. Do not casually refactor camera code or reorder camera setup.


Coding Guidelines:

- World units use tile units. Solid blocks are `1x1`.
- Keep systems small and mostly struct-based with inline helpers.
- Prefer explicit data flow and obvious state over abstraction.
- Keep gameplay state grouped in small runtime structs (`Gameplay`, `Room`, `Player`, `PhysicalEntity`) instead of spreading it across many layers.
- Use `gl2d::Renderer2D` directly for world rendering.
- Use `renderer.renderLine(...)` for debug/grid lines. Keep the default grid thickness around `0.05f` unless asked otherwise.
- Update the camera before calling `renderer.setCamera(...)`, then draw world objects. Do not reset the camera at the end of the frame.
- When movement feel is being tuned, expose controls in ImGui instead of scattering magic numbers.


Game Architecture (high level):

- `gameLayer/gameLayer.cpp` is the thin runtime shell (renderer setup, window metrics, fullscreen toggle, shader reload, clear, flush).
- `gameLayer/Gameplay.*` owns gameplay state and gameplay update/render.
- `gameLayer/Room.*` stores tile/block data.
- `gameLayer/Physics.*` handles simple struct-based collision and movement.
- `gameLayer/Player.h` stores player runtime state and timers.


Gameplay Loop:

- Update order: input -> player update/jump timers -> physics resolve -> camera update -> `renderer.setCamera(...)` -> draw room -> draw grid -> draw player -> debug UI.
- Render order: set camera first, then draw world objects.
