import ctypes as c
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as anim
import time
import datetime
import astropy.io.fits as pf
import cv2
import socket
import sys

if len(sys.argv)<2:
    print("Invocation: python client.py <Server IP>")
    sys.exit()

ip = sys.argv[1]

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

port = 12376

fig = plt.figure(figsize=(10,6))
fig.suptitle("Timestamp: %s, exposure: %f s\nCCD Temperature: %f"%(datetime.datetime.fromtimestamp(timenow()/1e3),0,0))

im = plt.imshow(np.zeros((812,1230,3),dtype=np.uint8),vmin=0,vmax=0xff,animated=True,cmap='bone')
a = image()

def animate(i):
    global a
    print("Frame: ",i)
    k = i
    val = ''.encode('utf-8')
    #print("Receiving %d packets:"%(1))
    for j in range(1):
        s = socket.socket()
        temp = ''.encode('utf-8')
        #print("Socket created successfully")
        while True:
            try:
                s.connect((ip,port))
                break
            except Exception:
                continue
        try:
            temp = s.recv(c.sizeof(image),socket.MSG_WAITALL)
        except Exception:
            pass
        if (len(temp)!=c.sizeof(image)):
            print("Received: ",len(val))
        s.close()
        val += temp
    c.memmove(c.addressof(a),val,c.sizeof(image))
    img = np.array(a.picdata[0:a.imgsize])
    data = np.reshape(img,(a.pixy,a.pixx))
    # if i==2:
    #     print("Saving...")
    #     np.save('image',data)
    print(a.exposure, datetime.datetime.fromtimestamp(a.tnow/1e3))
    #np.save(str(timenow()),data)
    #data = cv2.resize(data,dsize=(1392,1040),cv2.INTER_CUBIC)
    fig.suptitle("Timestamp: %s, exposure: %f s\nCCD Temperature: %f"%(datetime.datetime.fromtimestamp(a.tnow/1e3),a.exposure,a.ccdtemp/100))
    # x0 = 0 ; x1 = 1260
    # xl = (x1 - x0)//4
    subimg = data[140:952,70:1300]
    img = np.zeros((data[140:952,70:1300].shape[0],data[140:952,70:1300].shape[1],3),dtype=np.uint8)
    col = ((0x00,0xff,0x92),(0xff,0xe2,0x00),(0xff,0xff,0xff),(0xff,0x00,0x00))
    bounds = [0,297,625,940,1230]
    for j in range(4):
        dat = np.array(subimg[:,bounds[j]:bounds[j+1]],dtype=np.float64)
        if True:
            dat = dat/65535.
            dat -= dat.min()
            dat /= dat.max()
            for z in range(3):
                img[:,bounds[j]:bounds[j+1],z] += (dat*col[j][z]).astype(np.uint8)
	# 		#plt.colorbar()
    im.set_array(img)
    return im,

animator = anim.FuncAnimation(fig,animate,blit=False,repeat=False,interval=1000)
plt.show()