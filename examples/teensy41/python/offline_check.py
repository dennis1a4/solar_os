import os,time,heapq,bisect,itertools,functools,contextlib,machine
print('cwd:',os.getcwd())
print('library files:',os.listdir('/flash/lib'))
items=[8,3,5];heapq.heapify(items)
print('minimum:',heapq.heappop(items))
print('tick:',time.ticks_ms(),'UTC epoch:',time.time())
print('Python libraries available offline')
