# MicroPython syntax highlighting demo
import time

class Sensor:
    def read(self):
        return 0x2A

sensor = Sensor()
for sample in range(3):
    value = sensor.read()
    print("sample", sample, value, len("hello"))
    time.sleep(0.1)

enabled = True
missing = None
message = """A multiline string.
Keywords such as def and print remain string text here.
"""
