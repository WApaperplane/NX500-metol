import os, re, glob, json, datetime

cur = 'docs/current'
files = sorted(f for f in os.listdir(cur) if f.endswith('.md'))
readme = open('docs/README.md', encoding='utf-8').read()
root_readme = open('README.md', encoding='utf-8').read()
indexed = set(re.findall(r'\(current/([^)\s]+\.md)\)', readme + root_readme))

def fdate(f):
    m = re.search(r'(\d{4}-\d{2}-\d{2})', f)
    return m.group(1) if m else ''

LO, HI = '2026-10-07', '2026-10-14'
cands = [f for f in files if LO <= fdate(f) <= HI and f not in indexed]
print('候选（%s ~ %s，未索引）: %d 份' % (LO, HI, len(cands)))

# 全仓库非 md 文本，查路径引用
corpus = {}
for p in glob.glob('**/*', recursive=True):
    if not os.path.isfile(p) or p.startswith(('.git', 'docs/current', 'docs/archive')):
        continue
    if not re.search(r'\.(md|sh|py|js|cjs|json|txt|yml|yaml|conf|service)$', p):
        continue
    try:
        if os.path.getsize(p) > 3_000_000:
            continue
        corpus[p] = open(p, encoding='utf-8', errors='ignore').read()
    except Exception:
        pass

rows = []
for f in cands:
    refs_path = []
    refs_name = []
    for p, txt in corpus.items():
        if f in txt:
            refs_name.append(p)
            if ('current/' + f) in txt or ('docs/current/' + f) in txt:
                refs_path.append(p)
    rows.append({'file': f, 'date': fdate(f), 'path_refs': refs_path, 'name_refs': refs_name})

print()
print('%-52s %-11s %s' % ('file', 'date', 'path-refs (移动后会断)'))
for r in rows:
    flag = ' '.join(r['path_refs'][:3])
    print('%-52s %-11s %s' % (r['file'], r['date'], flag if flag else '-'))

blocked = [r for r in rows if r['path_refs']]
moved = [r for r in rows if not r['path_refs']]
print()
print('可安全移动: %d   有路径引用需先处理: %d' % (len(moved), len(blocked)))
json.dump(rows, open('/tmp/archive_cands.json', 'w'), ensure_ascii=False, indent=1)
