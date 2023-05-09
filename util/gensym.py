#!/usr/bin/python
import sys

size = 'quad'

def extern(sym):
    print(f'.extern {sym}')
    print(f'.type {sym}, @function')

def entry(sym):
    print(f'.{size} {sym}')
    print(f'.asciz "{sym}"')

ignore = ['kernel_symbols_start', 'kernel_symbols_end']
lines = [x.strip().split(' ')[2] for x in sys.stdin]
lines = [line for line in lines if line not in ignore]

print('.section .symbols\n')
for name in lines:
    extern(name)

print('.global kernel_symbols_start')
print('kernel_symbols_start:\n')
for name in lines:
    entry(name)

print('.global kernel_symbols_end')
print('kernel_symbols_end:')