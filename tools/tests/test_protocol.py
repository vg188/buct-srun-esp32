"""Compile actual protocol code against an in-memory HTTP fake: zero socket I/O."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
TESTS = 'offline online unknown logout_unknown logout_unreachable logout_offline logout_ack logout_rejected login_ok login_error_ok login_bad login_already challenge_ip_fallback'.split()
# Avoid depending on MinGW runtime DLL discovery when launching on Windows.
LINK_FLAGS = ['-static-libgcc', '-static-libstdc++'] if os.name == 'nt' else []
with tempfile.TemporaryDirectory(prefix='buct-protocol-') as temp:
    exe=Path(temp)/'protocol.exe'
    subprocess.run(['g++','-std=c++11',*LINK_FLAGS,'-Wall','-Wextra',
        '-I',str(ROOT/'tools/tests/stubs'),'-I',str(ROOT/'BUCT_AutoAuth'),
        str(ROOT/'tools/tests/protocol_test.cpp'), str(ROOT/'BUCT_AutoAuth/srun_auth.cpp'),
        '-o',str(exe)],check=True)
    for test in TESTS:
        subprocess.run([str(exe),test],check=True)
    subprocess.run(['g++','-std=c++11',*LINK_FLAGS,'-Wall','-Wextra','-Werror',
        '-I',str(ROOT/'tools/tests/stubs'),'-I',str(ROOT/'BUCT_AutoAuth'),
        '-x','c++','-c',str(ROOT/'BUCT_AutoAuth/BUCT_AutoAuth.ino'),
        '-o',str(Path(temp)/'adapter.o')],check=True)
print(f'{len(TESTS)} protocol cases passed; Arduino adapter host syntax check passed (not ESP32 compilation).')
