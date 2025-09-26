from collections import namedtuple

settings = {
    'counter': 1,
}

Dims = namedtuple('Dims', ['width', 'height', 'goal_width', 'free_kick'])

class SoccerError(Exception):
    pass

def error(msg):
    raise SoccerError(msg)

def counter():
    result = settings['counter']
    settings['counter'] = result + 1
    return result
