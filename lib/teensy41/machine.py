"""Resource-managed Teensy subset of MicroPython machine; see offline API notes."""
import _solaros_hw as _hw

class _Device:
    def deinit(self):
        if self._handle is not None:
            _hw.close(self._handle)
            self._handle = None
    def _live(self):
        if self._handle is None:
            raise OSError(9)
        return self._handle
    def __enter__(self):
        self._live()
        return self
    def __exit__(self, *args):
        self.deinit()

def _pin_id(pin):
    return pin.id if isinstance(pin, Pin) else int(pin)

class Pin(_Device):
    IN = 0
    OUT = 1
    PULL_UP = 2
    PULL_DOWN = 3
    def __init__(self, id, mode=IN, pull=None, *, value=0):
        self.id = int(id)
        self._handle = None
        self.init(mode, pull, value=value)
    def init(self, mode=IN, pull=None, *, value=0):
        if mode not in (self.IN, self.OUT) or pull not in (None, self.PULL_UP, self.PULL_DOWN):
            raise ValueError('unsupported pin mode')
        if mode == self.OUT and pull is not None:
            raise ValueError('pull requires input')
        self.deinit()
        self._handle = _hw.open(0, self.id, pull if pull is not None else mode, int(bool(value)))
    def value(self, value=None):
        return _hw.value(self._live(), 0 if value is None else 1, 0 if value is None else int(bool(value)))
    __call__ = value
    def on(self):
        self.value(1)
    def off(self):
        self.value(0)

class ADC(_Device):
    def __init__(self, pin):
        id = _pin_id(pin)
        if isinstance(pin, Pin):
            pin.deinit()
        self._handle = _hw.open(1, id, 0, 0)
    def read_u16(self):
        return _hw.value(self._live(), 0, 0)

class PWM(_Device):
    def __init__(self, pin, *, freq=1000, duty_u16=0):
        id = _pin_id(pin)
        if isinstance(pin, Pin):
            pin.deinit()
        self._handle = _hw.open(2, id, freq, duty_u16)
    def freq(self, value=None):
        return _hw.value(self._live(), 3 if value is None else 2, value or 0)
    def duty_u16(self, value=None):
        return _hw.value(self._live(), 0 if value is None else 1, value or 0)

class I2C(_Device):
    # Fixed board buses. Existing firmware clients share each bus at 100 kHz.
    def __init__(self, id, *, freq=100000, scl=None, sda=None):
        pins = ((19,18), (16,17), (24,25))
        if id not in (0,1,2):
            raise ValueError('I2C id must be 0, 1 or 2')
        if scl is not None and _pin_id(scl) != pins[id][0] or sda is not None and _pin_id(sda) != pins[id][1]:
            raise ValueError('fixed I2C pins')
        self._handle = _hw.open(3, id, freq, 0)
    def scan(self):
        found = []
        for address in range(8,120):
            try:
                self.writeto(address, b'')
                found.append(address)
            except OSError as e:
                if e.args[0] not in (16,19):
                    raise
        return found
    def writeto(self, address, buffer, stop=True):
        if not stop:
            raise NotImplementedError('use readfrom_mem for repeated start')
        _hw.transfer(self._live(), address, buffer, None)
        return len(buffer)
    def readfrom_into(self, address, buffer, stop=True):
        if not stop:
            raise NotImplementedError('stop=False')
        _hw.transfer(self._live(), address, None, buffer)
    def readfrom(self, address, nbytes, stop=True):
        if not 0 <= nbytes <= 32:
            raise ValueError('I2C transfer limit is 32 bytes')
        data = bytearray(nbytes)
        self.readfrom_into(address, data, stop)
        return bytes(data)
    @staticmethod
    def _reg(memaddr, addrsize):
        if addrsize not in (8,16) or not 0 <= memaddr < (1 << addrsize):
            raise ValueError('register address')
        return bytes((memaddr,)) if addrsize == 8 else bytes((memaddr >> 8, memaddr & 255))
    def readfrom_mem_into(self, address, memaddr, buffer, *, addrsize=8):
        _hw.transfer(self._live(), address, self._reg(memaddr,addrsize), buffer)
    def readfrom_mem(self, address, memaddr, nbytes, *, addrsize=8):
        if not 0 <= nbytes <= 32:
            raise ValueError('I2C transfer limit is 32 bytes')
        data = bytearray(nbytes)
        self.readfrom_mem_into(address, memaddr, data, addrsize=addrsize)
        return bytes(data)
    def writeto_mem(self, address, memaddr, buffer, *, addrsize=8):
        self.writeto(address, self._reg(memaddr,addrsize) + bytes(buffer))

class SPI(_Device):
    MSB = 0
    # This port requires an explicit, automatically controlled expansion CS.
    def __init__(self, id, *, cs, baudrate=1000000, polarity=0, phase=0, bits=8, firstbit=MSB):
        mapping = {(1,37):0, (1,36):1, (0,9):2}
        key = (id,_pin_id(cs))
        if key not in mapping or bits != 8 or firstbit != self.MSB or polarity not in (0,1) or phase not in (0,1):
            raise ValueError('unsupported SPI configuration')
        if isinstance(cs, Pin):
            cs.deinit()
        self._handle = _hw.open(4,mapping[key],baudrate,polarity*2+phase)
    def write(self, buffer):
        _hw.transfer(self._live(),0,buffer,None)
    def write_readinto(self, tx, rx):
        if len(tx) != len(rx):
            raise ValueError('buffers must have equal length')
        _hw.transfer(self._live(),0,tx,rx)
    def readinto(self, buffer, write=0xff):
        self.write_readinto(bytes((write,))*len(buffer),buffer)
    def read(self, nbytes, write=0xff):
        if not 0 <= nbytes <= 4096:
            raise ValueError('SPI transfer limit is 4096 bytes')
        data=bytearray(nbytes)
        self.readinto(data,write)
        return bytes(data)

class UART(_Device):
    def __init__(self,id,baudrate=115200,*,bits=8,parity=None,stop=1,timeout=0):
        if id not in (7,8,3) or bits!=8 or parity is not None or stop!=1 or not 0<=timeout<=60000:
            raise ValueError('UART supports 7/8/3, 8N1 and timeout 0..60000 ms')
        self.timeout=timeout
        self._handle=_hw.open(5,{7:0,8:1,3:2}[id],baudrate,0)
    def any(self):
        return _hw.value(self._live(),0,0)
    def write(self, buffer):
        if isinstance(buffer,str):
            buffer=buffer.encode()
        return _hw.transfer(self._live(),0,buffer,None)
    def readinto(self, buffer, nbytes=None):
        import time
        view=memoryview(buffer)
        if nbytes is not None:
            if not 0<=nbytes<=len(view):
                raise ValueError('invalid read length')
            view=view[:nbytes]
        start=time.ticks_ms()
        while True:
            n=_hw.transfer(self._live(),0,None,view)
            if n or not len(view):
                return n
            if time.ticks_diff(time.ticks_ms(),start)>=self.timeout:
                return None
            time.sleep_ms(1)
    def read(self,nbytes=None):
        if nbytes is None:
            nbytes=max(1,min(self.any(),4096))
        if not 0<=nbytes<=4096:
            raise ValueError('UART transfer limit is 4096 bytes')
        data=bytearray(nbytes)
        n=self.readinto(data)
        return bytes(data[:n]) if n is not None else None
    def readline(self):
        data=bytearray()
        while len(data)<4096:
            ch=self.read(1)
            if not ch:
                break
            data.extend(ch)
            if ch==b'\n':
                break
        return bytes(data) if data else None
