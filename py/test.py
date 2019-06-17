import ctypes as c
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as anim
import time
import datetime
import astropy.io.fits as pf

lib = c.cdll.LoadLibrary('libatik.so')
lib.debug(c.c_bool(True))

def timenow():
    return int((datetime.datetime.now().timestamp()*1e3))

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

class AtikCam:
    def __init__(self):
        count = 0
        while count < 1 :
            count = lib.start()
            if count < 1:
                print("Camera not found... Waiting...")
                time.sleep(1)

        caps = AtikCaps() #capabilites struct
        lib.dopen() #open camera
        time.sleep(1) #wait 1 sec
        lib.getCapabilities(c.byref(caps)) #get camera capabilities
        #store capabilities in internal variables
        self.lineCount=caps.lineCount
        self.pixelCountX=caps.pixelCountX
        self.pixelCountY=caps.pixelCountY
        self.pixelSizeX=caps.pixelSizeX
        self.pixelSizeY=caps.pixelSizeY
        self.maxBinX = caps.maxBinX
        self.maxBinY = caps.maxBinY
        self.tempSensorCount = caps.tempSensorCount
        self.offsetX = caps.offsetX #changes after calibration
        self.offsetY = caps.offsetY
        self.supportsLongExposure = caps.supportsLongExposure
        self.minShortExposure = caps.minShortExposure
        self.maxShortExposure = caps.maxShortExposure
        self.maxExposure = 200.0
        self.exposure = self.maxShortExposure #init exposure
        self.subX = self.pixelCountX #full frame, changes after calibration etc
        self.subY = self.pixelCountY
        self.binX = 1 #init bins
        self.binY = 1 
        self.data = np.zeros(self.pixelCountX*self.pixelCountY,dtype=np.uint16) #internal buffer
        del caps #delete capabilities struct
        return
    
    def __del__(self):
        del self.data #delete buffer on exit
        lib.dclose()

    def snap(self):
        self.timestamp = timenow()
        if self.exposure > self.maxShortExposure:
            success = lib.startExposure(c.c_bool(False))
            if success :
                print("Long exposure started.")
            delay = lib.camdelay(c.c_double(self.exposure))
            time.sleep(delay*1e-6)
            success = lib.readCCD_long(self.offsetX,self.offsetY,self.subX,self.subY,self.binX,self.binY)
            if success:
                print("Read from CCD")

        else:
            success = lib.readCCD_short(self.offsetX,self.offsetY,self.subX,self.subY,self.binX,self.binY,c.c_double(self.exposure))
            if success:
                print("Short exposure successful")

        width = lib.imageWidth(self.subX,self.binX)
        height = lib.imageHeight(self.subY,self.binY)
        success = lib.getImage(c.c_void_p((self.data).ctypes.data),c.c_uint(width*height))
        self.width = width
        self.height = height
        if self.tempSensorCount:
            self.temperature = c.c_float()
            lib.getTemperatureSensorStatus(1,c.byref(self.temperature))
        if success:
            self.lastExpSuccess = True
            return self.data[0:width*height].reshape(height,width)
        else:
            return np.zeros((height,width),dtype=np.uint16)

    def optimumExposure(self):
        if self.lastExpSuccess == False:
            return
        result = self.exposure
        data = np.sort(self.data[0:self.width*self.height]) #small to large
        PERCENTILE = 0.9 #0.5 for median
        coord = int(PERCENTILE*(data.shape[0]-1))
        val = data[coord]

        PIX_MEDIAN = 40000
        PIX_GIVE = 5000

        if val > PIX_MEDIAN-PIX_GIVE and val < PIX_MEDIAN+PIX_GIVE:
            self.exposure = result
            return

        result = PIX_MEDIAN*self.exposure/val
        if result <= self.minShortExposure:
            self.exposure = self.minShortExposure
            return
        self.exposure = int(result*1000)*1e-3 #multiples of ms
        return

    def save(self):
        if self.lastExpSuccess == False:
            return
        hdr = pf.Header()
        hdr['Name']="Atik 414EX"
        hdr['EXPOSURE']=self.exposure
        hdr['TIMESTAMP']=self.timestamp
        hdr['TEMPERATURE']=self.temperature
        hdr['SIZE']=self.width*self.height
        hdu = pf.PrimaryHDU(self.data[0:self.width*self.height].reshape(self.height,self.width),header=hdr)
        hdul = pf.HDUList([hdu])
        hdul.writeto(str(self.timestamp)+".fit[compress]")
        return

cam = AtikCam()
fig = plt.figure(figsize=(10,6))
fig.suptitle("Timestamp: %s ms, Exposure: %f s" % ( datetime.datetime.fromtimestamp(timenow()*1e-3), 3.0))
extent = 20,1260,110,950

im = plt.imshow(np.zeros((1040,1392),dtype=np.uint16),vmin=0,vmax=65535, animated=True,extent=extent,cmap='bone')

def animate(i):
    global cam
    data = cam.snap()
    exposure = cam.exposure
    timestamp = cam.timestamp
    cam.optimumExposure()
    #cam.save()
    fig.suptitle("Timestamp: %s ms, Exposure: %f s" % ( datetime.datetime.fromtimestamp(timestamp*1e-3), exposure))
    #x0 = 0 ; x1 = 1260
    #xl = (x1 - x0)//4
    #img = np.zeros((data[110:950,0:1260].shape[0],data[110:950,0:1260].shape[1],3),dtype=np.uint8)
    #col = ((0x00,0xff,0x92),(0xff,0xe2,0x00),(0xaa,0x00,0x00),(0xff,0x00,0x00))
    #for k in range(4):
	#plt.figure(figsize=(6,12))
    #    dat = np.zeros(data[110:950,x0+k*xl:x0+(k+1)*xl].shape,dtype=np.float64)
    #    dat += data[110:950,x0+k*xl:x0+(k+1)*xl]
    #    if True:
    #        dat = dat/65536.
    #        for z in range(3):
    #            img[:,x0+k*xl:x0+(k+1)*xl,z] += (dat*col[k][z]).astype(np.uint8)
			#plt.colorbar()
    im.set_array(data)
    time.sleep(1)
    return im,

animator = anim.FuncAnimation(fig,animate,blit=False,repeat=False)
plt.show()

# caps = AtikCaps()

# print(caps.lineCount)

# lib = c.cdll.LoadLibrary('libatik.so')

# lib.debug(c.c_bool(True))

# #dev = c.c_void_p()
# #lib.start(c.byref(dev))
# lib.start()
# #lib.dopen(dev)
# lib.dopen()
# #print(dev)

# #lib.getCapabilities(dev,c.byref(caps))
# lib.getCapabilities(c.byref(caps))
# for f in AtikCaps._fields_:
# 	eval("print(caps."+f[0]+")")
	
# width = caps.pixelCountX
# height = caps.pixelCountY
# print(width)
# print(lib.readCCD_short(0,0,1392,1040,1,1,c.c_double(0.02)))
# time.sleep(1)
# dat = np.zeros(width*height,dtype=np.uint16)
# print("Here:" ,dat.shape)
# print(lib.getImage(c.c_void_p(dat.ctypes.data),c.c_uint(width*height)))
# dat=dat.reshape((1040,1392))
# print(dat.shape)
# plt.imshow(dat)
# plt.show()
# lib.dclose()

# print("Done")
