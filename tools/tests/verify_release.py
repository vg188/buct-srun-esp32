"""Verify the candidate preserves the proven release's protocol implementation."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
REPO=ROOT if (ROOT/'.git').exists() else ROOT/'publish/buct-srun-esp32'
def baseline(name):
    return subprocess.check_output(['git','-C',str(REPO),'show',f'beaaa44:BUCT_AutoAuth/{name}']).decode('utf-8').replace('\r\n','\n')
def current(name):
    return (ROOT/'BUCT_AutoAuth'/name).read_text(encoding='utf-8-sig')
for name in ['srun_crypto.h','srun_crypto.cpp']:
    assert baseline(name)==current(name), f'Crypto changed: {name}'
expected=baseline('srun_auth.cpp').replace(
    '    if (!getOnlineInfo(info) || !info.online) return true;',
    '    if (!getOnlineInfo(info)) return false;\n    if (!info.online) return true;'
).replace('    return true;  // reachable portal, just not online',
          '    return false;  // unknown response is not proof of an offline session')
expected=expected.replace('    srunDebugLog("HTTP GET -> %s%s", host, pathQ);',
    '    // Never log the query: it contains account/authentication material.\n'
    "    const char* queryStart = strchr(pathQ, '?');\n"
    '    int pathLength = queryStart ? (int)(queryStart - pathQ) : (int)strlen(pathQ);\n'
    '    srunDebugLog("HTTP GET -> %s%.*s", host, pathLength, pathQ);')
expected=expected.replace('srunDebugLog("bad status line: %.60s", statusLine.c_str());',
                         'srunDebugLog("invalid HTTP status line");')
expected=expected.replace('srunDebugLog("login: bad JSONP wrap: %.80s", body.c_str());',
                         'srunDebugLog("login: invalid JSONP wrapper (%u bytes)", (unsigned)body.length());')
expected=expected.replace('// Log the raw response once — field layout differs across srun builds\n    srunDebugLog("login resp: %.160s", json.c_str());',
                         '// Log only size, never raw responses which may contain personal data.\n    srunDebugLog("login response received (%u bytes)", (unsigned)json.length());')
assert current('srun_auth.cpp')==expected, 'Unexpected protocol change beyond reviewed return semantics'
assert '#define ACCOUNT_REPLACEMENT_ENABLED 0' in current('Config.h'), 'Default logout guard changed'
print('PASS release baseline: crypto unchanged; HTTP/login unchanged; two status/logout fixes and redacted logging only.')
