"""Read-only release guard for Git's INDEX (not the working-tree files).
Rules catch common accidents, not every possible secret. Review the staged diff too.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args])
files=[p for p in git('ls-files','-z').decode().split('\0') if p]
errors=[]
placeholders={
    'WIFI_SSID':'YOUR_WIFI_SSID', 'WIFI_PASSWORD':'YOUR_WIFI_PASSWORD',
    'CAMPUS_USERNAME':'YOUR_STUDENT_ID', 'CAMPUS_PASSWORD':'YOUR_CAMPUS_PASSWORD',
}
patterns={
    'private key':r'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----',
    'GitHub token':r'\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b',
    'personal absolute path':r'(?i)\b[A-Z]:[\\/](?:Users|omp)[\\/]',
}
for name in files:
    p=Path(name)
    if p.name=='Config.local.h' or p.name.startswith('.env') or any(x in p.parts for x in ('.diag','build','__pycache__')):
        errors.append((name,'private/local file')); continue
    if p.suffix.lower() in ('.bin','.elf','.o','.a','.d','.map','.exe','.log','.pcap','.pcapng','.pyc') or p.name.endswith('_pass.txt'):
        errors.append((name,'binary/log/credential artifact')); continue
    blob=git('show',':'+name)
    try: text=blob.decode('utf-8-sig')
    except UnicodeDecodeError:
        errors.append((name,'non-text file requires explicit review')); continue
    for label,pattern in patterns.items():
        if re.search(pattern,text): errors.append((name,label))
    if p.name in ('Config.h','Config.local.example.h'):
        for key,value in placeholders.items():
            matches=re.findall(rf'(?m)^#define\s+{key}\s+"([^"]*)"\s*$',text)
            if matches!=[value]: errors.append((name,f'{key} must remain a single placeholder'))
if errors:
    for name,reason in errors: print(f'FAIL {name}: {reason}')
    sys.exit(1)
print(f'PASS privacy rules: inspected {len(files)} staged text files; no values printed.')
