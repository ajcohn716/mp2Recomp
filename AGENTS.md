# Repository workflow

- Before editing files, create a task-specific branch using the `codex/` prefix.
  Do not work directly on `main`. Reuse an existing task branch when continuing
  the same work; use a separate branch for unrelated work.
- Commit frequently as you complete small, coherent changes. Do not leave all
  changes for a single commit at the end of a long session. Run checks appropriate
  to each change before committing, and use descriptive commit messages.
- Stage only files belonging to your task. Preserve unrelated changes and never
  include ignored ROMs, generated game code, assets, saves, or build output.
- At handoff, report the branch, commits made, checks run, and any remaining
  uncommitted work. Push or merge only when authorized by the user.
