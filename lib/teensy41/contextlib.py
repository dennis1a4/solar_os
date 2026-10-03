"""Generator context managers; ExitStack is not included in this port bundle."""
from ucontextlib import contextmanager

class closing:
    def __init__(self,obj): self.obj=obj
    def __enter__(self): return self.obj
    def __exit__(self,*args): self.obj.close()

class suppress:
    def __init__(self,*exceptions): self.exceptions=exceptions
    def __enter__(self): return self
    def __exit__(self,type,value,tb): return type is not None and issubclass(type,self.exceptions)
