import sys
import fcntl
from pathlib import Path
from deathmatch import deathmatch
from utils import Dims

STATS_DIR = Path.home() / 'data' / 'paper-football'
TOURNEY_DIR = STATS_DIR / 'tournaments'

def find_next(fn):
    with open(fn, 'r+') as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        try:
            lines = f.readlines()
            for i, line in enumerate(lines):
                if line.startswith('GAME'):
                    continue
                parts = line.strip().split('\t')
                if len(parts) >= 6 and parts[5] == '***':
                    match_id, round_id, game_id, e1, e2, result = parts[:6]

                    lines[i] = f"{match_id}\t{round_id}\t{'~~~':>6s}\t{e1}\t{e2}\t~~~\n"
                    f.seek(0)
                    f.writelines(lines)
                    f.truncate()
                    return int(match_id), e1.strip(), e2.strip()
        finally:
            fcntl.flock(f, fcntl.LOCK_UN)

    return None

def update_result(fn, match_id, game_id, result):
    with open(fn, 'r+') as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        try:
            lines = f.readlines()
            for i, line in enumerate(lines):
                if line.startswith('GAME'):
                    continue
                parts = line.strip().split('\t')
                if len(parts) >= 6 and int(parts[0]) == match_id:
                    lines[i] = f"{match_id:4d}\t{parts[1]}\t{game_id:6d}\t{parts[3]}\t{parts[4]}\t{result}\n"
                    break

            f.seek(0)
            f.writelines(lines)
            f.truncate()
        finally:
            fcntl.flock(f, fcntl.LOCK_UN)

def play_matches(tourney_name, qmatches):
    fn = TOURNEY_DIR / f"{tourney_name}.txt"

    for i in range(qmatches):
        match = find_next(fn)
        if not match:
            print("No more unplayed matches")
            break

        match_id, e1, e2 = match
        print(f"Match {match_id}: {e1} vs {e2}")

        dims = Dims(21, 31, 6, 5)
        protocol = deathmatch(e1, e1, e2, dims)

        if protocol.result is None:
            result = "???"
        elif protocol.result > 0:
            result = "1-0"
        else:
            result = "0-1"

        update_result(fn, match_id, protocol.game_id, result)
        print(f"Result: {result}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python play_tourney.py <tourney_name> <qmatches>")
        sys.exit(1)

    tourney_name = sys.argv[1]
    qmatches = int(sys.argv[2])
    play_matches(tourney_name, qmatches)
