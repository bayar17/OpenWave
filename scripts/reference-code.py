#!/usr/bin/env python3
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.request
import uuid


ROOT = Path(__file__).resolve().parents[1]
STATE = Path.home() / '.local/share/openwave-reference'
CORPORA = Path.home() / '.xerj-code'
GROUPS = ('shell', 'wayland', 'apps', 'security')
URL = os.environ.get('XERJ_URL', 'http://127.0.0.1:9200').rstrip('/')
XERJ = os.environ.get('XERJ_BIN', str(Path.home() / '.local/bin/xerj'))
ENV = dict(os.environ, XERJ_DISABLE_FEEDBACK='true')


def request(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(URL + path, data=data,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=120) as response:
        return json.load(response)


def run(*args):
    result = subprocess.run([XERJ, *args], env=ENV)
    if result.returncode not in (0, 3):
        raise RuntimeError(f'XERJ failed with exit {result.returncode}')


def project_state():
    path = STATE / 'project-active.json'
    return json.loads(path.read_text()) if path.exists() else {'prefix': 'openwave', 'state': 'project'}


def project_args(state=None):
    state = state or project_state()
    return ['autoindex', str(ROOT), '--url', URL, '--prefix', state['prefix'],
            '--state-dir', str(STATE / state['state']), '--workers', '2', '--bulk-mb', '2',
            '--no-graph', '--no-semantic', '--code-analyzer', 'code',
            '--max-file-mb', '2', '--progress', 'plain']


def reference_state(group):
    path = CORPORA / 'state' / f'openwave-{group}.json'
    state = json.loads(path.read_text())
    indexed = datetime.datetime.fromisoformat(state['indexed_at'].replace('Z', '+00:00'))
    if (datetime.datetime.now(datetime.timezone.utc) - indexed).days >= 30:
        raise RuntimeError(f'{group} index is stale; run index references')
    return state


def index_references():
    for group in GROUPS:
        manifest = ROOT / 'references/xerj' / f'{group}.json'
        name = f'openwave-{group}'
        destination = CORPORA / 'corpora' / name
        if not (destination / 'corpus.json').exists():
            run('corpus', 'add', '--from', str(manifest))
        pinned = json.loads(manifest.read_text())
        for repo in pinned['repos']:
            checkout = destination / repo['repo']
            actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
            if actual != repo['sha']:
                raise RuntimeError(f'{checkout}: HEAD differs from pinned manifest; review before updating')
            if subprocess.check_output(['git', '-C', str(checkout), 'status', '--porcelain'], text=True):
                raise RuntimeError(f'{checkout}: reference checkout has local changes')
        (destination / '.xerjignore').write_text((ROOT / 'references/xerj/ignore').read_text())
        run('corpus', 'index', name, '--url', URL)


def search(query, group, count):
    groups = GROUPS if group == 'all' else (group,)
    indices = project_state()['prefix'] + '-*' if group == 'local' else ','.join(
        reference_state(g)['index_prefix'] + '-*' for g in groups)
    body = {'size': count * 5, 'query': {'multi_match': {
        'query': query, 'fields': ['defs^3', 'body', 'code', 'text', 'title', 'name'],
        'type': 'most_fields'}}, '_source': ['ax_path', 'body', 'text', 'code', 'title', 'start_line', 'line']}
    result = request('/' + indices + '/_search', body)
    if result.get('_shards', {}).get('failed', 0):
        raise RuntimeError('Search returned failed shards')
    seen = set()
    hits = sorted(result['hits']['hits'], key=lambda h:
                  Path(h.get('_source', {}).get('ax_path', '')).suffix in ('.md', '.rst', '.txt'))
    for hit in hits:
        source = hit.get('_source', {})
        relative = source.get('ax_path')
        if not relative or relative in seen:
            continue
        if group == 'local':
            base, provenance = ROOT, 'working tree'
        else:
            matching = next(g for g in groups if hit['_index'].startswith(reference_state(g)['index_prefix'] + '-'))
            base = CORPORA / 'corpora' / f'openwave-{matching}'
            manifest = json.loads((ROOT / 'references/xerj' / f'{matching}.json').read_text())
            repo = next((r for r in manifest['repos'] if relative.startswith(r['repo'] + '/')), None)
            if repo is None:
                continue
            provenance = repo['sha'][:12] + '; approach-only'
        file = (base / relative).resolve()
        if not file.is_relative_to(base.resolve()) or not file.is_file():
            continue
        content = file.read_text(errors='replace')
        lines = content.splitlines()
        excerpt = source.get('body') or source.get('text') or source.get('code') or ''
        offset = source.get('start_line', source.get('line', 1))
        start = max(0, int(offset or 1) - 1)
        if isinstance(excerpt, str) and excerpt:
            position = content.find(excerpt)
            if position >= 0:
                start = content[:position].count('\n')
        expanded = re.sub(r'([a-z])([A-Z])', r'\1 \2', query).replace('_', ' ')
        tokens = re.findall(r'[\w]+', query.lower()) + re.findall(r'[\w]+', expanded.lower())
        candidates = range(start, min(len(lines), start + max(1, len(str(excerpt).splitlines()))))
        def relevance(i):
            matches = sum(t in lines[i].lower() for t in tokens)
            definition = '::' in lines[i] or re.search(r'\b(class|function|def|struct)\b', lines[i])
            return matches * 2 + bool(matches and definition)
        best = max(candidates, key=relevance, default=start)
        begin, end = max(0, best - 4), min(len(lines), best + 15)
        print(f'\n{relative}:{best + 1} [{provenance}]\n{file}')
        for i in range(begin, end):
            print(f'{i + 1:5} {lines[i]}')
        seen.add(relative)
        if len(seen) == count:
            break
    if not seen:
        print('No matching source excerpts. Try concrete symbols or a different reference group.')


def main():
    parser = argparse.ArgumentParser(description='Local OpenWave and pinned reference source search')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('status')
    index = sub.add_parser('index')
    index.add_argument('scope', choices=('project', 'references', 'all'), default='project', nargs='?')
    sub.add_parser('watch')
    sub.add_parser('rebuild-project', help='Build a new schema generation and switch project search after success')
    local = sub.add_parser('local')
    local.add_argument('query')
    local.add_argument('-k', type=int, default=5)
    ref = sub.add_parser('ref')
    ref.add_argument('group', choices=(*GROUPS, 'all'))
    ref.add_argument('query')
    ref.add_argument('-k', type=int, default=5)
    args = parser.parse_args()
    if args.command == 'status':
        print(json.dumps(request('/_cluster/health'), indent=2))
        run('corpus', 'list', '--url', URL)
    elif args.command in ('index', 'rebuild-project'):
        STATE.mkdir(parents=True, exist_ok=True)
        scope = 'project' if args.command == 'rebuild-project' else args.scope
        if scope in ('project', 'all'):
            watching = subprocess.run(['systemctl', '--user', 'is-active', '--quiet',
                                      'openwave-xerj-project'], capture_output=True).returncode == 0
            if watching:
                subprocess.run(['systemctl', '--user', 'stop', 'openwave-xerj-project'], check=True)
            try:
                if args.command == 'rebuild-project':
                    generation = 'project-' + uuid.uuid4().hex[:12]
                    state = {'prefix': 'openwave-' + generation, 'state': generation}
                    run(*project_args(state))
                    temporary = STATE / 'project-active.json.tmp'
                    temporary.write_text(json.dumps(state) + '\n')
                    temporary.replace(STATE / 'project-active.json')
                else:
                    run(*project_args())
            finally:
                if watching:
                    subprocess.run(['systemctl', '--user', 'start', 'openwave-xerj-project'], check=True)
        if scope in ('references', 'all'):
            index_references()
    elif args.command == 'watch':
        run(*project_args(), '--watch', '--debounce', '1500')
    else:
        if not 1 <= args.k <= 20:
            parser.error('-k must be between 1 and 20')
        search(args.query, 'local' if args.command == 'local' else args.group, args.k)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
        sys.exit(f'reference-code: {error}')
