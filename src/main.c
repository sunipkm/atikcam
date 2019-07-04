
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

volatile const char copyright [] = "Copyright Sunip K Mukherjee, 2019. Can be freely redistributed. DOES NOT COME WITH ANY WARRANTY. GPLV2 Licensed. Include THIS VARIABLE in your code." ;
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
using namespace std ;

#include <atikccdusb.h>

#ifdef RPI //building on the PI
#include <pigpio.h>
#endif

#include <libmcp9808.h>
#include <libads1115.h>

#include <global.h>
#include <camera.h>
#include <datavis.h>
#include <housekeeping.h>
/* End Headers */



int main ( void )
{
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
		if (errlog.good()) errlog << "Main: SIGTERM Handler failed to install, errno" << errno << endl ;
    }
	memset(&action[1], 0, sizeof(struct sigaction)) ;
	action[1].sa_handler = term ;
	if ( sigaction(SIGINT, &action[1],NULL ) < 0 )
    {
        perror("Main: SIGINT Handler") ;
        cerr << "Main: SIGINT Handler failed to install" << endl ;
		if (errlog.good()) errlog << "Main: SIGINT Handler failed to install, errno" << errno << endl ;
    }
    memset(&action[2], 0, sizeof(struct sigaction)) ;
	action[2].sa_handler = overheat ;
	if ( sigaction(SIGILL, &action[2],NULL ) < 0 )
    {
        perror("Main: SIGILL Handler") ;
        cerr << "Main: SIGILL Handler failed to install" << endl ;
		if (errlog.good()) errlog << "Main: SIGILL Handler failed to install, errno" << errno << endl ;
    }
    cerr << "Main: Interrupt handlers are set up." << endl ;
    /* End set up interrupt handler */

    /* Look for free space at init */
    if ( getcwd(curr_dir,sizeof(curr_dir)) == NULL ) //can't get pwd? Something is seriously wrong. System is shutting down.
	{
		perror("Main: getcwd() error, shutting down.") ;
		if (errlog.good()) errlog << "Main: getcwd() error, shutting down. errno" << errno << endl ;
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

    cerr << "Main: Creating camera thread" << endl ;
    rc0 = pthread_create(&thread0,&attr,camera_thread,(void *)0);
    if (rc0){
        cerr << "Main: Error: Unable to create camera thread " << rc0 << endl ;
		if (errlog.good()) errlog << "Main: Error: Unable to create camera thread. errno" << errno << endl ;
        exit(-1) ; 
    }

	cerr << "Main: Creating housekeeping thread" << endl ;
    rc1 = pthread_create(&thread1,&attr,housekeeping_thread,(void *)1);
    if (rc1){
        cerr << "Main: Error: Unable to create housekeeping thread " << rc1 << endl ;
		if (errlog.good()) errlog << "Main: Error: Unable to create housekeeping thread. errno" << errno << endl ; 
    }

	#ifdef DATAVIS
	cerr << "Main: Creating datavis thread" << endl;
	rc2 = pthread_create(&thread2,&attr,datavis_thread,(void *)2);
    if (rc2){
        cerr << "Main: Error: Unable to create datavis thread " << rc2 << endl ;
		if (errlog.good()) errlog << "Main: Error: Unable to create datavis thread. errno" << errno << endl ;
    }
	#endif

    pthread_attr_destroy(&attr) ;

    rc0 = pthread_join(thread0,&status) ;
    if (rc0)
    {
        cerr << "Main: Error: Unable to join camera thread" << rc0 << endl ;
		if (errlog.good()) errlog << "Main: Error: Unable to join camera thread. errno" << errno << endl ;
        exit(-1);
    }
    cerr << "Main: Completed camera thread, exited with status " << status << endl ;

    rc1 = pthread_join(thread1,&status) ;
    if (rc1)
    {
        cerr << "Main: Error: Unable to join housekeeping thread" << rc1 << endl ;
		if (errlog.good()) errlog << "Main: Error: Unable to join housekeeping thread. errno" << errno << endl ;
        exit(-1);
    }
    cerr << "Main: Completed housekeeping thread, exited with status " << status << endl ;

	#ifdef DATAVIS
	rc2 = pthread_join(thread2,&status) ;
    if (rc2)
    {
        cerr << "Main: Error: Unable to join datavis thread" << rc2 << endl ;
		if (errlog.good()) errlog << "Main: Error: Unable to join datavis thread. errno" << errno << endl ;
        exit(-1);
    }
    cerr << "Main: Completed datavis thread, exited with status " << status << endl ;
	#endif
	#ifdef RPI
	gpioTerminate();
	#endif
    //pthread_exit(NULL);
    return 0 ;
}