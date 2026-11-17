#!/usr/bin/env python3
"""Small helpers for editing vendored Valve sources: brace-matched block replacement."""
def match_brace(s, i):
    """i = index of '{'; returns index of matching '}'"""
    depth = 0
    while True:
        c = s[i]
        if c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1

def replace_function_body(s, signature, body):
    a = s.index(signature)
    b = s.index('{', a)
    e = match_brace(s, b)
    return s[:b] + '{\n' + body + '\n}' + s[e + 1:]

def replace_if_block(s, anchor, replacement, start=0):
    """replace the statement starting at anchor (an if-block) including its braces"""
    a = s.index(anchor, start)
    b = s.index('{', a)
    e = match_brace(s, b)
    return s[:a] + replacement + s[e + 1:]
