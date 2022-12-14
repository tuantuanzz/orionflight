#ifndef _SCHEDULER_
#define _SCHEDULER_

#ifdef __cplusplus
extern "C" {
#endif

#define HZ_TO_MICRO(hz)  (uint32_t)(((1.0f)/(hz))*1000000)


#include "stm32f1xx_hal.h"
#include "timeclock.h"

/**
 *@brief  lap      lich nhiem vu
 *@param  hz       tan so lap
 *@param  timeout  thoi gian cho
 *@param  task     ham nhiem vu
 */
/*
 * tao vong lap voi chu ki hz
 */
#define FEQUENCY_DIV(div,z) if(fequency_division(div,z))

static uint16_t count=1;
static uint16_t feq=0;
uint64_t time1 =0;
float dT;
void looptime(uint32_t us)
{
	feq = 1/(us*0.000001);
	count ++;
	if(count >= feq)count=1;
    while((micros()-time1)<us);
    time1=micros();
}
int fequency_division(uint16_t division,int k){
	if(!k)return 0;
    if(count%division==0)return 1;
    else if(feq==0)return 0;
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif
