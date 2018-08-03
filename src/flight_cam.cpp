/** Comments inside binary **/
volatile const char notice1 [] = "Cubesat designed by Sunip K [optics, electronics, mechanical], John Scudder, Jason Martel [mechanical], Jeff Baumgardner, Chris Mendillo [optical basics], Tim Cook, Supriya Chakrabarti [general help and sponsors]. Also our dear families who have supported us, including Soma M., Subir M., Nabanita D." ;
volatile const char notice2 [] = "Flight Code written by Sunip K."
"Functionalities:"
"1. Set exposure to obtain image where the median pixel value is set to 10000 (max 65535)."
"2. Log temperature of the CCD camera in the log file defined by the compiler option 'TEMPLOG_LOCATION'."
"3. Continualy keep exposing the CCD at an interval of at least 10s."
"4. Export (long) timestamp, (double) exposure, (unsigned short) pixX, (unsigned short) pixY , (unsigned short *) pixdatastream, (unsigned char) 0x00 [terminator byte] and save into a file named by the timestamp."
"5. Apply quicksort on the data, find the median and round exposure time according to the median to integral multiple of minimum short exposure."
"6. Repeat."
"7. In case any of the stages return failure, it keeps looking for the camera every second and logs camera missing info in a separate log file." ; 


volatile const char temperature_comment [] = "Temperature data: File format is: (byte) sensor number + (long) timestamp +(float) temperature + (byte) 0x00 [separator]. The data is NOT compressed. The reason to not compress the data is to simply save processor cycle and context switches. Also that data is not supposed to be oversized." ;

volatile const char pixdata_comment [] = "Pic Data: File format is: (long) timestamp + (float) exposure + (unsigned short) pixX + (unsigned short) pixY + (unsigned short [pixX * pixY ]) pixdatastream + (unsigned char) 0x00. The data is compressed in GZip." ;

volatile const char copyright [] = "Copyright Sunip K Mukherjee, 2018. Can be freely redistributed. DOES NOT COME WITH ANY WARRANTY. GPLV2 Licensed. Include THIS VARIABLE in your code." ;
/****************************/

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
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

#include "gzstream.h"

#include "atikccdusb.h"

#define MAX 1

#ifndef TIME_WAIT_USB
#define TIME_WAIT_USB 1000000
#endif

#ifndef PIC_TIME_GAP
#define PIC_TIME_GAP 10.0 // minimum gap between images in seconds
#endif

#ifndef PIX_MEDIAN
#define PIX_MEDIAN 10000.0
#endif

#ifndef PIX_GIVE
#define PIX_GIVE 5000.0
#endif

using namespace std ;

/** Globals **/

static AtikCamera *devices[MAX] ;

volatile sig_atomic_t done = 0 ; //interrupt handler

double minShortExposure = -1 ;
double maxShortExposure = -1 ;

char curr_dir[PATH_MAX] ;

struct statvfs * fsinfo ;

/*************/

void term (int signum)
{
	done = 1 ;
	cout << "In " << __FUNCTION__ << ": Received Signal: 0x" << hex << signum << dec << endl ;
	return ;
}

char space_left(void) ;

void sys_poweroff() ;

void sys_reboot() ;

int compare ( const void * a , const void * b)
{
	return ( * ( (unsigned short *) a ) - * ( (unsigned short *) b ) ) ;
}

long timenow()
{
	return ((std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now())).time_since_epoch())).count()) ;
}

double find_optimum_exposure ( unsigned short * , unsigned int , double ) ;
// returns exposure based on median pixel value defined in the macro PIX_MEDIAN. If shorter than minShortExposure, returns -1.

bool snap_picture ( AtikCamera * device , unsigned pixX , unsigned pixY , unsigned short * data , double exposure  ) ;

int main ( void )
{
	/** Interrupt Handlers **/

	struct sigaction action[2] ;
	memset(&action[0], 0, sizeof(struct sigaction)) ;
	action[0].sa_handler = term ;
	sigaction(SIGTERM, &action[0],NULL ) ;

	memset(&action[1], 0, sizeof(struct sigaction)) ;
	action[1].sa_handler = term ;
	sigaction(SIGINT, &action[1],NULL ) ;
	
	#ifdef SK_DEBUG
	cerr << "Info: Set up interrupt handlers." << endl ;
	#endif

	/************************/

	/** Check if we have any free space to operate. If not, shut down. **/	
	if ( getcwd(curr_dir,sizeof(curr_dir)) == NULL ) //can't get pwd? Just die.
	{
		perror("getcwd() error, shutting down for good.\n") ;
		sys_poweroff() ;
		return 1 ;
	}
	
	#ifdef SK_DEBUG
	cerr << "Info: PWD: " << curr_dir << endl ;
	#endif

	fsinfo = new struct statvfs ;
	if ( statvfs ( curr_dir , fsinfo ) == 0 )
	{
		unsigned long long free_space = fsinfo -> f_bsize * fsinfo -> f_bavail ;
		if ( free_space < 1 * 1024 * 1024 )
		{
			perror("Not enough free space. Shutting down.\n") ;
			sys_poweroff() ;
			return 1 ;
		}
		#ifdef SK_DEBUG
		cerr << "Info: Free Space: << free_space / ( 1024 * 1024 ) << "MiB" << endl ;
		#endif
	}
	/********************************************************************/

	/** Atik Debug Messages (prints to stdout/stderr) **/
	#ifdef ATIK_CAM_DEBUG
	AtikDebug = true ;
	#else
	AtikDebug = false ;
	#endif //ATIK_CAM_DEBUG
	/********************************************/

	/** Atik Camera Temperature Log **/
	ofstream templog ;
	#ifndef TEMPLOG_LOCATION
	#define TEMPLOG_LOCATION "temp_log.bin"
	#endif

	templog.open( TEMPLOG_LOCATION , ios::binary | ios::app ) ;
	bool templogstat = true ;
	if ( !templog.good() )
	{
		cerr << "Error: Unable to open temperature log stream." << endl ;
		templogstat = false ;
	}
	
	#ifdef SK_DEBUG
	cerr << "Info: Opened temperature log file" << endl ;
	#endif
	/*********************************/

	/** Camera Missing Log **/
	ofstream camlog ;
	#ifndef CAMLOG_LOCATION
	#define CAMLOG_LOCATION "cam_log.bin"
	#endif

	camlog.open(CAMLOG_LOCATION,ios::binary | ios::app) ;
	if ( !camlog.good() )
	{
		cerr << "Error: Unable to open camera log stream." << endl ;
	}
	#ifdef SK_DEBUG
	cerr << "Info: Opened camera log file." << endl ;
	#endif
	/************************/

	/** Error Log **/
	ofstream errlog ;
	#ifndef ERRLOG_LOCATION
	#define ERRLOG_LOCATION "err_log.txt"
	#endif

	errlog.open(ERRLOG_LOCATION,ios::app) ;
	if(!errlog.good())
	{
		cerr << "Error: Unable to open error log stream." << endl ;
	}

	/***************/

	unsigned char firstrun = 1 ;
	do {
		if ( ! firstrun ){
			#ifdef SK_DEBUG
			cerr << "Camera not found. Waiting " << TIME_WAIT_USB / 1000000 << " s..." << endl ;
			#endif
			usleep ( TIME_WAIT_USB ) ; //spend 1 seconds between looking for the camera every subsequent runs
		}
		int count = AtikCamera::list(devices,MAX) ;
		#ifdef SK_DEBUG
		cerr << "List: " << count << " number of devices." << endl ;
		#endif
		if ( ! count )
		{
			camlog << timenow() << (unsigned char) 0x00 ;
		}
		volatile bool success1 , success2 ; //two values used by two threads
		while ( count-- ) //loop so that I can break whenever ANY error is detected (probably means camera disconnected)
		{
			AtikCamera * device = devices[0] ; //first camera is our camera

			success1 = device -> open() ; //open device for access

			if ( !success1 ){ //if failed
				#ifdef SK_DEBUG
				cerr << __FILE__ << ":" << __LINE__ << ":device->open()" << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": Error: Failed to open device for first time." << endl ;
				break ; //get out and fall back to the main loop
			}

			#ifdef SK_DEBUG
			cerr << "Name: " << device -> getName() << endl ;
			#endif

			AtikCapabilities * devcap = new AtikCapabilities ;
			const char * devname ; CAMERA_TYPE type ;

			success1 = device -> getCapabilities(&devname, &type, devcap) ;

			if ( !success1 ){ //if failed
				#ifdef SK_DEBUG
				cerr << __FILE__ << ":" << __LINE__ << ":device->getCapabilities()" << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": Error: Failed to get device capabilities." << endl ;
				break ; //get out and fall back to the main loop
			}
			else {
				#ifdef SK_DEBUG
				cerr << "Device: " << "Returned Capabilities" << endl ;
				#endif //SK_DEBUG
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

			#ifdef SK_DEBUG
			cerr << "Device: AtikCapabilities:" << endl ;
			cerr << "Pixel Count X: " << pixelCX << "; Pixel Count Y: " << pixelCY << endl ;
			cerr << "Pixel Size X: " << pixelSX << " um; Pixel Size Y: " << pixelSY << " um" << endl ;
			cerr << "Max Bin X: " << maxBinX << "; Max Bin Y: " << maxBinY << endl ;
			cerr << "Temperature Sensors: " << tempSensCount << endl ;
			cerr << "Offset X: " << offsetX << "; Offset Y: " << offsetY << endl ;
			cerr << "Long Exposure Mode Supported: " << longExpMode << endl ;
			cerr << "Minimum Short Exposure: " << minShortExposure << " ms" << endl ;
			cerr << "Maximum Short Exposure: " << maxShortExposure << " ms" << endl ;
			#endif //SK_DEBUG

			if ( minShortExposure > maxShortExposure )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Minimum short exposure > Maximum short exposure. Something wrong with camera. Breaking and resetting." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Minimum short exposure > Maximum short exposure. Something wrong with camera. Breaking and resetting." << endl ;
				delete[] devcap ;
				break ;
			}

			double exposure = maxShortExposure ; // prepare for first exposure to set baseline

			unsigned short * picdata = NULL ;

			unsigned imgsize = pixelCX*pixelCY ;

			picdata   = new unsigned short[imgsize] ;

			if ( picdata == NULL )
			{
				cerr << "Fatal Error: Could not allocate memory for picture buffer." << endl ;
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": Fatal Error: Failed to allocate memory for pixel data. Rebooting..." << endl ;
				sys_reboot() ;
				return 1 ;
			}

			success2 = true ; //just to initiate the for loop...

			if ( templogstat )
			{
				for ( unsigned sensor = 1 ; success2 && sensor <= tempSensCount ; sensor ++ )
				{
					float temp ; success2 = device -> getTemperatureSensorStatus(sensor,&temp) ;
					templog << (unsigned char) sensor << timenow() << temp << (unsigned char) 0x00 ;
				}	
			}

			/** Take the first picture! **/
			#ifdef SK_DEBUG
			cerr << "Info: Preparing to take first exposure." << endl ;
			#endif
			long tnow = timenow() ; //measured time
			success1 = device -> readCCD ( 0 , 0 , pixelCX , pixelCY , 1 , 1 , maxShortExposure ) ;
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
				break ;
			}

			/** Let's save the data first **/
			string gfname ;
			gfname = to_string(tnow) + ".bin.gz" ;
			ogzstream out(gfname.c_str()) ;

			if ( !out.good() )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Could not open filestream to write data to." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not open output stream. Check for storage space?" << endl ;
				delete [] picdata ;
				delete[] devcap  ;
				break ;
			}
			out << tnow << ( float ) exposure << pixelCX << pixelCY ;

			for ( unsigned i = 0 ; i < imgsize ; i++ )
				out << picdata [ i ] ;
			out.close() ;

			if ( !out.good() )
			{
				#ifdef SK_DEBUG
				cerr << "Error: Something went wrong when writing the first exposure." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not succesfully write the first image to disk." << endl ;
				delete [] picdata ;
				delete[] devcap ;
				break ;
			}
			/*****************************/

			/** Exposure Determination Routine **/
			exposure = find_optimum_exposure(picdata,imgsize,exposure) ;
			if ( exposure < 0 ) //too bright
			{
				#ifdef SK_DEBUG
				cerr << "OpticsError: Too bright surroundings. Exiting for now." << endl ;
				#endif
				errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "OpticsError: Too bright surroundings. Exiting for now." << endl ;
				delete [] picdata ;
				delete[] devcap ;
				break ;
			}

			/************************************/
			omp_set_num_threads( 2 ) ;
			success2 = true ;
			while ( ( ! done) && success1 && space_left() > 0 )
			{
				double old_exposure = exposure ;
				/** Taking Picture and logging temperature **/
				if ( exposure <= maxShortExposure )
				{
					tnow = timenow() ;
					success1 = snap_picture(device,pixelCX,pixelCY,picdata,exposure) ;
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
							float temp ; success2 = device -> getTemperatureSensorStatus(sensor,&temp) ;
							templog << (unsigned char) sensor << timenow() << temp << (unsigned char) 0x00 ;
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
					volatile bool pic_taken = false ;
					#pragma omp parallel default(shared)//take pictures and temperature readings
					{
						if ( omp_get_thread_num( ) == 0 ) //first thread to take pictures
						{	
							tnow = timenow() ;
							success1 = snap_picture ( device,pixelCX,pixelCY,picdata,exposure ) ;
							pic_taken = true ;
							if ( ! success1 )
							{
								#ifdef SK_DEBUG
								cerr << "[" << tnow << "] Error: Could not snap picture." << endl ; 
								#endif
								errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not snap picture." << endl ;
							}
						}
						if ( omp_get_thread_num( ) == 1 ) //second thread to take images
						{
							while(!pic_taken)
							{
								if ( templogstat )
								{
									for ( unsigned sensor = 1 ; success2 && sensor <= tempSensCount ; sensor ++ )
									{
										float temp ; success2 = device -> getTemperatureSensorStatus(sensor,&temp) ;
										templog << (unsigned char) sensor << timenow() << temp << (unsigned char) 0x00 ;
									}
								}
								usleep ( 1000 ) ;
							}
						}
					}
				}
				/** End Taking picture and logging temperature **/

				/** Post-processing **/
				gfname = to_string(tnow) + ".bin.gz" ;
				out.open(gfname.c_str()) ;

				if ( !out.good() )
				{
					#ifdef SK_DEBUG
					cerr << "Error: Could not open filestream to write data to." << endl ;
					#endif
					errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not open output stream. Check for storage space?" << endl ;
					delete [] picdata ;
					delete[] devcap  ;
					break ;
				}
				out << tnow << ( float ) exposure << pixelCX << pixelCY ;

				for ( unsigned i = 0 ; i < imgsize ; i++ )
					out << picdata [ i ] ;
				out.close() ;

				if ( !out.good() )
				{
					#ifdef SK_DEBUG
					cerr << "Error: Something went wrong when writing the first exposure." << endl ;
					#endif
					errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "Error: Could not succesfully write the first image to disk." << endl ;
					delete [] picdata ;
					delete[] devcap ;
					break ;
				}
				/*****************************/

				/** Exposure Determination Routine **/
				exposure = find_optimum_exposure(picdata,imgsize,exposure) ;
				if ( exposure < 0 ) //too bright
				{
					#ifdef SK_DEBUG
					cerr << "OpticsError: Too bright surroundings. Exiting for now." << endl ;
					#endif
					errlog << "[" << timenow() << "]" << __FILE__ << ": " << __LINE__ << ": " << "OpticsError: Too bright surroundings. Exiting for now." << endl ;
					delete [] picdata ;
					delete[] devcap ;
					break ;
				}
				sync() ;
				if ( old_exposure < PIC_TIME_GAP ) //sleep for rest of the time if on shorter than PIC_TIME_GAP (s) exposure
					usleep((long)(PIC_TIME_GAP-old_exposure)*1000000) ;
				/************************************/
				/*********************/

			} //loop 3

			delete [] picdata ;
			delete[] devcap ;
		} //loop 2
		firstrun = 0 ;
	} while ( ! done ) ; //loop 1


	/** Final Cleanup **/
	delete fsinfo ;
	camlog .close() ;
	templog.close() ;
	errlog .close() ;
	/*******************/
	
	return 0 ;
}




double find_optimum_exposure ( unsigned short * picdata , unsigned int imgsize , double exposure )
{
	double result = exposure ;
	double val ;
	qsort(picdata,imgsize,sizeof(unsigned short),compare) ;
	if ( imgsize && 0x01 )
		val = ( picdata[imgsize/2] + picdata[imgsize/2+1] ) * 0.5 ;
	else
		val = picdata[imgsize/2] ;

	#ifndef PIX_MEDIAN
	#define PIX_MEDIAN 10000.0
	#endif

	#ifndef PIX_GIVE
	#define PIX_GIVE 5000.0
	#endif

	if ( val > PIX_MEDIAN - PIX_GIVE && val < PIX_MEDIAN + PIX_GIVE /* && PIX_MEDIAN - PIX_GIVE > 0 && PIX_MEDIAN + PIX_GIVE < 65535 */ )
		return result ;

	/** If calculated median pixel is within PIX_MEDIAN +/- PIX_GIVE, return current exposure **/

	result = PIX_MEDIAN * exposure / val ;

	if ( result < minShortExposure )
		return -1 ;
	unsigned long mult = ( unsigned long ) result / minShortExposure ;
	result = mult * minShortExposure ;
	return result ;
}

bool snap_picture ( AtikCamera * device , unsigned pixX , unsigned pixY , unsigned short * data , double exposure  )
{
	bool success ;
	if ( exposure <= maxShortExposure )
	{
		#ifdef SK_DEBUG
		cerr << "Info: Exposure time less than max short exposure, opting for short exposure mode." << endl ;
		#endif
		success = device -> readCCD(0,0,pixX,pixY,1,1,exposure) ;
		if ( ! success )
		{	
			#ifdef SK_DEBUG
			cerr << "Error: Short exposure failed." << endl ;
			#endif
			return success ;
		}
	}
	else if ( exposure > maxShortExposure )
	{
		#ifdef SK_DEBUG
		cerr << "Info: Exposure time greater than max short exposure, opting for long exposure mode." << endl ;
		#endif
		success = device ->startExposure(false) ; //false for some gain mode thing
		if ( ! success )
		{	
			#ifdef SK_DEBUG
			cerr << "Error: Failed to start long exposure." << endl ;
			#endif
			return success ;
		}
		long delay = device -> delay(exposure) ;
		#ifdef SK_DEBUG
		cerr << "Info: Long exposure delay set to " << delay << " ms." << endl ;
		#endif
		usleep(delay) ;
		success = device -> readCCD(0,0,pixX,pixY,1,1) ;
		if ( ! success )
		{	
			#ifdef SK_DEBUG
			cerr << "Error: Failed to stop long exposure." << endl ;
			#endif
			return success ;
		}

	}
	else return false ;
	success = device -> getImage(data,pixX*pixY) ;
	return success ;
}

void sys_poweroff(void)
{
	sync() ;
	setuid(0) ;
	reboot(LINUX_REBOOT_CMD_POWER_OFF) ;
}

void sys_reboot(void)
{
	sync() ;
	setuid(0) ;
	reboot(RB_AUTOBOOT) ;
}

char space_left(void)
{
	if ( statvfs ( curr_dir , fsinfo ) == 0 )
	{
		unsigned long long free_space = fsinfo -> f_bsize * fsinfo -> f_bavail ;
		if ( free_space < 1 * 1024 * 1024 )
		{
			#ifdef SK_DEBUG
			cerr << "Error: Not enough space." << endl ;
			#endif
			return 0x00 ;
		}
		else
			return 0x01 ;
	}
	else return -1 ; //extreme error!
}
