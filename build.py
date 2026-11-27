#!/usr/bin/env python3
"""Build wrapper: runs build.bat with a clean Windows environment.
The MSYS/Git-Bash shell exports variables (VSCMD_*, PROMPT with parentheses,
etc.) that break VsDevCmd.bat's argument parsing, so we spawn cmd.exe
directly with a minimal env from Python."""
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.abspath(__file__))

CLEAN_ENV = {
    'SystemRoot': r'C:\Windows',
    'ProgramFiles(x86)': r'C:\Program Files (x86)',
    'ProgramFiles': r'C:\Program Files',
    'PATH': r'C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem;'
            r'C:\Windows\System32\WindowsPowerShell\v1.0;'
            r'C:\Program Files\dotnet',
    'USERNAME': 'Administrator',
    'USERPROFILE': r'C:\Users\Administrator',
    'TEMP': r'C:\Windows\Temp',
    'TMP': r'C:\Windows\Temp',
    'COMSPEC': r'C:\Windows\System32\cmd.exe',
    'PATHEXT': '.COM;.EXE;.BAT;.CMD',
    'DOTNET_ROOT': r'C:\Program Files\dotnet',
    'DOTNET_CLI_TELEMETRY_OPTOUT': '1',
    'NUMBER_OF_PROCESSORS': str(os.cpu_count() or 4),
}

def run(script):
    path = os.path.join(ROOT, script)
    print(f'>>> running {script} (clean env)')
    r = subprocess.run(['cmd', '/c', script], cwd=ROOT, env=CLEAN_ENV)
    return r.returncode

if __name__ == '__main__':
    scripts = sys.argv[1:] or ['build.bat']
    rc = 0
    for s in scripts:
        rc = run(s) or rc
    sys.exit(rc)
