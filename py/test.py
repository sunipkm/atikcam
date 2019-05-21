import ctypes as c
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as anim
import time

class AtikCaps(c.Structure):
    _fields_ = [
        ("lineCount",c.c_uint),
        ("pixelCountX",c.c_uint),
        ("pixelCountY",c.c_uint),
        ("pixelSizeX",c.c_double),
        ("pixelSizeY",c.c_double),
        ("maxBinX",c.c_uint),
        ("maxBinY",c.c_uint),
        ("tempSensorCount",c.c_uint),
        ("offsetX",c.c_int),
        ("offsetY",c.c_int),
        ("supportsLongExposure",c.c_bool),
        ("minShortExposure",c.c_double),
        ("maxShortExposure",c.c_double)
    ]
    
caps = AtikCaps()

print(caps.lineCount)

lib = c.cdll.LoadLibrary('libatik.so')

lib.debug(c.c_bool(True))

#dev = c.c_void_p()
#lib.start(c.byref(dev))
lib.start()
#lib.dopen(dev)
lib.dopen()
#print(dev)

#lib.getCapabilities(dev,c.byref(caps))
lib.getCapabilities(c.byref(caps))
for f in AtikCaps._fields_:
	eval("print(caps."+f[0]+")")
	
width = caps.pixelCountX
height = caps.pixelCountY
print(width)
print(lib.readCCD_short(0,0,1392,1040,1,1,c.c_double(0.02)))
time.sleep(1)
dat = np.zeros(width*height,dtype=np.uint16)
print("Here:" ,dat.shape)
print(lib.getImage(c.c_void_p(dat.ctypes.data),c.c_uint(width*height)))
dat=dat.reshape((1040,1392))
print(dat.shape)
plt.imshow(dat)
plt.show()
lib.dclose()

print("Done")
