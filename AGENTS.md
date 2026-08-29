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
