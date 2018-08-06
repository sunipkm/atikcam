import os
import sys
import astropy.io.fits as pf
import numpy as np
import matplotlib as ml
import matplotlib.pyplot as plt

files = os.listdir('./data/')

for fname in files:
	fn = fname.split('.')
	if len(fn) == 2:
		a=pf.open('data/'+fname)
		ml.image.imsave(fname+'.png',a[0].data)
		
