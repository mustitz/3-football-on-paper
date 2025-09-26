import sys
from pathlib import Path
from tournament import make_schedule
from deathmatch import load_engine
from utils import Dims

STATS_DIR = Path.home() / 'data' / 'paper-football'
TOURNEY_DIR = STATS_DIR / 'tournaments'

def test_engine(name, dims):
    try:
        with load_engine(name, dims) as engine:
            engine.status()
        return True
    except Exception as e:
        print(f"Engine {name} failed: {e}")
        return False

def create_tourney(name, qcycles, dims, *engines):
    print(f"Testing {len(engines)} engines...")
    for engine in engines:
        if not test_engine(engine, dims):
            print(f"Engine {engine} failed, aborting")
            return

    print(f"All {len(engines)} engines OK")

    TOURNEY_DIR.mkdir(exist_ok=True)
    fn = TOURNEY_DIR / f"{name}.txt"

    with open(fn, 'w') as f:
        f.write(f"GAME {dims.width} {dims.height} {dims.goal_width} {dims.free_kick}\n")
        counter = 1
        for r, e1, e2 in make_schedule(qcycles, *engines):
            f.write(f"{counter:4d}\t{r:3d}\t{0:6d}\t{e1:16s}\t{e2:16s}\t***\n")
            counter += 1

    print(f"Tournament '{name}' created with {r} matches")
    print(f"File: {fn}")

if __name__ == "__main__":
    name = sys.argv[1]
    dims = Dims(21, 31, 6, 5)

    engines = []
    for qthink in [0.5, 1, 2, 5]:
        for C in [0.3, 0.7, 1.4, 2.0, 4.0]:
            qthink_str = f"{qthink}M" if qthink != int(qthink) else f"{int(qthink)}M"
            engines.append(f"origin/{qthink_str}-C{C}")

    create_tourney(name, 4, dims, *engines)
