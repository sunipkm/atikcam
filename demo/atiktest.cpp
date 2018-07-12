/*
 * ATIK CCD INDI Driver
 *
 * Copyright (c) 2013-2015 CloudMakers, s. r. o. All Rights Reserved.
 *
 * The code is based upon Linux library source developed by
 * Jonathan Burch, Artemis CCD Ltd. It is provided by CloudMakers
 * and contributors "AS IS", without warranty of any kind.
 */

#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fitsio.h>

#include "atikconfig.h"
#include "atikccdusb.h"

#define MAX 10
#define SIZE 100

using namespace std;

static AtikCamera *devices[MAX];

bool success;
const char* name;
CAMERA_TYPE type;
bool hasShutter;
bool hasGuidePort;
bool has8BitMode;
bool hasFilterWheel;
unsigned lineCount;
unsigned pixelCountX, pixelCountY;
double pixelSizeX, pixelSizeY;
unsigned maxBinX, maxBinY;
unsigned tempSensorCount;
float currentTemp;
float targetTemp;
COOLER_TYPE cooler;
COOLING_STATE state;
COLOUR_TYPE colour = COLOUR_NONE;
int offsetX, offsetY;
bool supportsLongExposure;
double minShortExposure, maxShortExposure;
float power;
unsigned filterCount, targetFilter, currentFilter;
bool isMoving;
unsigned short *data;
unsigned short value;
double longExposure = 2.0;
double shortExposure = 0.5;

bool testAll = true;
bool testCooler = false;
bool testST4 = false;
bool testGPIO = false;
bool testShutter = false;
bool testFilterWheel = false;
bool testShort = false;
bool testLong = false;
bool testGain = false;

unsigned subX = -1;
unsigned subY = -1;
unsigned binX = 1;
unsigned binY = 1;
bool usePreview = false;
bool use8Bit = false;
int gain = -1;
int offset = -1;
int packetSize = -1;

unsigned width, height;

void save(const char *fileName) {
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

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp("-help", argv[i])) {
      cerr << "atic_ccd_test [-help] [-cooler] [-st4] [-gpio] [-shutter] [-filterwheel] [-short exposrure_time] [-long exposrure_time] [-preview] [-8bit] [-subx value] [-suby value] [-binx value] [-biny value] [-gain value] [-offset value] [-packet size_mb]" <<endl;
      exit(0);
    } else if (!strcmp("-cooler", argv[i])) {
      testCooler = true;
      testAll = false;
    } else if (!strcmp("-st4", argv[i])) {
      testST4 = true;
      testAll = false;
    } else if (!strcmp("-gpio", argv[i])) {
      testGPIO = true;
      testAll = false;
    } else if (!strcmp("-shutter", argv[i])) {
      testShutter = true;
      testAll = false;
    } else if (!strcmp("-filterwheel", argv[i])) {
      testFilterWheel = true;
      testAll = false;
    } else if (!strcmp("-short", argv[i])) {
      shortExposure = atof(argv[++i]);
      testShort = true;
      testAll = false;
    } else if (!strcmp("-long", argv[i])) {
      longExposure = atof(argv[++i]);
      testLong = true;
      testAll = false;
    } else if (!strcmp("-preview", argv[i])) {
      usePreview = true;
    } else if (!strcmp("-8bit", argv[i])) {
      use8Bit = true;
    } else if (!strcmp("-subx", argv[i])) {
      subX = atoi(argv[++i]);
    } else if (!strcmp("-suby", argv[i])) {
      subY = atoi(argv[++i]);
    } else if (!strcmp("-binx", argv[i])) {
      binX = atoi(argv[++i]);
    } else if (!strcmp("-biny", argv[i])) {
      binY = atoi(argv[++i]);
    } else if (!strcmp("-gain", argv[i])) {
      gain = atoi(argv[++i]);
      testGain = true;
      testAll = false;
    } else if (!strcmp("-offset", argv[i])) {
      offset = atoi(argv[++i]);
      testGain = true;
      testAll = false;
    } else if (!strcmp("-packet", argv[i])) {
      packetSize = 1024 * 1024 * atoi(argv[++i]);
      if (packetSize <= 0)
        packetSize = 1024 * 1024;
    }
  }

  AtikDebug = 1;
  cerr << endl << "version " << VERSION_MAJOR << "." << VERSION_MINOR << endl;
  cerr << endl << "list --------------------" << endl << endl;
  int count = AtikCamera::list(devices, MAX);
  for (int i = 0; i < count; i++) {
    AtikCamera *device = devices[i];

    cerr << endl << "open " << device->getName() << " --------------------" << endl << endl;

    success = device->open();
    
    if (packetSize > 0)
      device->setParam(MAX_PACKET_SIZE, packetSize);

    if (success) {
      cerr << endl << "getCapabilities --------------------" << endl << endl;
      success = device->getCapabilities(&name, &type, &hasShutter, &hasGuidePort, &has8BitMode, &hasFilterWheel, &lineCount, &pixelCountX, &pixelCountY, &pixelSizeX, &pixelSizeY, &maxBinX, &maxBinY, &tempSensorCount, &cooler, &colour, &offsetX, &offsetY, &supportsLongExposure, &minShortExposure, &maxShortExposure);

      if (subX == -1)
        subX = pixelCountX;
      if (subY == -1)
        subY = pixelCountY;

      data = (unsigned short *)malloc(2*pixelCountX*pixelCountY);

      if (testAll || testShort || testLong) {
        if (success) {
          cerr << endl << "setPreviewMode --------------------" << endl << endl;
          device->setPreviewMode(usePreview);
        }
        if (success) {
          cerr << endl << "set8BitMode --------------------" << endl << endl;
          device->set8BitMode(use8Bit);
        }
      }

      if (testGain) {
        cerr << endl << "setGain --------------------" << endl << endl;
        success = device->setGain(gain, offset);
        cerr << endl << "getGain --------------------" << endl << endl;
        success = device->getGain(&gain, &offset);
      }

      if (hasShutter && (testAll || testShutter)) {
        if (success) {
          cerr << endl << "setShutter --------------------" << endl << endl;
          success = device->setShutter(true);
          usleep(1 * 1000 * 1000);
          success = device->setShutter(false);
        }
      }

      if (hasFilterWheel && (testAll || testFilterWheel)) {
      cerr << endl << "getFilterWheelStatus --------------------" << endl << endl;
        success = device->getFilterWheelStatus(&filterCount, &isMoving, &currentFilter, &targetFilter);
        if (success) {
          cerr << endl << "setFilter --------------------" << endl << endl;
          success = device->setFilter(3);
          usleep(1 * 1000 * 1000);
          while (success) {
            success = device->getFilterWheelStatus(&filterCount, &isMoving, &currentFilter, &targetFilter);
            if (!isMoving)
              break;
            usleep(1 * 1000 * 1000);
          }
          success = device->setFilter(1);
          usleep(1 * 1000 * 1000);
          while (success) {
            success = device->getFilterWheelStatus(&filterCount, &isMoving, &currentFilter, &targetFilter);
            if (!isMoving)
              break;
            usleep(1 * 1000 * 1000);
          }
        }
      }

      if (success && tempSensorCount > 0) {
        cerr << endl << "getTemperatureSensorStatus --------------------" << endl << endl;
        for (unsigned sensor = 1; success && sensor <= tempSensorCount; sensor++) {
          success = device->getTemperatureSensorStatus(sensor, &currentTemp);
        }
      }

      if (cooler == COOLER_SETPOINT && (testAll || testCooler)) {
        if (success) {
          cerr << endl << "getCoolingStatus --------------------" << endl << endl;
          success = device->getCoolingStatus(&state, &targetTemp, &power);
        }
        if (success) {
          cerr << endl << "setCooling --------------------" << endl << endl;
          success = device->setCooling(-10);
          usleep(5*1000*1000);
        }
        if (success) {
          cerr << endl << "getCoolingStatus --------------------" << endl << endl;
          success = device->getCoolingStatus(&state, &targetTemp, &power);
        }
        if (success) {
          cerr << endl << "initiateWarmUp --------------------" << endl << endl;
          success = device->initiateWarmUp();
          usleep(5*1000*1000);
        }
        if (success) {
          cerr << endl << "getCoolingStatus --------------------" << endl << endl;
          success = device->getCoolingStatus(&state, &targetTemp, &power);
        }
      }

      if (success && (testAll || testShort)) {
        cerr << endl << "readCCD (short) --------------------" << endl << endl;
        
        if (shortExposure <= maxShortExposure) {
          width = device->imageWidth(subX, binX);
          height = device->imageHeight(subY, binY);
          cerr << endl << subX/binX << "x" << subY/binY << " rounded to " << width << "x" << height << endl << endl;

          success = device->readCCD(0, 0, subX, subY, binX, binY, shortExposure);
          if (success) {
            cerr << endl << "getImage --------------------" << endl << endl;
            success = device->getImage(data, width*height);
          }
          if (success) {
            cerr << endl << "sample data..." << endl;
            for (int i=0; i<10; i++) {
              for (int j=0; j<10; j++)
                cerr << " " << data[i*height+j];
              cerr << endl;
            }
            long average = 0;
            int min = 65535;
            int max = 0;
            int count = width*height;
            for (int i=0; i<count; i++) {
              average += data[i];
              min = min < data[i] ? min : data[i];
              max = max > data[i] ? max : data[i];
  //            data[i] -= 32767u;
            }
            cerr << endl << "average = " << ((double)average/count) << " min = " << min << " max = " << max << endl;
            save("short.fits");
          }
        } else {
          cerr << "short exposure time is too long" << endl;
        }
      }

      if (success && (testAll || testLong)) {
        cerr << endl << "startExposure (long) --------------------" << endl << endl;
        if (supportsLongExposure) {
          if (hasShutter)
            success = device->setShutter(true);
          if (success) {
            success = success && device->startExposure(false);
          }
          if (success) {
            cerr << endl << "sleep --------------------" << endl << endl;
            long delay = device->delay(longExposure);
            cerr << endl << longExposure << "s delay fixed to " << delay << "us" << endl << endl;
            usleep(delay);
            cerr << endl << "readCCD --------------------" << endl << endl;

            width = device->imageWidth(subX, binX);
            height = device->imageHeight(subY, binY);
            cerr << endl << subX/binX << "x" << subY/binY << " rounded to " << width << "x" << height << endl << endl;

            success = device->readCCD(0, 0, subX, subY, binX, binY);
          }
          if (success) {
            cerr << endl << "getImage --------------------" << endl << endl;
            success = device->getImage(data, width*height);
          }
          if (success) {
            cerr << endl << "sample data..." << endl;
            for (int i=0; i<10; i++) {
              for (int j=0; j<10; j++)
                cerr << " " << data[i*height+j];
              cerr << endl;
            }
            long average = 0;
            int min = 65535;
            int max = 0;
            int count = width*height;
            for (int i=0; i<count; i++) {
              average += data[i];
              min = min < data[i] ? min : data[i];
              max = max > data[i] ? max : data[i];
            }
            cerr << endl << "average = " << ((double)average/count) << " min = " << min << " max = " << max << endl;
            save("long.fits");
          }
        } else {
          cerr << "long exposure is not supported" << endl;
        }
      }

      if (testAll || testShort || testLong) {
        if (success) {
          cerr << endl << "setPreviewMode --------------------" << endl << endl;
          device->setPreviewMode(false);
        }
        if (success) {
          cerr << endl << "set8BitMode --------------------" << endl << endl;
          device->set8BitMode(false);
        }
      }

      if (hasGuidePort && (testAll || testST4)) {
        if (success) {
          cerr << endl << "setGuideRelays --------------------" << endl << endl;
          success = device->setGuideRelays(GUIDE_NORTH);
        }
        if (success) {
          cerr << endl << "... 0.1s" << endl;
          usleep(100 * 1000);
          cerr << endl << "setGuideRelays --------------------" << endl << endl;
          success = device->setGuideRelays(GUIDE_EAST);
        }
        if (success) {
          cerr << endl << "... 0.1s" << endl;
          usleep(100 * 1000);
          cerr << endl << "setGuideRelays --------------------" << endl << endl;
          success = device->setGuideRelays(GUIDE_SOUTH);
        }
        if (success) {
          cerr << endl << "... 0.1s" << endl;
          usleep(100 * 1000);
          cerr << endl << "setGuideRelays --------------------" << endl << endl;
          success = device->setGuideRelays(GUIDE_WEST);
        }
        if (success) {
          cerr << endl << "... 0.1s" << endl;
          usleep(100 * 1000);
          cerr << endl << "setGuideRelays --------------------" << endl << endl;
          success = device->setGuideRelays(0);
        }
      }

      if (lineCount > 0 && (testAll || testGPIO)) {
        if (success) {
          cerr << endl << "getGPIO --------------------" << endl << endl;
          success = device->getGPIO(&value);
        }
        if (success) {
          cerr << endl << "setGPIODirection --------------------" << endl << endl;
          success = device->setGPIODirection(0x00);
        }
        value ^= 0x01;
        if (success) {
          cerr << endl << "setGPIO --------------------" << endl << endl;
          success = device->setGPIO(value);
        }
        if (success) {
          cerr << endl << "getGPIO --------------------" << endl << endl;
          success = device->getGPIO(&value);
        }
      }

      cerr << endl << "close --------------------" << endl << endl;
      device->close();

      if (success)
        cerr << endl << device->getName() << " test OK" << endl;
      else
        cerr << endl << device->getName() << " test failed: " << device->getLastError() << endl;
    }
    free(data);
  }
}
