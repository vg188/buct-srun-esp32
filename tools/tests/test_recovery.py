"""Offline tests: compile the actual controller; never open network/serial ports.
Requires g++ on PATH. Temporary binaries contain only synthetic credentials.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TESTS = (
    'late_router dhcp_window dhcp_success ap_lost_during_dhcp settle_disconnect '
    'backoff wan_recovers already_online login_probe_fails online_reconnect fatal '
    'unknown_status online_probe_failure_safe mismatch_default_safe verify_disabled '
    'verify_cadence replacement_cooldown replacement_reconnect_cooldown replacement_fatal '
    'drop_during_probe drop_during_info blocking_call_timing wrap_wifi wrap_schedule wrap_cooldown'
).split()
# Avoid depending on MinGW runtime DLL discovery when launching on Windows.
LINK_FLAGS = ['-static-libgcc', '-static-libstdc++'] if os.name == 'nt' else []
with tempfile.TemporaryDirectory(prefix='buct-recovery-') as temp:
    exe = Path(temp) / 'recovery.exe'
    subprocess.run([
        'g++', '-std=c++11', *LINK_FLAGS, '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'BUCT_AutoAuth'),
        str(ROOT / 'tools/tests/recovery_test.cpp'),
        str(ROOT / 'BUCT_AutoAuth/auth_controller.cpp'), '-o', str(exe)
    ], check=True)
    for test in TESTS:
        subprocess.run([str(exe), test], check=True)
print(f'{len(TESTS)} recovery scenarios passed (mock network only).')
