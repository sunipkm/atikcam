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

# from matplotlib import rc
# rc('font',**{'family':'sans-serif','sans-serif':['Arial'],'size':28})
# ## for Palatino and other serif fonts use:
# #rc('font',**{'family':'serif','serif':['Palatino']})
# rc('text', usetex=True)

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
        ('ccdtemp',c.c_short),
        ('boardtemp',c.c_short),
        ('chassistemp',c.c_short),
        ('picdata',90480*c.c_byte),
    ]

port = 12345

print(c.sizeof(image))
fig = plt.figure(figsize=(10,6))
fig.suptitle("Timestamp: %s, exposure: %f s\nCCD Temperature: %f"%(datetime.datetime.fromtimestamp(timenow()/1e3),0,0))
extent=None#(-1230/2*0.09/3,1230*0.09/6,-812*0.09/6,812*0.09/6)
#fig.text(50,-5,r'$500 \pm 5~nm$')
#im = plt.imshow(np.zeros((260,384,3),dtype=np.uint8),vmin=0,vmax=0xff,animated=True,cmap='bone',extent=extent)
im = plt.imshow(np.zeros((260,384),dtype=np.uint8),vmin=0,vmax=255,animated=True,cmap='bone',extent=extent)
a = image()

def animate(i):
    global a
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
    img = np.array(a.picdata[0:a.pixx*a.pixy],dtype=np.uint8)
    data = np.reshape(img,(a.pixy,a.pixx))
    if i==2:
        print("Saving...")
        np.save('image',data)
    print("Frame: ",i,a.exposure, datetime.datetime.fromtimestamp(a.tnow/1e3))
    #np.save(str(timenow()),data)
    #data = cv2.resize(data,dsize=(1392,1040),cv2.INTER_CUBIC)
    fig.canvas.set_window_title("CoMIC Instrument Monitor: Frame %d"%(i))
    # plt.xticks([-15,-10,-5,0,5,10,15],[r'$-15^\circ$',r'$-10^\circ$',r'$-5^\circ$',r'$0^\circ$',r'$5^\circ$',r'$10^\circ$',r'$15^\circ$'])
    # plt.yticks([-10,-5,0,5,10],[r'$-10^\circ$',r'$-5^\circ$',r'$0^\circ$',r'$5^\circ$',r'$10^\circ$'])
    fig.suptitle("Timestamp: %s, exposure: %.3f s\nCCD Temperature: %.2f$^\circ$C, Instrument Temperature: %.2f$^\circ$C"%(datetime.datetime.fromtimestamp(a.tnow/1e3),a.exposure,a.ccdtemp/100,a.boardtemp/100))
    # plt.text(80,-10,r'$500 \pm 5~nm$')
    # plt.text(390,-10,r'$589 \pm 5~nm$')
    # plt.text(700,-10,r'$770 \pm 5~nm$')
    # plt.text(1000,-10,r'$700 \pm 5~nm$')    
    # subimg = data[140:952,70:1300]
    # img = np.zeros((data[140:952,70:1300].shape[0],data[140:952,70:1300].shape[1],3),dtype=np.uint8)
    # col = ((0x00,0xff,0x92),(0xff,0xe2,0x00),(0xff,0xff,0xff),(0xff,0x00,0x00))
    # bounds = [0,297,625,940,1230]
    # for j in range(4):
    #     dat = np.array(subimg[:,bounds[j]:bounds[j+1]],dtype=np.float64)
    #     if True:
    #         dat = dat/65535.
    #         dat -= dat.min()
    #         dat /= dat.max()
    #         for z in range(3):
    #             img[:,bounds[j]:bounds[j+1],z] += (dat*col[j][z]).astype(np.uint8)
	# # 		#plt.colorbar()
    im.set_array(data)
    print("\x1b[A""\x1b[A")
    return im,

animator = anim.FuncAnimation(fig,animate,blit=False,repeat=False,interval=500)
plt.show()