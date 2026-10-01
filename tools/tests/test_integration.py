"""Host integration of actual .ino + controller + protocol with in-memory I/O.
No socket, serial, login or logout calls reach a real device/server.
"""
from pathlib import Path
import re
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
TESTS='diagnostics_monitor usb_power_only serial_attach_later late_ap_end_to_end dhcp_independent_window gateway_required wifi_status_required disconnect_rejected fatal_end_to_end mismatch_no_logout portal_online_no_logout daily_cycles_120_days'.split()
# Avoid depending on MinGW runtime DLL discovery when launching on Windows.
LINK_FLAGS = ['-static-libgcc', '-static-libstdc++'] if os.name == 'nt' else []
with tempfile.TemporaryDirectory(prefix='buct-integration-') as temp:
    stage=Path(temp)/'sketch'
    stage.mkdir()
    for src in (ROOT/'BUCT_AutoAuth').iterdir():
        if src.name in ('Config.local.h', 'Config.local.example.h') or src.suffix not in ('.ino','.cpp','.h'):
            continue
        text=src.read_text(encoding='utf-8-sig')
        if src.name=='Config.h':
            for key,value in [('WIFI_SSID','test-ap'),('WIFI_PASSWORD','fake-wifi-password'),('CAMPUS_USERNAME','test-user'),('CAMPUS_PASSWORD','fake-campus-password')]:
                text,count=re.subn(rf'(?m)^#define\s+{key}\s+.*$',f'#define {key} "{value}"',text)
                assert count==1, f'Cannot sanitize {key}'
        (stage/src.name).write_text(text,encoding='utf-8')
    exe=Path(temp)/'integration.exe'
    subprocess.run(['g++','-std=c++11',*LINK_FLAGS,'-Wall','-Wextra','-Werror',
        '-I',str(ROOT/'tools/tests/stubs'),'-I',str(stage),
        str(ROOT/'tools/tests/integration_test.cpp'),str(stage/'auth_controller.cpp'),
        str(stage/'srun_auth.cpp'),'-o',str(exe)],check=True)
    for name in TESTS:
        subprocess.run([str(exe),name],check=True,timeout=15)
print(f'{len(TESTS)} integration scenarios passed; clocks and peripherals are simulated.')
