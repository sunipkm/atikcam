/** Comments inside binary **/
volatile const char notice1 [] = "Cubesat designed by Sunip K [optics, electronics, mechanical], John Scudder, Jason Martel [mechanical], Jeff Baumgardner, Chris Mendillo [optical basics], Tim Cook, Supriya Chakrabarti [general help and sponsors]. Also our dear families who have supported us, including Soma M., Subir M., Nabanita D." ;
volatile const char notice2 [] = "Flight Code written by Sunip K."
"Functionalities:"
"1. Set exposure to obtain image where the median pixel value is set to 10000 (max 65535)."
"2. Log temperature of the CCD camera in the log file defined by the compiler option 'TEMPLOG_LOCATION'."
"3. Continualy keep exposing the CCD at an interval of at least 10s."
"4. Save image as <timestamp>.FITS (uncompressed)."
"5. Apply quicksort on the data, find the median and round exposure time according to the median to integral multiple of minimum short exposure."
"6. Repeat."
"7. In case any of the stages return failure, it keeps looking for the camera every second and logs camera missing info in a separate log file." ; 


volatile const char temperature_comment [] = "Temperature data: File format is: (byte) sensor number + (long) timestamp +(float) temperature + (byte) 0x00 [separator]. The data is NOT compressed. The reason to not compress the data is to simply save processor cycle and context switches. Also that data is not supposed to be oversized." ;

volatile const char pixdata_comment [] = "Pic Data: FITS file chosen in favor of portability. Includes as keys timestamp, image x and y dimensions, exposure time (float)." ;

volatile const char copyright [] = "Copyright Sunip K Mukherjee, 2018. Can be freely redistributed. DOES NOT COME WITH ANY WARRANTY. GPLV2 Licensed. Include THIS VARIABLE in your code." ;
/****************************/

/* Headers */
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <string>
#include <chrono>
#include <thread>
#include <pthread.h> //posix threads for camera control and temperature monitoring
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/statvfs.h>
#include <limits.h>
#include <omp.h>
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <fitsio.h> //CFITSIO for saving files

#include <boost/filesystem.hpp> //boost library for filesystem support
//using namespace boost::filesystem ;
using namespace std ;
#include <atikccdusb.h>

#ifdef RPI //building on the PI
#include <pigpio.h>
#endif

/* End Headers */

/* Globals */
#define MAX 1

#ifndef MAX_ALLOWED_EXPOSURE
#define MAX_ALLOWED_EXPOSURE 130
#endif

#ifndef TIME_WAIT_USB
#define TIME_WAIT_USB 1000000
#endif

#ifndef PIC_TIME_GAP
#define PIC_TIME_GAP 10.0 // minimum gap between images in seconds
#endif

#ifndef PIX_MEDIAN
#define PIX_MEDIAN 45000.0
#endif

#ifndef PIX_GIVE
#define PIX_GIVE 5000.0
#endif

#ifndef PIX_BIN
#define PIX_BIN 1
#endif

static AtikCamera *devices[MAX] ;
bool gpio_status;
volatile sig_atomic_t done = 0 ; //global interrupt handler
volatile bool ccdoverheat = false ; //global ccd temperature status

double minShortExposure = -1 ;
double maxShortExposure = -1 ;

unsigned pix_bin = PIX_BIN ;

char curr_dir[PATH_MAX] ;

struct statvfs * fsinfo ;

ofstream templog ;
ofstream camlog ;
ofstream errlog ;

volatile int boardtemp , chassistemp ; //globals that both threads can access

#ifndef PORT
#define PORT 12376
#endif //PORT

#ifndef SERVER_IP
#define SERVER_IP "192.168.1.2"
#endif //SERVER_IP
/* End Globals */

/* Internal data structure */
typedef struct image {
	uint64_t tnow ; // timestamp in ms (from epoch, in local time)
	float exposure ; //exposure length in seconds
	unsigned short pixx ; //pixel x
	unsigned short pixy ; //pixel y
    unsigned int imgsize ; //pixx*pixy
    short ccdtemp ; // temp in C = temp/100
    short boardtemp ;
    short chassistemp ;
	unsigned short picdata[1449072] ;
	unsigned char unused[6] ; //first set of padding, default for GCC -> multiple of 32
	unsigned char unused2[1792] ; //padding to round off to 708*4096 bytes
} image ; //size 708*4096
/* End internal data structure */

/* Packet Serializer */
#ifndef PACK_SIZE
#define PACK_SIZE sizeof(image)
#endif
typedef union{
	image a ;
	unsigned char buf[sizeof(image)/PACK_SIZE][PACK_SIZE];
} packetize ;
packetize global_p ;
/* Packet Serializer */

/* Housekeeping Log in binary */
typedef union flb { float f ; char b[sizeof(float)] ; } flb ;
typedef union shb { unsigned short s ; char b[sizeof(unsigned short)] ; } shb ;
typedef union llb { unsigned long long int l ; char b[sizeof(long)] ; } llb ;

inline void put_data ( ostream & str , unsigned short val )
{
	shb x ;
	x.s = val ;
	for ( char i = 0 ; i < sizeof(x.b) ; i++ )
		str << x.b[i] ;
}

inline void put_data ( ostream & str , unsigned long long int val )
{
	llb x ;
	x.l = val ;
	for ( char i = 0 ; i < sizeof(x.b) ; i++ )
		str << x.b[i] ;
}

inline void put_data ( ostream & str , float val )
{
	flb x ;
	x.f = val ;
	for ( char i = 0 ; i < sizeof(x.b) ; i++ )
		str << x.b[i] ;
}
/* End Housekeeping log */
typedef struct sockaddr sk_sockaddr; 
/* Saving FITS Image */
#ifndef NOSAVEFITS
int save(const char *fileName , image * data) {
  fitsfile *fptr;
  int status = 0, bitpix = USHORT_IMG, naxis = 2;
  int bzero = 32768, bscale = 1;
  long naxes[2] = { (long)data->pixx, (long)data->pixy };
  if (!fits_create_file(&fptr, fileName, &status)) {
	fits_set_compression_type(fptr, RICE_1, &status) ;
    fits_create_img(fptr, bitpix, naxis, naxes, &status);
    fits_write_key(fptr, TSTRING, "PROGRAM", (void *)"sk_flight", NULL, &status);
    fits_write_key(fptr, TUSHORT, "BZERO", &bzero, NULL, &status);
    fits_write_key(fptr, TUSHORT, "BSCALE", &bscale, NULL, &status);
    fits_write_key(fptr, TFLOAT, "EXPOSURE", &(data->exposure), NULL, &status);
    fits_write_key(fptr, TSHORT, "TEMP", &(data->ccdtemp), NULL, &status);
    fits_write_key(fptr, TLONGLONG, "TIMESTAMP", &(data->tnow),NULL, &status);
    long fpixel[] = { 1, 1 };
    fits_write_pix(fptr, TUSHORT, fpixel, data->imgsize, data->picdata, &status);
    fits_close_file(fptr, &status);
    cerr << endl << "Main: Camera Thread: Save: saved to " << fileName << endl << endl;
  }
  else {
	  cerr << "Error: " << __FUNCTION__ << " : Could not save image." << endl ;
	  errlog << "Error: " << __FUNCTION__ << " : Could not save image." << endl ;
  }
  return status ;
}
#else
int save(const char *fileName, image* data)
{
	return false;
}
#endif
/* End saving FITS Image */

/* Interrupt handler */
void term (int signum)
{
	done = 1 ;
	cerr << "Interrupt: Received Signal: 0x" << hex << signum << dec << endl ;
	return ;
}

void overheat(int signum)
{
    ccdoverheat = true ;
    cerr << "Interrupt: Received Signal: 0x" << hex << signum << dec << endl ;
    return ;
}
/* End Interrupt handler */

/* Shutdown and reboot */
#ifdef ENABLE_PWOFF
void sys_poweroff(void)
{
	sync() ;
	setuid(0) ;
	reboot(LINUX_REBOOT_CMD_POWER_OFF) ;
}
#else
void sys_poweroff(void)
{	
	cerr << "Info: Poweroff instruction received!" << endl ;
	exit(0);
}
#endif //ENABLE_PWOFF

#ifdef ENABLE_REBOOT
void sys_reboot(void)
{
	sync() ;
	setuid(0) ;
	reboot(RB_AUTOBOOT) ;
}
#else
void sys_reboot(void)
{
	cerr << "Info: Reboot instruction received!" << endl ;
}
#endif //ENABLE_REBOOT
/* End */

/* Calculate Space Remaining */
char space_left(void)
{
	boost::filesystem::space_info si = boost::filesystem::space(curr_dir) ;
	cerr << __FUNCTION__ << " : PWD -> " << curr_dir << endl ;
	long long free_space = (long long) si.available ;
	cerr << __FUNCTION__ << " : free_space -> " << free_space << endl ;
	if ( free_space < 1 * 1024 * 1024 )
	{
		perror("Not enough free space. Shutting down.\n") ;
		sys_poweroff() ;
		return 0 ;
	}
	else return 1 ;
}
/* End calculate space remaining */

/* Sorting */
int compare ( const void * a , const void * b)
{
	return ( * ( (unsigned short *) a ) - * ( (unsigned short *) b ) ) ;
}
/* End Sorting */

/* Time in ms from 1-1-1970 00:00:00 */
unsigned long long int timenow()
{
	return ((std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now())).time_since_epoch())).count()) ;
}
/* End time from epoch */

/* Calculate optimum exposure */
double find_optimum_exposure ( unsigned short * picdata , unsigned int imgsize , double exposure )
{
	cerr << __FUNCTION__ << " : Received exposure: " << exposure << endl ;
	double result = exposure ;
	double val ;
	qsort(picdata,imgsize,sizeof(unsigned short),compare) ;

	#ifdef MEDIAN
	if ( imgsize && 0x01 )
		val = ( picdata[imgsize/2] + picdata[imgsize/2+1] ) * 0.5 ;
	else
		val = picdata[imgsize/2] ;
	#endif //MEDIAN

	#ifndef MEDIAN
	#ifndef PERCENTILE
	#define PERCENTILE 90.0
	unsigned char direction ;
	if ( picdata[0] < picdata[imgsize-1] )
		direction = 1 ;
	else
		direction = 0 ;

	unsigned int coord = floor((PERCENTILE*(imgsize-1)/100.0)) ;
	if ( direction )
		val = picdata[coord] ;
	else
	{
		if ( coord == 0 )
			coord = 1 ;
		val = picdata[imgsize-coord] ;
	}

	cerr << "Info: " << __FUNCTION__ << "Direction: " << direction << ", Coordinate: " << coord << endl ;
	cerr << "Info: " << __FUNCTION__ << "10 values around the coordinate: " << endl ;
	unsigned int lim2 = imgsize - coord > 3 ? coord + 4 : imgsize - 1 ;
	unsigned int lim1 = lim2 - 10 ;
	for ( int i = lim1 ; i < lim2 ; i++ )
		cerr << picdata[i] << " ";
	cerr << endl ;

	#endif //PERCENTILE
	#endif //MEDIAN

	cerr << "In " << __FUNCTION__ << ": Median: " << val << endl ;

	#ifndef PIX_MEDIAN
	#define PIX_MEDIAN 40000.0
	#endif

	#ifndef PIX_GIVE
	#define PIX_GIVE 5000.0
	#endif

	if ( val > PIX_MEDIAN - PIX_GIVE && val < PIX_MEDIAN + PIX_GIVE /* && PIX_MEDIAN - PIX_GIVE > 0 && PIX_MEDIAN + PIX_GIVE < 65535 */ )
		return result ;

	/** If calculated median pixel is within PIX_MEDIAN +/- PIX_GIVE, return current exposure **/

	result = ((double)PIX_MEDIAN) * exposure / ((double)val) ;

	cerr << __FUNCTION__ << " : Determined exposure from median " << val << ": " << result << endl ;
    
    while ( result > MAX_ALLOWED_EXPOSURE && pix_bin < 4 )
    {
        result /= 2 ;
        pix_bin *= 2 ;
    }
    
    while ( result <= minShortExposure && pix_bin > 1 )
    {
        result *= 2 ;
        pix_bin /= 2 ;
    }
	if ( result <= minShortExposure )
		return minShortExposure ;
	unsigned long mult = floor ( result / minShortExposure ) ;
	result = mult * minShortExposure ;
    if ( pix_bin < 0 )
        pix_bin = 1 ;
    if ( pix_bin > 4 )
        pix_bin = 4 ;
	return result ;
}
/* End calculate optimum exposure */

/* Snap picture routine */
bool snap_picture ( AtikCamera * device , unsigned pixX , unsigned pixY , unsigned short * data , double exposure  )
{
	bool success ;
	if ( exposure <= maxShortExposure )
	{
		cerr << "Info: Exposure time less than max short exposure, opting for short exposure mode." << endl ;
		success = device -> readCCD(0,0,pixX,pixY,pix_bin,pix_bin,exposure) ;
		if ( ! success )
		{	
			cerr << "Error: Short exposure failed." << endl ;
			return success ;
		}
	}
	else if ( exposure > maxShortExposure )
	{
		cerr << "Info: Exposure time greater than max short exposure, opting for long exposure mode." << endl ;
		success = device ->startExposure(false) ; //false for some gain mode thing
		if ( ! success )
		{	
			cerr << "Error: Failed to start long exposure." << endl ;
			return success ;
		}
		long delay = device -> delay(exposure) ;
		cerr << "Info: Long exposure delay set to " << delay << " ms." << endl ;
		usleep(delay) ;
		success = device -> readCCD(0,0,pixX,pixY,pix_bin,pix_bin) ;
		if ( ! success )
		{	
			cerr << "Error: Failed to stop long exposure." << endl ;
			return success ;
		}

	}
	else return false ;
	unsigned width = device -> imageWidth(pixX,pix_bin) ;
	unsigned height = device -> imageHeight(pixY,pix_bin) ;
	success = device -> getImage(data,width*height) ;
	return success ;
}
/* End snap picture */

/* Camera thread */
void * camera_thread(void *t)
{
    /** Atik Camera Temperature Log **/
	#ifndef TEMPLOG_LOCATION
	#define TEMPLOG_LOCATION "/home/pi/temp_log.bin"
	#endif

	templog.open( TEMPLOG_LOCATION , ios::binary | ios::app ) ;
	bool templogstat = true ;
	if ( !templog.good() )
	{
		cerr << "Error: Unable to open temperature log stream." << endl ;
		templogstat = false ;
	}
	
	cerr << "Info: Opened temperature log file" << endl ;
	/*********************************/

	/** Camera Missing Log **/
	ofstream camlog ;
	#ifndef CAMLOG_LOCATION
	#define CAMLOG_LOCATION "/home/pi/cam_log.bin"
	#endif

	camlog.open(CAMLOG_LOCATION,ios::binary | ios::app) ;
	if ( !camlog.good() )
	{
		cerr << "Error: Unable to open camera log stream." << endl ;
	}
	cerr << "Info: Opened camera log file." << endl ;
	/************************/

	/** Error Log **/
	ofstream errlog ;
	#ifndef ERRLOG_LOCATION
	#define ERRLOG_LOCATION "/home/pi/err_log.txt"
	#endif

	errlog.open(ERRLOG_LOCATION,ios::app) ;
	if(!errlog.good())
	{
		cerr << "Error: Unable to open error log stream." << endl ;
	}
    /***************/

    bool cam_off = false ;
	unsigned char firstrun = 1 ;
	do {
		if ( ! firstrun ){
			cerr << "Camera not found. Waiting " << TIME_WAIT_USB / 1000000 << " s..." << endl ;
			usleep ( TIME_WAIT_USB ) ; //spend 1 seconds between looking for the camera every subsequent runs
			#ifdef RPI
			if ( cam_off )
			{
				usleep ( 1000000 * 60 ) ;
				cam_off = false ;
				gpioWrite(17,1) ;
			}
			#endif
		}
		int count = AtikCamera::list(devices,MAX) ;
		cerr << "List: " << count << " number of devices." << endl ;
		if ( ! count )
		{
			put_data(camlog, timenow()); camlog << (unsigned char) 0x00 ;
		}
		volatile bool success1 , success2 ; //two values used by two threads
		while ( count-- ) //loop so that I can break whenever ANY error is detected (probably means camera disconnected)
		{
			AtikCamera * device = devices[0] ; //first camera is our camera

			success1 = device -> open() ; //open device for access
			cerr << "Info: device -> open(): " << success1 << endl ;

			if ( !success1 ){ //if failed
				cerr << __FILE__ << ":" << __LINE__ << ":device->open()" << endl ;
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": Error: Failed to open device for first time." << endl ;
				break ; //get out and fall back to the main loop
			}

			cerr << "Name: " << device -> getName() << endl ;

			AtikCapabilities * devcap = new AtikCapabilities ;
			const char * devname ; CAMERA_TYPE type ;

			success1 = device -> getCapabilities(&devname, &type, devcap) ;

			cerr << "Info: getCapabilities: " << success1 << endl ;

			if ( !success1 ){ //if failed
				cerr << __FILE__ << ":" << __LINE__ << ":device->getCapabilities()" << endl ;
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": Error: Failed to get device capabilities." << endl ;
				device -> close() ;
				break ; //get out and fall back to the main loop
			}
			else {
				cerr << "Device: " << "Returned Capabilities" << endl ;
			}

			unsigned       pixelCX = devcap -> pixelCountX ;
			unsigned       pixelCY = devcap -> pixelCountY ;

			double         pixelSX = devcap -> pixelSizeX ;
			double         pixelSY = devcap -> pixelSizeY ;

			unsigned       maxBinX = devcap -> maxBinX ;
			unsigned       maxBinY = devcap -> maxBinY ;

			unsigned tempSensCount = devcap -> tempSensorCount ;

			int            offsetX = devcap -> offsetX ;
			int            offsetY = devcap -> offsetY ;

			bool       longExpMode = devcap -> supportsLongExposure ;

			           minShortExposure = devcap -> minShortExposure ;
			           maxShortExposure = devcap -> maxShortExposure ;

			if ( pix_bin > maxBinX || pix_bin > maxBinY )
				pix_bin = maxBinX < maxBinY ? maxBinX : maxBinY ; //smaller of the two 

			unsigned       width  = device -> imageWidth(pixelCX,pix_bin) ;
			unsigned       height = device -> imageHeight(pixelCY,pix_bin) ; 

			cerr << "Device: AtikCapabilities:" << endl ;
			cerr << "Pixel Count X: " << pixelCX << "; Pixel Count Y: " << pixelCY << endl ;
			cerr << "Pixel Size X: " << pixelSX << " um; Pixel Size Y: " << pixelSY << " um" << endl ;
			cerr << "Max Bin X: " << maxBinX << "; Max Bin Y: " << maxBinY << endl ;
			cerr << "Temperature Sensors: " << tempSensCount << endl ;
			cerr << "Offset X: " << offsetX << "; Offset Y: " << offsetY << endl ;
			cerr << "Long Exposure Mode Supported: " << longExpMode << endl ;
			cerr << "Minimum Short Exposure: " << minShortExposure << " ms" << endl ;
			cerr << "Maximum Short Exposure: " << maxShortExposure << " ms" << endl ;
			cerr << "Binned X width: " << width << endl ;
			cerr << "Binned Y height: " << height << endl ;

			if ( minShortExposure > maxShortExposure )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Minimum short exposure > Maximum short exposure. Something wrong with camera. Breaking and resetting." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Minimum short exposure > Maximum short exposure. Something wrong with camera. Breaking and resetting." << endl ;
				delete[] devcap ;
				device -> close() ;
				break ;
			}

			double exposure = maxShortExposure ; // prepare for first exposure to set baseline

			unsigned short * picdata = NULL ;

			unsigned imgsize = width * height ;

			picdata   = new unsigned short[imgsize] ;

			if ( picdata == NULL )
			{
				cerr << "Fatal Error: Could not allocate memory for picture buffer." << endl ;
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": Fatal Error: Failed to allocate memory for pixel data. Rebooting..." << endl ;
				sys_reboot() ;
				pthread_exit(NULL);
			}

			success2 = true ; //just to initiate the for loop...
            float temp ;
			if ( templogstat )
			{
				for ( unsigned sensor = 1 ; success2 && sensor <= tempSensCount ; sensor ++ )
				{
					success2 = device -> getTemperatureSensorStatus(sensor,&temp) ;
					#ifdef RPI
					if ( gpio_status )
						if ( temp > 40.0 )
						{
							gpioWrite(17,0) ;
							cerr << "Info: Turned off camera." << endl ;
							cam_off = true ;
                            ccdoverheat = true ;
						}
					#endif
					templog << (unsigned char) sensor ;
					put_data(templog,timenow());
					put_data(templog,temp);
					templog << (unsigned char) 0x00 ;

					/** FOR TESTING ONLY **/
					#ifdef TESTING
					if ( temp > 40 )
						exit(0) ;
					#endif
					/**********************/
					#ifdef SK_DEBUG
					cerr << "Info: Sensor " << sensor << ": Temp: " << temp << " C" << endl ;
					#endif
				}	
			}

			/** Take the first picture! **/
			#ifdef SK_DEBUG
			cerr << "Info: Preparing to take first exposure." << endl ;
			#endif
			unsigned long long int tnow = timenow() ; //measured time
			success1 = device -> readCCD ( 0 , 0 , pixelCX , pixelCY , pix_bin , pix_bin , maxShortExposure ) ;
			#ifdef SK_DEBUG
			cerr << "Info: Exposure Complete. Returned: " << success1 << endl ;
			#endif
			if ( ! success1 )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Could not complete first exposure. Falling back to loop 1." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not complete first exposure. Falling back to loop 1." << endl ;
				delete [] picdata ;
				delete[] devcap ;
				device -> close() ;
				break ;
			}
			success1 = device -> getImage ( picdata , imgsize ) ;
			if ( ! success1 )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Could not get data off of the camera. Falling back to loop 1." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not get data off of the camera. Falling back to loop 1." << endl ;
				delete [] picdata ;
				delete[] devcap  ;
				device -> close() ;
				break ;
			}
			#ifdef SK_DEBUG
			cerr << "Info: getImage() -> " << success1 << endl ;
			#endif

			/** Let's save the data first **/
			string gfname ;
			gfname = to_string(tnow) + ".fit[compress]" ;
			image * imgdata = new image ;
			
			imgdata -> tnow = tnow ;
			imgdata -> pixx = width ;
			imgdata -> pixy = height ;
            imgdata -> imgsize = width*height ;
			imgdata -> exposure = exposure ;
            memcpy(&(imgdata->picdata),picdata,width*height*sizeof(unsigned short));
			//(imgdata -> picdata) = picdata ; // FIX WITH MEMCPY

			if ( save(gfname.c_str(),imgdata) )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Could not open filestream to write data to." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not open output stream. Check for storage space?" << endl ;
				delete [] picdata ;
				delete[] devcap  ;
				device -> close() ;
				break ;
			}
			#ifdef SK_DEBUG
			cerr << "Info: Wrote tnow -> " << tnow << ", exposure -> " << exposure << endl ;
			#endif

			/*****************************/

			/** Exposure Determination Routine **/
			#ifdef SK_DEBUG
			cerr << "Info: Calculating new exposure." << endl ;
			cerr << "Old Exposure -> " << exposure << " ms," << endl ;
			#endif
			exposure = find_optimum_exposure(picdata,imgsize,exposure) ;
			#ifdef SK_DEBUG
			cerr << "New Exposure -> " << exposure << "ms." << endl ;
			#endif
			if ( exposure < 0 ) //too bright
			{
				#ifdef SK_DEBUG
				cerr << "OpticsError: Too bright surroundings. Exiting for now." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "OpticsError: Too bright surroundings. Exiting for now." << endl ;
				delete [] picdata ;
				delete[] devcap ;
				device -> close() ;
				break ;
			}

			#ifdef SK_DEBUG
			cerr << "Loop conditions: done " << done << " : success1 " << success1 << " : space_left() " << space_left() << endl ;
			#endif

			/************************************/
			omp_set_num_threads( 2 ) ;
			success2 = true ;
			while ( ( ! done) && success1 && (space_left() > 0 ))
			{
				#ifdef SK_DEBUG
				cerr << "Info: Entered loop mode." << endl ;
				#endif
				double old_exposure = exposure ;
				/** Taking Picture and logging temperature **/
				if ( exposure <= maxShortExposure )
				{
					#ifdef SK_DEBUG
					cerr << "Info: Loop: Short exposure mode." << endl ;
					#endif
					tnow = timenow() ;
					success1 = snap_picture(device,pixelCX,pixelCY,picdata,exposure) ;
					#ifdef SK_DEBUG
					cerr << "Info: Loop: Short: " << tnow << " obtained image." << endl ;
					#endif
					if ( ! success1 )
					{
						#ifdef SK_DEBUG
						cerr << "[" << tnow << "] Error: Single Thread Mode: Could not snap picture." << endl ; 
						#endif
						errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Single Thread Mode: Could not snap picture." << endl ;
					}
					if ( templogstat )
					{
						for ( unsigned sensor = 1 ; success2 && sensor <= tempSensCount ; sensor ++ )
						{
							success2 = device -> getTemperatureSensorStatus(sensor,&temp) ;
							templog << (unsigned char) sensor ;
							put_data(templog,timenow());
							put_data(templog,temp);
							templog << (unsigned char) 0x00 ;
							
							#ifdef RPI
							if ( gpio_status )
								if ( temp > 40.0 )
								{
									gpioWrite(17,0) ;
									cerr << "Info: Turned off camera." << endl ;
									cam_off = true ;
                                    raise(SIGILL) ; //CCD overheat is indicated by illegal instruction
								}
							#endif

							/** FOR TESTING ONLY **/
							#ifdef TESTING
							if ( temp > 40 )
								exit(0) ;
							#endif
							#ifdef SK_DEBUG
							cerr << "Info: Sensor: " << sensor << " Temp: " << temp << " C" << endl ;
							#endif
						}
						if ( ! success2 )
						{
							#ifdef SK_DEBUG
							cerr << "[" << timenow() << "]: Warning: Could not read temperature sensor." << endl ;
							#endif
							errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Warning: Could not read temperature sensor." << endl ; 
						}
					}
				}
				else
				{
					#ifdef SK_DEBUG
					cerr << "Info: Loop: Long" << endl ;
					#endif
					volatile bool pic_taken = false ;
					#pragma omp parallel default(shared)//take pictures and temperature readings
					{
						if ( omp_get_thread_num( ) == 0 ) //first thread to take pictures
						{	
							tnow = timenow() ;
							success1 = snap_picture ( device,pixelCX,pixelCY,picdata,exposure ) ;
							#ifdef SK_DEBUG
							cerr << "Info: Loop: Long: " << tnow << " obtained image." << endl ;
							#endif
							pic_taken = true ;
							if ( ! success1 )
							{
								#ifdef SK_DEBUG
								cerr << "[" << tnow << "] Error: Could not snap picture." << endl ; 
								#endif
								errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not snap picture." << endl ;
							}
						}
						if ( omp_get_thread_num( ) == 1 ) //second thread to take temperature
						{
							while(!pic_taken)
							{
								if ( templogstat )
								{
									for ( unsigned sensor = 1 ; success2 && sensor <= tempSensCount ; sensor ++ )
									{
										temp ; success2 = device -> getTemperatureSensorStatus(sensor,&temp) ;
										templog << (unsigned char) sensor ;
										put_data(templog,timenow());
										put_data(templog,temp);
										templog << (unsigned char) 0x00 ;

										/** FOR TESTING ONLY **/
										#ifdef RPI
										if ( gpio_status )
											if ( temp > 40.0 )
											{
												gpioWrite(17,0) ;
												cerr << "Info: Turned off camera." << endl ;
												cam_off = true ;
                                                raise(SIGILL) ; //CCD overheat is indicated by illegal instruction
											}
										#endif
										//#ifdef SK_DEBUG
										cerr << "Info: Sensor: " << sensor << " Temp: " << temp << " C" << endl ;
										//#endif
									}
								}
								usleep ( 100000 ) ; //100ms
							}
							#ifdef SK_DEBUG
							cerr << "Info: Sensor: 1" << " Temp: " << temp << " C" << endl ;
							#endif
						}
					}
				}
				/** End Taking picture and logging temperature **/
				#ifdef SK_DEBUG
				cerr << "Info: Picture taken. Processing." << endl ;
				#endif
				/** Post-processing **/
				gfname = to_string(tnow) + ".fit[compress]" ;
				image * imgdata = new image ;
                
                width  = device -> imageWidth(pixelCX,pix_bin) ;
                height = device -> imageHeight(pixelCY,pix_bin) ;
                
				imgdata -> tnow = tnow ;
				imgdata -> pixx = width ;
				imgdata -> pixy = height ;
                imgdata -> imgsize = width*height ;
				imgdata -> exposure = exposure ;
                imgdata -> ccdtemp = floor(temp*100) ;
                imgdata -> boardtemp = boardtemp ;
                imgdata -> chassistemp = chassistemp ;
                memcpy(&(imgdata->picdata),picdata,width*height*sizeof(unsigned short));
				
				#ifdef DATAVIS
				memcpy(&(global_p.a),imgdata,sizeof(image));
				// global_p.a.tnow = tnow ;
				// global_p.a.pixx = width ;
				// global_p.a.pixy = height ;
                // global_p.a.imgsize = width*height ;
				// global_p.a.exposure = exposure ;
                // global_p.a.ccdtemp = floor(temp*100) ;
                // global_p.a.boardtemp = boardtemp ;
                // global_p.a.chassistemp = chassistemp ;
				// cerr << "Camera: " << global_p.a.exposure << endl ;
				#endif
                if ( save(gfname.c_str(),imgdata) )
				{
					#ifdef SK_DEBUG
					cerr << "Error: Could not open filestream to write data to." << endl ;
					#endif
					errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not open output stream. Check for storage space?" << endl ;
					delete [] picdata ;
					delete [] devcap  ;
					device -> close() ;
					break ;
				}
				sync() ;
				
				#ifdef SK_DEBUG
				cerr << "Info: Loop: Wrote data to disk." << endl ;
				#endif
				/*****************************/

				/** Exposure Determination Routine **/
				#ifdef SK_DEBUG
				cerr << "Info: Loop: Old exposure: " << old_exposure << " s" << endl ;
				#endif
				exposure = find_optimum_exposure(picdata,imgsize,exposure) ;
				#ifdef SK_DEBUG
				cerr << "Info: Loop: New exposure: " << exposure << " s" << endl ;
				#endif
				if ( exposure < minShortExposure ) //too bright
				{
					#ifdef SK_DEBUG
					cerr << "OpticsError: Too bright surroundings. Setting up minimum exposure." << endl ;
					#endif
					errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "OpticsError: Too bright surroundings. Setting minimum exposure." << endl ;
					// delete [] picdata ;
					// delete[] devcap ;
					// break ;
					exposure = minShortExposure ;
				}
				if ( old_exposure < PIC_TIME_GAP ) //sleep for rest of the time if on shorter than PIC_TIME_GAP (s) exposure
					usleep((long)(PIC_TIME_GAP-old_exposure)*1000000) ;
				/************************************/
				/*********************/

			} //loop 3
			device -> close() ;
			delete [] picdata ;
			delete[] devcap ;
		} //loop 2
		firstrun = 0 ;
	} while ( ! done ) ; //loop 1
    cerr << "Camera Thread: Received kill signal" << endl ;
    camlog .close() ;
	templog.close() ;
	errlog .close() ;
    pthread_exit(NULL) ;
}
/* End camera thread */

/* Body temperature monitoring thread */
void * housekeeping_thread(void *t)
{   
    long tid = (long) t ;
    cerr << "Housekeeping: " << tid << endl ;
	while(!done)
		sleep(1);
    pthread_exit(NULL) ;
}
/* End body temperature monitoring thread */

/* Data visualization server thread */
void * datavis_thread(void *t)
{
	int server_fd, new_socket, valread; 
    struct sockaddr_in address; 
    int opt = 1; 
    int addrlen = sizeof(address);  
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        //exit(EXIT_FAILURE); 
    } 
       
    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
                                                  &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        //exit(EXIT_FAILURE); 
    } 

	struct timeval timeout;      
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt (server_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                sizeof(timeout)) < 0)
        perror("setsockopt failed\n");

    if (setsockopt (server_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                sizeof(timeout)) < 0)
        perror("setsockopt failed\n");

    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( PORT ); 

    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (sk_sockaddr *)&address,sizeof(address))<0) 
    { 
        perror("bind failed"); 
        //exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 32) < 0) 
    { 
        perror("listen"); 
        //exit(EXIT_FAILURE); 
    }
	cerr << "DataVis: Main: Server File: " << server_fd << endl ;
	while(!done)
	{
		valread = 0 ;
        char recv_buf[32] = {0} ;
		//cerr << "DataVis: " << global_p.a.exposure << endl ;
        for ( int i = 0 ; (i < sizeof(image)/PACK_SIZE) ; i++ ){
			if ( done ) break;
			cerr << "DataVis: Loop: Server File: " << server_fd << endl ;
			if ((new_socket = accept4(server_fd, (sk_sockaddr *)&address, (socklen_t*)&addrlen,SOCK_NONBLOCK))<0) 
        	{ 
            	perror("accept"); 
				cerr << "DataVis: Accept from socket error!" <<endl ;
        	}
            ssize_t numsent = send(new_socket,&global_p.buf[i],PACK_SIZE,0);
			//cerr << "DataVis: Size of sent data: " << PACK_SIZE << endl ;
			if ( numsent != PACK_SIZE ){
				perror("DataVis: Send: ");
				cerr << "DataVis: Reported sent data: " << numsent << "/" << PACK_SIZE << endl;
			}
            //cerr << "DataVis: Data sent" << endl ;
            //valread = read(sock,recv_buf,32);
            //cerr << "DataVis: " << recv_buf << endl ;
			close(new_socket);
		}
		sleep(PIC_TIME_GAP);
		cerr << "DataVis thread: Sent" << endl ;
	}
	close(server_fd);
	pthread_exit(NULL);
}
/* Data visualization server thread */
int main ( void )
{
    /* Setup GPIO */
    gpio_status = false;
    #ifdef RPI
    if ( gpioInitialise() < 0 ){
        perror("Main: GPIO Init") ;
        cerr << "Warning: pigpio init failed." << endl ;
    }
    else {
        gpio_status = true ;
        gpioSetMode(17,PI_OUTPUT) ; //12V Rail
        gpioSetMode(27,PI_OUTPUT) ; //28V Rail
    }
    #endif //RPI
    /* Set up interrupt handler */
    struct sigaction action[3] ;
	memset(&action[0], 0, sizeof(struct sigaction)) ;
	action[0].sa_handler = term ;
    if (sigaction(SIGTERM, &action[0],NULL )<0)
    {
        perror("Main: SIGTERM Handler") ;
        cerr << "Main: SIGTERM Handler failed to install" << endl ;
    }
	memset(&action[1], 0, sizeof(struct sigaction)) ;
	action[1].sa_handler = term ;
	if ( sigaction(SIGINT, &action[1],NULL ) < 0 )
    {
        perror("Main: SIGINT Handler") ;
        cerr << "Main: SIGINT Handler failed to install" << endl ;
    }
    memset(&action[2], 0, sizeof(struct sigaction)) ;
	action[2].sa_handler = overheat ;
	if ( sigaction(SIGILL, &action[2],NULL ) < 0 )
    {
        perror("Main: SIGILL Handler") ;
        cerr << "Main: SIGILL Handler failed to install" << endl ;
    }
    cerr << "Main: Interrupt handlers are set up." << endl ;
    /* End set up interrupt handler */

    /* Look for free space at init */
    if ( getcwd(curr_dir,sizeof(curr_dir)) == NULL ) //can't get pwd? Something is seriously wrong. System is shutting down.
	{
		perror("Main: getcwd() error, shutting down.") ;
		sys_poweroff() ;
		return 1 ;
	}
    cerr << "Main: PWD: " << curr_dir << endl ;
    boost::filesystem::space_info si = boost::filesystem::space(curr_dir) ;
	long long free_space = (long long) si.available ;
	if ( free_space < 1 * 1024 * 1024 )
	{
		perror("Main: Not enough free space. Shutting down.") ;
		sys_poweroff() ;
		return 1 ;
	}
    /* End look for free space at init */

    /* Set up atik camera debug */
    #ifdef ATIK_DEBUG
    AtikDebug = true ;
    #else
    AtikDebug = false ;
    #endif //ATIK DEBUG

    int rc0, rc1, rc2 ;
    pthread_t thread0 , thread1, thread2 ;
    pthread_attr_t attr ;
    void* status ;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);

    // cerr << "Main: Creating camera thread" << endl ;
    // rc0 = pthread_create(&thread0,&attr,camera_thread,(void *)0);
    // if (rc0){
    //     cerr << "Main: Error: Unable to create camera thread " << rc0 << endl ;
    //     exit(-1) ; 
    // }

	// cerr << "Main: Creating housekeeping thread" << endl ;
    // rc1 = pthread_create(&thread1,&attr,housekeeping_thread,(void *)1);
    // if (rc1){
    //     cerr << "Main: Error: Unable to create housekeeping thread " << rc1 << endl ;
    //     exit(-1) ; 
    // }

	cerr << "Main: Creating datavis thread" << endl;
	rc2 = pthread_create(&thread2,&attr,datavis_thread,(void *)2);
    if (rc2){
        cerr << "Main: Error: Unable to create datavis thread " << rc2 << endl ;
        exit(-1) ; 
    }

    pthread_attr_destroy(&attr) ;

    // rc0 = pthread_join(thread0,&status) ;
    // if (rc0)
    // {
    //     cerr << "Main: Error: Unable to join camera thread" << rc0 << endl ;
    //     exit(-1);
    // }
    // cerr << "Main: Completed camera thread, exited with status " << status << endl ;

    // rc1 = pthread_join(thread1,&status) ;
    // if (rc1)
    // {
    //     cerr << "Main: Error: Unable to join housekeeping thread" << rc1 << endl ;
    //     exit(-1);
    // }
    // cerr << "Main: Completed housekeeping thread, exited with status " << status << endl ;

	rc2 = pthread_join(thread2,&status) ;
    if (rc2)
    {
        cerr << "Main: Error: Unable to join datavis thread" << rc2 << endl ;
        exit(-1);
    }
    cerr << "Main: Completed datavis thread, exited with status " << status << endl ;
	#ifdef RPI
	gpioTerminate();
	#endif
    //pthread_exit(NULL);
    return 0 ;
}