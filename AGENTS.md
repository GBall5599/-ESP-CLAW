# Repository Guidelines

## Project Structure & Module Organization

This repository ports Espressif `esp-claw` to the LCKFB ESP32-S3 handheld board. Keep active work in `esp-claw/`, especially `esp-claw/application/basic_demo/` for the runnable demo and `esp-claw/components/` for reusable capabilities and Lua modules. `14-handheld/` is the original board example, and `14-handheld(new)/` is the ESP-IDF 5.5.x adapted reference. `Ref/` stores upstream reference code and should be treated as read-only unless explicitly syncing upstream. Generated outputs belong in `build/` and must not be edited by hand.

## Build, Test, and Development Commands

Use an exported ESP-IDF environment before running project commands.

- `cd esp-claw/application/basic_demo` enters the main demo application.
- `idf.py gen-bmgr-config -c ./boards -b <board_name>` generates board-manager files; check `boards/` for valid names.
- `idf.py menuconfig` edits Wi-Fi, LLM, IM, timezone, and capability defaults.
- `idf.py build` compiles the current application.
- `idf.py flash monitor` flashes the connected ESP32-S3 and opens serial logs.

For handheld reference validation, run the same `idf.py build` flow inside `14-handheld(new)/`.

## Coding Style & Naming Conventions

Follow ESP-IDF C conventions: 4-space indentation, `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for macros, and `esp_err_t` returns for fallible operations. Keep component APIs in matching `.h` files and implementation in `.c` files. Preserve existing prefixes such as `cap_`, `claw_`, `lua_module_`, and board-specific `esp32_s3_szp`. Prefer small components with clear `CMakeLists.txt` dependencies over cross-directory includes.

## Testing Guidelines

There is no single root test runner. Validate changes with the narrowest ESP-IDF app or component test available, then build `esp-claw/application/basic_demo`. Existing component tests live under paths such as `esp-claw/components/*/test_apps/` and managed component `test/` folders. Name new test apps after the component and scenario, for example `event_router_cli_test`.

## Commit & Pull Request Guidelines

Git history uses Conventional Commits, commonly `feat:` and `fix:` with concise English summaries, e.g. `fix: resolve reboot loop and diagnose LCD display issue`. Keep commits scoped to one behavior or component. Pull requests should include a short summary, target board or app path, ESP-IDF version, validation commands run, and serial log excerpts or screenshots for UI, display, camera, Wi-Fi, or boot changes.

## Security & Configuration Tips

Do not commit Wi-Fi passwords, bot tokens, LLM API keys, or search keys from `menuconfig`, `sdkconfig`, or local notes. Keep machine-specific paths in local IDE settings. Treat `/fatfs` runtime data such as sessions, memory, scripts, and attachments as device-local state unless a change intentionally updates seeded assets.
