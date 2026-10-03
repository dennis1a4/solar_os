# Expansion slot1, Serial8: RX34/TX35, 3.3 V TTL (not RS232 voltage).
from machine import UART
import time
with UART(8,115200,timeout=10) as port:
    for _ in range(100):
        data=port.read(128)
        if data:
            port.write(data)
        time.sleep_ms(10)
