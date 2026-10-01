"""Teensy detachable file logger; run, Ctrl+Z, bg, then tail /sd/logger.csv.

Use jobs to find its numeric ID, fg ID to reattach, or job stop ID to stop.
This sample records uptime; it does not require CAN hardware or bindings.
"""
import solaros

with open('/sd/logger.csv', 'a') as log:
    while True:
        stamp = solaros.time.ticks_ms()
        log.write(str(stamp) + '\n')
        log.flush()
        print('Logged', stamp)
        solaros.sleep_ms(250)
