import os, re, collections

readme = open('docs/README.md', encoding='utf-8').read()
# 只取 §1~§3（到 "## 4." 之前）
sec = readme.split('## 4.')[0]
targets = re.findall(r'\(current/([^)\s]+\.md)\)', sec)
targets = sorted(set(targets))
print('§1~§3 索引文档:', len(targets))

PREFIX = r'(?:scripts|tools|test_server|looks|fw|docs|evidence|rea-evidence|raw8|nx500-modding-work)/'
pat = re.compile(r'(?<![\w/])(' + PREFIX + r'[\w./\-]+)')

def noise(p):
    if p.endswith(('-', '_', '/', '..')):
        return True
    if re.match(r'.*fw[-_]?[a-z_]+$', p) and '.' not in p.split('/')[-1]:
        return True
    if p.count('/') < 1:
        return True
    return False

out = collections.OrderedDict()
for f in targets:
    p = os.path.join('docs/current', f)
    if not os.path.exists(p):
        out[f] = ['<file itself missing>']
        continue
    body = open(p, encoding='utf-8', errors='ignore').read()
    bad = []
    for m in set(pat.findall(body)):
        q = m.rstrip('.,;:)]`\'"')
        if noise(q):
            continue
        q2 = q[len('nx500-modding-work/'):] if q.startswith('nx500-modding-work/') else q
        if not os.path.exists(q2):
            bad.append(q)
    if bad:
        out[f] = sorted(set(bad))

print('有真实失效引用的:', len(out))
for f, bad in out.items():
    print('\n%s' % f)
    for b in bad:
        print('    %s' % b)
