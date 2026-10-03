"""Cooperative sleeps and wrap-safe 30-bit monotonic ticks; epoch is Unix UTC."""
from _solaros_hw import ticks as _ticks, delay as sleep_ms, epoch as time

def ticks_ms():
    return _ticks(False)

def ticks_us():
    return _ticks(True)

def ticks_diff(a,b):
    return ((a-b+(1<<29)) & ((1<<30)-1))-(1<<29)

def ticks_add(ticks,delta):
    return (ticks+delta)&((1<<30)-1)

def sleep(seconds):
    if seconds<0:
        raise ValueError('negative sleep')
    sleep_ms(int(seconds*1000))

def sleep_us(us):
    if us<0:
        raise ValueError('negative sleep')
    if us>=1000:
        sleep_ms(us//1000)
    start=ticks_us()
    while ticks_diff(ticks_us(),start)<us%1000:
        pass
