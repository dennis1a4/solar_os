# Bus 2: SDA 25 / SCL 24. Scan only hardware you intend to probe.
from machine import I2C
with I2C(2) as bus:
    print([hex(address) for address in bus.scan()])
