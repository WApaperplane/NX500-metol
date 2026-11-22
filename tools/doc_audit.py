import os, re, glob, collections, json

ROOT = '.'
docs = os.path.join(ROOT, 'docs')
cur_dir = os.path.join(docs, 'current')
files = sorted(os.listdir(cur_dir))
files = [f for f in files if f.endswith('.md')]

# 全仓库文本（用于引用统计）
corpus = {}
for p in glob.glob('**/*', recursive=True):
    if not os.path.isfile(p):
        continue
    if p.startswith(('.git', 'node_modules', 'looks/', 'tmp')):
        continue
    if os.path.getsize(p) > 2_000_000:
        continue
    try:
        with open(p, 'r', encoding='utf-8', errors='ignore') as fh:
            corpus[p] = fh.read()
    except Exception:
        pass

readme = corpus.get(os.path.join('docs', 'README.md'), '')
root_readme = corpus.get('README.md', '')
indexed = set(re.findall(r'\(current/([^)\s]+\.md)\)', readme + root_readme))

rows = []
for f in files:
    p = os.path.join(cur_dir, f)
    body = corpus.get(p, '')
    # 被多少其他文件提到（按文件名）
    refs = [k for k, v in corpus.items() if k != p and f in v]
    head = '\n'.join(body.splitlines()[:20])
    flags = []
    if re.search(r'SUPERSEDED|已被取代|已作废|已撤回|过时|不再适用|DEPRECATED', head, re.I):
        flags.append('self-marked')
    if f in indexed:
        flags.append('indexed')
    m = re.search(r'(\d{4}-\d{2}-\d{2})', f)
    rows.append({
        'file': f,
        'date': m.group(1) if m else '',
        'indexed': f in indexed,
        'refs': len(refs),
        'ref_by': refs[:6],
        'flags': flags,
        'size': len(body),
    })

# 断链检查：README 里 index 的链接是否存在
broken = [x for x in indexed if not os.path.exists(os.path.join(cur_dir, x))]

# 同前缀分组（去掉日期）
groups = collections.defaultdict(list)
for r in rows:
    stem = re.sub(r'_\d{4}-\d{2}-\d{2}\.md$', '', r['file'])
    groups[stem].append(r)

print('=== 概览 ===')
print('current 文档数:', len(rows))
print('README 索引条目:', len(indexed), ' 断链:', len(broken))
if broken:
    print('断链文件:', broken)

print()
print('=== 未被 README 索引、且全仓库零引用（孤儿） ===')
orph = [r for r in rows if not r['indexed'] and r['refs'] == 0]
for r in sorted(orph, key=lambda x: x['date']):
    print('  %s  %s' % (r['date'] or '    -     ', r['file']))

print()
print('=== 未被索引但有引用（可能仍有用） ===')
oi = [r for r in rows if not r['indexed'] and r['refs'] > 0]
for r in sorted(oi, key=lambda x: x['date']):
    print('  %s  refs=%-2d %s' % (r['date'] or '    -     ', r['refs'], r['file']))

print()
print('=== 自标过时 ===')
for r in rows:
    if 'self-marked' in r['flags']:
        print('  %s  %s' % (r['date'] or '    -     ', r['file']))

print()
print('=== 同主题多版本 ===')
for stem, items in sorted(groups.items()):
    if len(items) > 1:
        items = sorted(items, key=lambda x: x['date'])
        print('  [%s] %d 份' % (stem, len(items)))
        for r in items:
            tag = []
            if r['indexed']:
                tag.append('indexed')
            if r['refs'] == 0:
                tag.append('orphan')
            print('      %s  %-52s %s' % (r['date'] or '-', r['file'], ' '.join(tag)))

json.dump({'rows': rows, 'broken': broken}, open('/tmp/docaudit.json', 'w'), ensure_ascii=False, indent=1)
