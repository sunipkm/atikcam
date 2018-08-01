#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>

#include "atikccdusb.h"

#define MAX 1

using namespace std ;

static AtikCamera * devices [ MAX ] ;

volatile sig_atomic_t done = 0;

void term(int signum)
{
	done = 1;
    printf("In %s: Received signal: 0x%x\n" , __FUNCTION__ , signum ) ;
}

int main ( void )
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);

    struct sigaction action1;
	memset(&action1, 0, sizeof(struct sigaction));
	action1.sa_handler = term;
	sigaction(SIGINT, &action1, NULL);

	AtikDebug = true ;
	int firstrun = 1 ;
	do {
	if ( ! firstrun )
		usleep(10000000) ;
	int count = AtikCamera::list(devices,MAX) ;

	for ( int i = 0 ; i < count ; i++ )
	{
		AtikCamera * device = devices[i] ;
		cerr << "Name: " << device->getName() << endl ;

		bool success = device -> open() ;

		if ( success ) cout << "Device " << device->getName() << " opened." << endl ;

		AtikCapabilities * devcap = new AtikCapabilities ;
		const char * devname ; CAMERA_TYPE type ;

		success = device -> getCapabilities ( & devname , & type , devcap ) ;
		if ( success ) cout << "Device " << devname << " returned capabilities." << endl ;

		if ( devcap -> tempSensorCount > 0 )
			while(!done && success)
			{
				for ( unsigned sensor = 1 ; success && sensor <= devcap -> tempSensorCount ; sensor ++ )
				{
					float temp ;
					success = device -> getTemperatureSensorStatus(sensor,&temp) ;
					cout << devname << ": Sensor: " << sensor << ": Temp: " << temp << endl ;
				}
				cout << endl ;
			}
		device -> close() ;	
	}
			firstrun = 0 ;
	}while(!done) ;
}