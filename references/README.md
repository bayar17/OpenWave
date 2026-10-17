# Local reference coding

XERJ v1.0.0-rc.96 is installed at `~/.local/bin/xerj`. Its checksum was verified during installation. The local development service binds only to `127.0.0.1`: Elasticsearch-compatible API on 9200, native REST on 9201, and gRPC on 9202. It uses lexical/BM25 search, with no remote embedding or reranking provider configured. This is a host development tool and is not installed in OpenWave's rootfs or disk image.

Search the project and relevant upstream implementations before editing code:

```bash
python3 scripts/reference-code.py local "navigateBack"
python3 scripts/reference-code.py ref shell "task switcher gesture"
python3 scripts/reference-code.py ref wayland "setMinimized"
python3 scripts/reference-code.py ref apps "history title favicon"
python3 scripts/reference-code.py ref security "file chooser permission"
python3 scripts/reference-code.py ref all "concrete identifier"
```

Results include paths, line excerpts, and pinned upstream commits. Read the surrounding source, assess the differences, and use the approach to write an OpenWave implementation. Retrieval is a starting point, not proof that an upstream behavior applies here. All references are configured as **approach-only**, including mixed-license repositories; do not paste implementations.

| Group | Pinned repositories | Relevant problems |
| --- | --- | --- |
| `shell` | [Plasma Mobile](https://github.com/KDE/plasma-mobile) | Qt Quick phone shell, app overview, gestures, panel transitions |
| `wayland` | [Sailfish Lipstick](https://github.com/sailfishos/lipstick), [Qt Wayland](https://github.com/qt/qtwayland) | Mobile compositor, focus, client surfaces, minimize and lifecycle protocols |
| `apps` | [Angelfish](https://github.com/KDE/angelfish), [MauiKit File Browsing](https://github.com/KDE/mauikit-filebrowsing) | Mobile browser tabs/history/downloads, file models and operations |
| `security` | [xdg-desktop-portal](https://github.com/flatpak/xdg-desktop-portal), [Bubblewrap](https://github.com/containers/bubblewrap) | D-Bus request identity, permissions, file chooser brokering, namespace isolation |

Angelfish uses Qt WebEngine; Ocean uses WPE/WebKit. Plasma Mobile uses KDE services, and Lipstick has its own Sailfish APIs. These references do not replace OpenWave's services, custom init, theme, or IPC contracts. Qt Wayland is a pinned upstream snapshot, not a promise of matching the exported SDK's Qt version.

## State and refresh

```bash
python3 scripts/reference-code.py status
python3 scripts/reference-code.py index project
python3 scripts/reference-code.py index references
python3 scripts/reference-code.py rebuild-project
systemctl --user status openwave-xerj
systemctl --user status openwave-xerj-project
```

The project watcher refreshes indexed source changes with a 1.5-second debounce. `.xerjignore` restricts the project to source, build scripts, patches, and documentation. It excludes binaries, image assets, SDKs, generated output, imported source trees, databases, user data, and XERJ's structured reference manifests. Hidden files are skipped by XERJ. Reference indexing also omits translations and bulky assets.

The helper explicitly searches `text` as well as `body`, `code`, and `defs`, because this XERJ version stores some QML and shell text in chunks that its default code query does not search. It resolves excerpts against the working tree or pinned reference checkout rather than guessing line numbers. If files have changed since indexing, refresh before relying on ranking.

XERJ can refuse an incremental pass when a newly added structured file requires a different inferred schema. Check the watcher journal for failed passes. `rebuild-project` builds a separate index generation, switches the helper's active prefix only after indexing completes, and restarts an already-running watcher. A failed rebuild leaves the previous searchable generation intact. Old generations remain available for recovery; do not use a wildcard across generations when freshness matters. Manual project indexing pauses the watcher to avoid journal conflicts.

Indexes and project journals are in `~/.local/share/openwave-reference/`; reference clones, manifests, build journals, and active-prefix state are in `~/.xerj-code/`. Neither directory belongs in Git or rootfs. The node's 3 GiB process budget and systemd memory limits bound development resource use. Index references sequentially. XERJ exit 3 can mean successful indexing with skipped files: inspect `code_files_indexed`, `code_files_junked`, and any `datasets_refused` in the output. Never assume an interrupted build is searchable.

Manifests in `xerj/*.json` pin the exact repositories and commits and contain license-review notes. `index references` verifies checkout commits and refuses local checkout changes. After 30 days, reindex pinned references to refresh search state. Updating upstream commits is a separate reviewed manifest change; a normal refresh does not pull new HEADs.

## Codex integration

The `xerj` stdio MCP server is registered in `~/.codex/config.toml`, using the absolute executable path and the local endpoint. Reload the Codex session to load newly registered tools. `xerj_code_search` accepts corpus names `openwave-shell`, `openwave-wayland`, `openwave-apps`, and `openwave-security`. For QML, prefer the helper above or `xerj_search` with:

```json
{
  "index": "openwave-*",
  "query": {"multi_match": {"query": "navigateBack", "fields": ["body", "code", "text", "defs"]}},
  "_source": ["ax_path", "body", "code", "text", "start_line", "line"],
  "size": 3,
  "max_tokens": 2000
}
```

The helper reads each reference corpus's active prefix from XERJ state and the project prefix from `~/.local/share/openwave-reference/project-active.json` (initially `openwave`), so it avoids incomplete or obsolete build generations. For MCP after a project rebuild, replace `openwave-*` in the example with that active prefix followed by `-*`. Do not use remote reranking, embedding proxies, sharing, or feedback publishing without user authorization.

## Recreate on another development host

Install XERJ following [its installation documentation](https://xerj.org/docs/install), verifying the release checksum. Then:

```bash
mkdir -p ~/.local/share/openwave-reference ~/.config/systemd/user
cp references/xerj/openwave-xerj.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now openwave-xerj
python3 scripts/reference-code.py index all
codex mcp add xerj -- "$HOME/.local/bin/xerj" mcp --url http://127.0.0.1:9200 --disable-feedback
```

Allow the node to become ready before indexing (`curl -f http://127.0.0.1:9200/`). The host service uses systemd's **user** manager; OpenWave's custom runtime init is unchanged. To enable continuous project indexing, create a user service whose `ExecStart` runs `python3 /absolute/path/to/OpenWave/scripts/reference-code.py watch`, with `After=` and `Requires=openwave-xerj.service`, then enable it. Stop either service with `systemctl --user stop SERVICE` when not needed.
