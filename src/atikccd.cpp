#include <atikccdusb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef ATIK_CCD_H
#define ATIK_CCD_H
#define MAX 1
#ifdef __cplusplus
extern "C" {
#endif //__cplusplus
    AtikCamera * devices[MAX];
    struct AtikCaps{
        unsigned lineCount;
        unsigned pixelCountX, pixelCountY;
        double pixelSizeX, pixelSizeY;
        unsigned maxBinX, maxBinY;
        unsigned tempSensorCount;
        int offsetX, offsetY;
        bool supportsLongExposure;
        double minShortExposure;
        double maxShortExposure;
    };
    #define SIZE 100

    int start(void ** dev)
    {
        //int count = AtikCamera_list(devices,MAX);
        //*dev = devices[0];
        //return count ;
        return AtikCamera_list(devices,MAX);
    }

    void debug(bool debugMode){
        AtikDebug = debugMode ;
    }

    const char * getname()
    {
        //AtikCamera * dev = static_cast<AtikCamera *>(device) ;
        //AtikCamera * dev = (devices[0]);
        return (devices[0])->getName() ;
    }

    //~ bool dopen(void * device){
        //~ //AtikCamera * dev = (devices)[0];
        //~ AtikCamera * dev = static_cast<AtikCamera *>(device) ;
        //~ return dev->open();
    //~ }
    bool dopen()
    {
		return devices[0]->open();
	}

    void dclose(){
        AtikCamera * dev = devices[0];
        //AtikCamera * dev = static_cast<AtikCamera *>(device) ;
        dev->close();
    }

    bool setParam(PARAM_TYPE code, long val)
    {
        AtikCamera * dev = devices[0];
        //AtikCamera * dev = static_cast<AtikCamera *>(device) ;
        return dev->setParam(code,val);
    }

    bool getParam(PARAM_TYPE code)
    {
        AtikCamera * dev = devices[0];
        //AtikCamera * dev = static_cast<AtikCamera *>(device) ;
        return dev -> getParam(code);
    }

    bool getCapabilities(AtikCaps * caps)
    {
        AtikCamera * dev = devices[0];//static_cast<AtikCamera *>(device);
        AtikCapabilities * cap = new AtikCapabilities ;
        const char * name = (const char *)malloc(100*sizeof(char)); CAMERA_TYPE type;
        bool status = dev -> getCapabilities(&name, &type, cap);
        printf("Reached!\n");
        caps->lineCount = cap->lineCount ;
        caps->maxBinX = cap->maxBinX;
        caps->maxBinY = cap->maxBinY;
        caps->maxShortExposure = cap->maxShortExposure;
        caps->minShortExposure = cap->minShortExposure;
        caps->offsetX = cap->offsetX;
        caps->offsetY = cap->offsetY;
        caps->supportsLongExposure = cap->supportsLongExposure;
        caps->tempSensorCount = cap->tempSensorCount;
        caps->pixelCountX = cap->pixelCountX;
        caps->pixelCountY = cap->pixelCountY;
        caps->pixelSizeX = cap->pixelSizeX;
        caps->pixelSizeY = cap->pixelSizeY;
        return status ;
    }

    bool getTemperatureSensorStatus(unsigned sensor, float * temp)
    {
        AtikCamera * dev = devices[0];
        return dev->getTemperatureSensorStatus(sensor,temp);
    }

    bool getCoolingStatus(COOLING_STATE *state, float* targetTemp, float * power)
    {
        return devices[0]->getCoolingStatus(state,targetTemp,power);
    }

    bool setCooling(float targetTemp)
    {
        return devices[0]->setCooling(targetTemp);
    }
    
    bool initiateWarmUp(){
        return devices[0]->initiateWarmUp();
    }

    bool setPreviewMode(bool useMode){
        return devices[0]->setPreviewMode(useMode);
    }

    bool set8BitMode(bool useMode){
        return devices[0]->set8BitMode(useMode);
    }

    bool setDarkFrameMode(bool useMode){
        return devices[0]->setDarkFrameMode(useMode);
    }

    bool startExposure(){
        return devices[0]->startExposure(false);
    }

    bool abortExposure(){
        return devices[0]->abortExposure();
    }

    bool readCCD_short(unsigned startX, unsigned startY, unsigned sizeX, unsigned sizeY, unsigned binX, unsigned binY, double delay)
    {
        return devices[0]->readCCD(startX,startY,sizeX,sizeY,binX,binY,delay);
    }

    bool readCCD_long(unsigned startX, unsigned startY, unsigned sizeX, unsigned sizeY, unsigned binX, unsigned binY)
    {
        return devices[0]->readCCD(startX,startY,sizeX,sizeY,binX,binY);
    }

    bool getImage(unsigned short* imgBuf, unsigned imgSize){
        return devices[0]->getImage(imgBuf,imgSize);
    }

    bool setShutter(bool open){
        return devices[0]->setShutter(open);
    }

    bool setGuideRelays(unsigned short mask){
        return devices[0]->setGuideRelays(mask);
    }

    bool setGPIODirection(unsigned short mask)
    {
        return devices[0]->setGPIODirection(mask);
    }

    bool getGPIO(unsigned short *mask){
        return devices[0]->getGPIO(mask);
    }

    bool setGPIO(unsigned short mask){
        return devices[0]->setGPIO(mask);
    }

    bool getGain(int * gain, int* offset){
        return devices[0]->getGain(gain,offset);
    }

    bool setGain(int gain, int offset){
        return devices[0]->setGain(gain,offset);
    }

    unsigned camdelay(double delay){
        return devices[0]->delay(delay);
    }

    unsigned imageWidth(unsigned width, unsigned binX)
    {
        return devices[0]->imageWidth(width,binX);
    }

    unsigned imageHeight(unsigned height, unsigned binY)
    {
        return devices[0]->imageHeight(height,binY);
    }

    unsigned getSerialNumber(void){
        return devices[0]->getSerialNumber();
    }

    unsigned getVersionMajor(void){
        return devices[0]->getVersionMajor();
    }

    unsigned getVersionMinor(void){
        return devices[0]->getVersionMinor();
    }


#ifdef __cplusplus
}
#endif //__cplusplus
#endif //ATIK_CCD_H
