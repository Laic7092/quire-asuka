# AGENTS.md

This is an Asuka library (a package apps import) (https://asuka.egoist.dev). Before writing Asuka, read the
language for the installed version: `asuka guide`.

- Check: `asuka check --json` (one diagnostic per line, each with a code)
- Look up a code: `asuka explain <code>`
- Test: `asuka test --json`
- A component's body is declarations; state is written in handlers,
  fns, tasks and effects. Lists in views need `key`.
