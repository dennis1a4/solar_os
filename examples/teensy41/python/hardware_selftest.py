# Exercises ownership and API behavior without sending peripheral commands.
import machine,time,os,heapq,bisect,itertools,functools,contextlib
import lsm9ds1,bmm150
import _solaros_hw as hw

def raises(fn,codes=(16,22,88)):
    try:
        fn()
    except OSError as e:
        assert e.args[0] in codes,e.args
        return
    raise AssertionError('operation should have been rejected')

with machine.Pin(28) as pin:
    assert pin.value() in (0,1)
    raises(lambda:machine.Pin(28))
    raises(lambda:machine.UART(7))
with machine.Pin(29):
    raises(lambda:machine.PWM(28))  # Timer partner cannot be stolen.
with machine.Pin(28):pass          # Failed bundle claim left no partial lease.
raises(lambda:machine.Pin(2))      # Fixed board pin.
raises(lambda:machine.Pin(48))     # QSPI pads never exposed.
raises(lambda:machine.ADC(18))     # I2C SDA is protected.
raises(lambda:machine.SPI(0,cs=9)) # Display reset is protected.
with machine.SPI(1,cs=36):pass     # No transfer or clock output.
with machine.I2C(0) as bus:
    raises(lambda:bus.writeto(0x0a,b'')) # Audio address reservation, no transaction.
with machine.I2C(2) as bus:
    raises(lambda:hw.transfer(bus._live(),0x40,None,bytearray(33)),(22,))
raises(lambda:machine.I2C(2,freq=400000),(88,))
handles=[machine.I2C(2) for _ in range(16)]
raises(lambda:machine.I2C(2),(12,))
for handle in handles:handle.deinit()
with machine.UART(8,timeout=0) as uart:
    assert uart.any()>=0
    assert uart.read(1) is None
h=hw.open(0,28,0,0);hw.close(h)
raises(lambda:hw.value(h,0,0))
assert time.ticks_diff(2,(1<<30)-2)==4
assert time.ticks_add((1<<30)-1,2)==1
start=time.ticks_ms();time.sleep_ms(5)
assert time.ticks_diff(time.ticks_ms(),start)>=5
items=[8,2,6];heapq.heapify(items);assert heapq.heappop(items)==2
assert bisect.bisect([1,3,5],4)==2
assert list(itertools.islice(itertools.count(),3))==[0,1,2]
assert functools.reduce(lambda a,b:a+b,[1,2,3])==6
@contextlib.contextmanager
def managed():
    yield 42
with managed() as value:assert value==42
with contextlib.suppress(ValueError):raise ValueError('expected')
root='/flash/_python_api_test_'+str(time.ticks_ms())
os.mkdir(root)
with open(root+'/a','w') as f:f.write('offline')
assert os.stat(root+'/a')[6]==7
assert os.listdir(root)==['a']
old=os.getcwd();os.chdir(root)
with open('a') as f:assert f.read()=='offline'
raises(lambda:os.chdir('a'),(20,))
os.chdir(old)
os.rename(root+'/a',root+'/b');os.remove(root+'/b');os.rmdir(root)
assert os.stat('/flash/lib/machine.py')[6]>0
print('PY_HARDWARE_OFFLINE_PASS')
