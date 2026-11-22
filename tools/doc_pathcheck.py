import os, re, glob, collections

cur_dir = 'docs/current'
files = sorted(f for f in os.listdir(cur_dir) if f.endswith('.md'))

PREFIXES = ('scripts/', 'tools/', 'test_server/', 'looks/', 'fw/', 'nx500-modding-work/', 'docs/')
pat = re.compile(r'(?<![\w/])((?:%s)[\w./\-]+)' % '|'.join(p.rstrip('/') for p in PREFIXES))

broken = collections.defaultdict(list)
for f in files:
    body = open(os.path.join(cur_dir, f), encoding='utf-8', errors='ignore').read()
    for m in set(pat.findall(body)):
        p = m.rstrip('.,;:)]`\'"')
        if not p or p.endswith('/'):
            continue
        if p.startswith('nx500-modding-work/'):
            p = p[len('nx500-modding-work/'):]
        if not os.path.exists(p):
            broken[f].append(p)

total = sum(len(v) for v in broken.values())
print('存在失效路径引用的文档数:', len(broken), ' 失效条目:', total)
for f in sorted(broken):
    print('\n  %s' % f)
    for p in sorted(set(broken[f]))[:6]:
        print('      -> %s' % p)
