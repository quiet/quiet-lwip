#!/usr/bin/env python
from __future__ import print_function

import sys

start_str = 'received frame @ '

if __name__ == '__main__':
    fname = sys.argv[1];
    with open(fname, 'rb') as f:
        for line in f:
            line = line[len(start_str):].strip()
            bytes_start = line.rindex(':') + 2
            print(line[:bytes_start - 2], end='')
            line = line[bytes_start:]
            count = 0
            for c in line:
                if (count % 32 == 0):
                    print()
                    print('%06x' % (count/2), end=' ')
                if (count % 2 == 0):
                    print(' %c' % c, end='')
                else:
                    print('%c' % c, end='')
                count += 1
            print()
            print('%06x' % (count/2))
            print()
