import os
import sys
import astropy.io.fits as pf
import numpy as np
import matplotlib as ml
import matplotlib.pyplot as plt

if len(sys.argv) == 2 :
	fn = sys.argv[1].split()
	if fn[len(fn)-1]=='fit' :
		a=pf.open(sys.argv[1])
		ml.image.imsave(sys.argv[1]+'.png',a[1].data,vmin=0,vmax=65535,cmap='gray')
		a.close()
	sys.exit(0)

files = os.listdir('./data/')

for fname in files:
	fn = fname.split('.')
	if len(fn) == 2:
		a=pf.open('data/'+fname)
		ml.image.imsave(fname+'.png',a[1].data,vmin=0,vmax=65535,cmap='gray')
		a.close()
		
