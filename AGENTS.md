# Repository guidance for coding agents

## Use CodeGraph when available

Before manually walking the source tree, check whether the `codegraph` CLI and a
project index are available:

```bash
codegraph status .
```

If the index exists but is stale, update it before relying on the results:

```bash
codegraph sync .
```

Use CodeGraph as the first source-code discovery tool for repository summaries,
architecture questions, symbol lookup, call relationships, change impact, and test
selection. Useful starting commands are:

```bash
codegraph files -p . --format grouped
codegraph explore -p . --max-files 12 "<area or behaviour to understand>"
codegraph query -p . "<symbol>"
codegraph node -p . "<symbol or file>"
codegraph callers -p . "<symbol>"
codegraph callees -p . "<symbol>"
codegraph impact -p . "<symbol>"
```

CodeGraph complements rather than replaces direct inspection:

- Read the source returned by CodeGraph and verify important claims against current
  code.
- Read `README.md`, `docs/STATUS.md`, and relevant design/user documentation for
  project intent, current status, hardware evidence, and known gaps. The CodeGraph
  index covers source and configuration, not all prose documentation or generated
  assets.
- Use Git or filesystem tools for tracked-file counts, unindexed files, generated
  output, and working-tree state.
- If CodeGraph is absent or cannot index the repository, fall back to direct source,
  documentation, Git, and filesystem inspection; do not block the task on it.

## Python Scripting on Windows: The CP-1252 Trap

**Always** explicitly declare `encoding='utf-8'` when reading or writing source code and text files in Python scripts.

Python's built-in `open()` function on Windows defaults to the system's locale encoding (typically `cp1252`), not `utf-8`. If a UTF-8 file containing multibyte characters is read or written without explicitly specifying the encoding, it causes double-encoding corruption (e.g., `×` becomes `Ã—`, `—` becomes `â€”`).

**Correct Usage:**
`with open('file.cpp', 'w', encoding='utf-8') as f:`

**Incorrect Usage:**
`with open('file.cpp', 'w') as f:`
