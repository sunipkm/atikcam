#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fitsio.h>

#include "atikccdusb.h"

using namespace std ;

#define MAX 1
#define SIZE 100

static AtikCamera *devices[MAX] ;

void save(const char *fileName, unsigned short * data , unsigned width, unsigned height) {
  fitsfile *fptr;
  int status = 0, bitpix = USHORT_IMG, naxis = 2;
  int bzero = 32768, bscale = 1;
  long naxes[2] = { (long)width, (long)height };
  unlink(fileName);
  if (!fits_create_file(&fptr, fileName, &status)) {
    fits_create_img(fptr, bitpix, naxis, naxes, &status);
    fits_write_key(fptr, TSTRING, "PROGRAM", (void *)"atik_ccd_test", NULL, &status);
    fits_write_key(fptr, TUSHORT, "BZERO", &bzero, NULL, &status);
    fits_write_key(fptr, TUSHORT, "BSCALE", &bscale, NULL, &status);
    long fpixel[] = { 1, 1 };
    fits_write_pix(fptr, TUSHORT, fpixel, width*height, data, &status);
    fits_close_file(fptr, &status);
    cerr << endl << "saved to " << fileName << endl << endl;
  }
}

int main ( void )
{
	AtikDebug = true ;
	cerr << "Devlist: " << endl ;
	int count = AtikCamera::list(devices,MAX) ;
	for ( int i = 0 ; i < count ; i++ )
	{
		AtikCamera * device = devices [ i ] ;
		cerr << "Open: " << device -> getName() << endl ;
		bool success = device -> open() ;

		if ( ! success )
			exit(0) ;

		AtikCapabilities * devcap = new AtikCapabilities ;

		cerr << "getCapabilities: " << endl ;
		const char * devname ; CAMERA_TYPE type ;
		success = device -> getCapabilities( &devname , &type , devcap ) ;

		if ( ! success )
			exit(0) ;

		if ( devcap -> tempSensorCount > 0 )
		{
			cerr << "Temperature Sensor: " << endl ;
			for ( unsigned sensor = 1 ; success && sensor <= devcap -> tempSensorCount ; sensor ++ ){
				float temp ;
				success = device -> getTemperatureSensorStatus(sensor,&temp) ;
				cerr << "Sensor: " << sensor << ", Temp: " << temp << endl ;
			}
		}

		unsigned width = device -> imageWidth(devcap->pixelCountX,1) ;
		unsigned height = device -> imageHeight(devcap->pixelCountY,1) ;

		cerr << "Exposing whole sensor: " << endl ;

		success = device->readCCD(0,0,devcap->pixelCountX,devcap->pixelCountY,1,1,0.01) ; //10ms exposure

		if ( ! success )
			exit(0) ;

		unsigned short * data = ( unsigned short * ) malloc ( width * height * sizeof ( unsigned short ) ) ;
		success = device -> getImage(data,width*height) ;


		if ( ! success )
			exit(0) ;

		save("short.fits",data,width,height) ;

		free(data) ;
	}
	return 0 ;
}