"""SolarOS virtual filesystem subset. All paths retain mount ownership checks."""
from _solaros_hw import fs as _fs
sep='/'
def getcwd(): return _fs(0)
def listdir(path='.'): return _fs(1,path)
def stat(path): return _fs(2,path)
def mkdir(path): return _fs(3,path)
def remove(path): return _fs(4,path)
unlink=remove
def rmdir(path): return _fs(5,path)
def rename(source,destination): return _fs(6,source,destination)
def chdir(path): return _fs(7,path)
def ilistdir(path='.'):
    for name in listdir(path):
        st=stat(path.rstrip('/')+'/'+name)
        yield (name,st[0]&0xf000,0,st[6])
