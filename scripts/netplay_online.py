#!/usr/bin/env python3
"""Two-player Riisorted online development launcher (Linux/Windows x86-64)."""
import argparse
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'launcher'))
from netplay import OnlineSession

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('role', choices=('host', 'join'))
parser.add_argument('session', type=Path, help='A new folder for isolated saves and logs')
parser.add_argument('--source-out', type=Path, default=ROOT / ('out/windows' if sys.platform == 'win32' else 'out'))
parser.add_argument('--address', default=None, help='Host bind address, or host IP when joining')
parser.add_argument('--port', type=int, default=42680)
parser.add_argument('--invite', default='', help='Invitation printed by the host')
parser.add_argument('--transport', choices=('direct', 'eos'), default='direct')
parser.add_argument('--force-relay', action='store_true', help='Force EOS relay transport for connection testing')
args = parser.parse_args()
session = OnlineSession(args.source_out, args.session,
                        log=lambda line: print(line, flush=True),
                        invitation=lambda code: print('Invitation: ' + code, flush=True))
session.transport = args.transport
session.force_relay = args.force_relay
try:
    if args.role == 'join' and (not args.invite or (args.transport == 'direct' and not args.address)):
        parser.error('Joining requires --invite INVITATION, and --address HOST_IP for direct connections')
    if not 1 <= args.port <= 65535:
        parser.error('Port must be between 1 and 65535')
    session.run_session(args.role, args.address or '0.0.0.0', args.port, args.invite)
except KeyboardInterrupt:
    session.cancel()
except Exception as error:
    print(str(error), file=sys.stderr)
    raise SystemExit(1)
