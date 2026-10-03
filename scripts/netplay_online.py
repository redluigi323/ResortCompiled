#!/usr/bin/env python3
"""Direct two-player Riisorted online development launcher (Linux x86-64)."""
import argparse
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'launcher'))
from netplay import OnlineSession

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('role', choices=('host', 'join'))
parser.add_argument('session', type=Path, help='A new folder for isolated saves and logs')
parser.add_argument('--source-out', type=Path, default=ROOT / 'out')
parser.add_argument('--address', default=None, help='Host bind address, or host IP when joining')
parser.add_argument('--port', type=int, default=42680)
parser.add_argument('--invite', default='', help='Invitation printed by the host')
args = parser.parse_args()
session = OnlineSession(args.source_out, args.session,
                        log=lambda line: print(line, flush=True),
                        invitation=lambda code: print('Invitation: ' + code, flush=True))
try:
    if args.role == 'join' and (not args.address or not args.invite):
        parser.error('Joining requires --address HOST_IP and --invite INVITATION')
    if not 1 <= args.port <= 65535:
        parser.error('Port must be between 1 and 65535')
    session.run_session(args.role, args.address or '0.0.0.0', args.port, args.invite)
except KeyboardInterrupt:
    session.cancel()
except Exception as error:
    print(str(error), file=sys.stderr)
    raise SystemExit(1)
