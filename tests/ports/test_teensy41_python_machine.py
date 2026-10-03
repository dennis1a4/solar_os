"""Host contract tests for wrappers; real claims are checked on the device."""
import importlib.util,sys,types,unittest,time
time.ticks_ms=lambda:0
time.ticks_diff=lambda a,b:a-b
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Backend:
    def __init__(self):self.live={};self.next=0;self.last=None
    def open(self,*args):self.next+=1;self.live[self.next]=args;return self.next
    def close(self,h):del self.live[h]
    def value(self,h,op,value):assert h in self.live;return value
    def transfer(self,h,address,tx,rx):
        assert h in self.live;self.last=(address,bytes(tx) if tx is not None else None)
        if rx is not None:
            rx[:]=bytes(range(len(rx)))
            return len(rx)
        return len(tx)
b=Backend();sys.modules['_solaros_hw']=b
spec=importlib.util.spec_from_file_location('teensy_machine',ROOT/'lib/teensy41/machine.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class MachineTests(unittest.TestCase):
    def tearDown(self):self.assertFalse(b.live)
    def test_pin_cleanup(self):
        with m.Pin(28,m.Pin.OUT,value=1) as pin:
            self.assertEqual(b.live[pin._handle],(0,28,1,1));pin.off()
        with self.assertRaises(OSError):pin.on()
    def test_i2c_registers(self):
        with m.I2C(2) as bus:
            self.assertEqual(bus.readfrom_mem(0x40,0x1234,3,addrsize=16),b'\x00\x01\x02')
            self.assertEqual(b.last,(0x40,b'\x12\x34'))
            with self.assertRaises(NotImplementedError):bus.writeto(0x40,b'',False)
            with self.assertRaises(ValueError):bus.readfrom(0x40,33)
    def test_spi_mapping(self):
        with m.SPI(1,cs=36,polarity=1,phase=1) as spi:
            self.assertEqual(b.live[spi._handle],(4,1,1000000,3))
            self.assertEqual(spi.read(3),b'\x00\x01\x02')
            with self.assertRaises(ValueError):spi.write_readinto(b'1',bytearray(2))
    def test_uart(self):
        with m.UART(8,31250) as uart:
            self.assertEqual(b.live[uart._handle],(5,1,31250,0))
            self.assertEqual(uart.read(2),b'\x00\x01')
            self.assertEqual(uart.write(b'abc'),3)
    def test_pwm_takes_pin(self):
        pin=m.Pin(28)
        with m.PWM(pin,duty_u16=1000) as pwm:
            self.assertIsNone(pin._handle)
            self.assertEqual(b.live[pwm._handle],(2,28,1000,1000))
    def test_invalid(self):
        with self.assertRaises(ValueError):m.UART(1)
        with self.assertRaises(ValueError):m.I2C(9)
        with self.assertRaises(ValueError):m.Pin(28,m.Pin.OUT,m.Pin.PULL_UP)
if __name__=='__main__':unittest.main()
