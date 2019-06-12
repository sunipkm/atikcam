import ctypes as c
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as anim
import time
import datetime
import astropy.io.fits as pf
import cv2
import socket

def timenow():
    return int((datetime.datetime.now().timestamp()*1e3))

class image(c.Structure):
    _fields_ = [
        ('tnow',c.c_ulonglong),
        ('exposure',c.c_float),
        ('pixx',c.c_ushort),
        ('pixy',c.c_ushort),
        ('imgsize',c.c_int),
        ('ccdtemp',c.c_short),
        ('boardtemp',c.c_short),
        ('chassistemp',c.c_short),
        ('picdata',1449072*c.c_ushort),
        ('unused',6*c.c_ubyte),
        ('unused2',1792*c.c_ubyte)
    ]

port = 12345

fig = plt.figure(figsize=(10,6))
fig.suptitle("Timestamp: %s, exposure: %f s\nCCD Temperature: %f"%(datetime.datetime.fromtimestamp(timenow()/1e3),0,0))

im = plt.imshow(np.zeros((1040,1392),dtype=np.uint16),vmin=0,vmax=65535,animated=True,cmap='bone')
a = image()

def animate(i):
    global a
    val = ''.encode('utf-8')
    print("Receiving %d packets:"%(1))
    for i in range(1):
        s = socket.socket()
        print("Socket created successfully")
        while True:
            try:
                s.connect(('192.168.1.4',port))
                break
            except Exception:
                continue
        temp = s.recv(c.sizeof(image),socket.MSG_WAITALL)
        if (len(temp)!=c.sizeof(image)):
            print("Received: ",len(val))
        s.close()
        val += temp
    c.memmove(c.addressof(a),val,c.sizeof(image))
    img = np.array(a.picdata[0:a.imgsize])
    data = np.reshape(img,(a.pixy,a.pixx))
    print(a.exposure, datetime.datetime.fromtimestamp(a.tnow/1e3))
    #np.save(str(timenow()),data)
    #data = cv2.resize(data,dsize=(1392,1040),cv2.INTER_CUBIC)
    fig.suptitle("Timestamp: %s, exposure: %f s\nCCD Temperature: %f"%(datetime.datetime.fromtimestamp(timenow()/1e3),a.exposure,a.ccdtemp/100))
    # x0 = 0 ; x1 = 1260
    # xl = (x1 - x0)//4
    # img = np.zeros((data[110:950,0:1260].shape[0],data[110:950,0:1260].shape[1],3),dtype=np.uint8)
    # col = ((0x00,0xff,0x92),(0xff,0xe2,0x00),(0xaa,0x00,0x00),(0xff,0x00,0x00))
    # for k in range(4):
	# #plt.figure(figsize=(6,12))
    #     dat = np.zeros(data[110:950,x0+k*xl:x0+(k+1)*xl].shape,dtype=np.float64)
    #     dat += data[110:950,x0+k*xl:x0+(k+1)*xl]
    #     if True:
    #         dat = dat/65536.
    #         for z in range(3):
    #             img[:,x0+k*xl:x0+(k+1)*xl,z] += (dat*col[k][z]).astype(np.uint8)
	# 		#plt.colorbar()
    im.set_array(data)
    return im,

animator = anim.FuncAnimation(fig,animate,blit=False,repeat=False,interval=1000)
plt.show()