def make_schedule(qcycles, *players):
    players = list(players)
    n = len(players)

    if n % 2 == 1:
        players.append(None)
        n += 1

    r = 0
    for cycle in range(qcycles):
        for _ in range(n - 1):
            r += 1

            for i1 in range(n):
                i2 = (r - i1 - 1) % (n - 1)
                if i1 == i2:
                    i2 = n - 1
                if i1 > i2:
                    continue

                p1 = players[i1]
                p2 = players[i2]
                if p2 is None:
                    continue

                if (i1 + i2 + cycle) & 1:
                    yield (r, p1, p2)
                else:
                    yield (r, p2, p1)
