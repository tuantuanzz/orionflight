#include "mpu6500.h"
#include "stm32f1xx_hal.h"
#include "maths.h"
#include "i2c.h"
#include "math.h"
#include "lpf.h"
#include "../flymode/quadrotor/config.h"
#include "spi.h"
#include "timeclock.h"
#include "debug.h"
/*hel*/
#define LSB_gyr  131.0f
#define GAIN 0.0005f
#define DEFAULT_SAMPLE_FREQ	1000.0f	// sample frequency in Hz
#define twoKpDef	(2.0f * 15.5f)	// 2 * proportional gain
#define twoKiDef	(2.0f * 0.0f)	// 2 * integral gain

/*******************************************/
float twoKp = twoKpDef;	// 2 * proportional gain (Kp)
float twoKi = twoKiDef;	// 2 * integral gain (Ki)
float q0 = 1.0f;
float q1 = 0.0f;
float q2 = 0.0f;
float q3 = 0.0f;
float integralFBx = 0.0f;
float integralFBy = 0.0f;
float integralFBz = 0.0f;
float anglesComputed = 0.0f;
float invSampleFreq = 1.0f / DEFAULT_SAMPLE_FREQ;

/*  configution mpu6500  */
//#define USE_LPF_1_ODER_ACC
#define ACC_FEQ_CUT  1000  //hz

static float acc_pitch_offset,acc_roll_offset;
int16_t gyr_offs_x, gyr_offs_y, gyr_offs_z;
int16_t acc_offs_x, acc_offs_y, acc_offs_z;


void get_offset(){
	static uint16_t k1,k2;
	float pitch_acc,roll_acc;
	IMU_raw_t data;
	static int32_t contan_gyro[3];
	static int32_t contan_acc[3];

	for(uint16_t i=0;i<2000;i++){

        //  acc offset
		mpu_get_acc(&data);
		if((data.accx+data.accy+data.accz)!=0)k1++;

        contan_acc[0] += data.accx;
        contan_acc[1] += data.accy;
        contan_acc[2] += data.accz;

		roll_acc   =-atan2_approx(data.accx,data.accz)*1/RAD;
		pitch_acc  = atan2_approx(data.accy,data.accz)*1/RAD;

		acc_pitch_offset += pitch_acc;
		acc_roll_offset  += roll_acc;

		// gyro offset
		mpu_get_gyro(&data);
		if((data.gyrox+data.gyroy+data.gyroz)!=0)k2++;
		contan_gyro[0] += data.gyrox;
	    contan_gyro[1] += data.gyroy;
	    contan_gyro[2] += data.gyroz;
	    delay_ms(1);
	    //HAL_Delay(1);
	}

	if(k1!=0){
	  acc_offs_x = contan_acc[0]/k1;
      acc_offs_y = contan_acc[1]/k1;
      acc_offs_z = contan_acc[2]/k1;

      acc_pitch_offset /=(float)k1;
      acc_roll_offset  /=(float)k1;
    }

    if(k2!=0){
      gyr_offs_x = contan_gyro[0]/k2;
      gyr_offs_y = contan_gyro[1]/k2;
      gyr_offs_z = contan_gyro[2]/k2;
    }
}


/* @ init mpu
 *
 *
 * */
SPI_HandleTypeDef mpu_spi_port;
I2C_HandleTypeDef mpu_i2cport;
GPIO_TypeDef *mpu_gpio_port = NULL;
uint16_t mpu_cs_pin;

const uint8_t mpu_address =(0x68<<1);

void MPU_i2c_init(I2C_HandleTypeDef *i2c){

	mpu_i2cport = *i2c;
    uint8_t buffer[6];

    buffer[0] = 0x6B;
	buffer[1] = 0x00;

	HAL_I2C_Master_Transmit(&mpu_i2cport,mpu_address,buffer,2,1);
	// Configure gyro(500dps full scale)
	buffer[0] = 0x1B;
	buffer[1] = 0x08;
	HAL_I2C_Master_Transmit(&mpu_i2cport,mpu_address,buffer,2,1);
	// Configure accelerometer(+/- 8g)
	buffer[0] = 0x1C;
	buffer[1] = 0x00;
	HAL_I2C_Master_Transmit(&mpu_i2cport,mpu_address,buffer,2,1);
	get_offset();
}

void MPU_spi_init(SPI_HandleTypeDef *spiportt,GPIO_TypeDef  *gpio_port,uint16_t pin){
	mpu_gpio_port = gpio_port;
	mpu_spi_port = *spiportt;
	mpu_cs_pin = pin;

    uint8_t data[2];
	data[0]=0x6b;
	data[1]=0x00;
	HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_RESET);
	HAL_SPI_Transmit(&mpu_spi_port,data,2,100);
	HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_SET);

	data[0]=0x1b;
	data[1]=0x00;
	HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_RESET);
	HAL_SPI_Transmit(&mpu_spi_port,data,2,100);
	HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_SET);

	data[0]=0x1c;
	data[1]=0x00;
	HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_RESET);
	HAL_SPI_Transmit(&mpu_spi_port,data,2,100);
	HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_SET);
	get_offset();

}
/**
 *  get gyro raw value
 */
void mpu_get_gyro(IMU_raw_t *k){
#ifdef MPU_VIA_I2C
	  uint8_t buffe[6];
	  buffe[0]= 0x43;// gyro address
	  HAL_I2C_Master_Transmit(&mpu_i2cport,mpu_address,buffe,1,1);
	  HAL_I2C_Master_Receive(&mpu_i2cport,mpu_address,buffe,6,1);

	  k->gyrox=(int16_t)(buffe[0]<<8)|buffe[1];
	  k->gyroy=(int16_t)(buffe[2]<<8)|buffe[3];
	  k->gyroz=(int16_t)(buffe[4]<<8)|buffe[5];
#endif
#ifdef MPU_VIA_SPI
	  uint8_t buffe[6];
	  buffe[0]= 0x43;// gyro address
	  buffe[0] |=0x80;
	  HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_RESET);
	  HAL_SPI_Transmit(&mpu_spi_port,&buffe[0],1,100);
	  HAL_SPI_Receive(&mpu_spi_port,buffe,6,100);
	  HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_SET);

	  k->gyrox=(int16_t)(buffe[0]<<8)|buffe[1];
	  k->gyroy=(int16_t)(buffe[2]<<8)|buffe[3];
	  k->gyroz=(int16_t)(buffe[4]<<8)|buffe[5];
#endif

	}

/**
 *  get acc raw value
 */
void mpu_get_acc(IMU_raw_t *k){
#ifdef MPU_VIA_I2C
	uint8_t buffe[6];
	  buffe[0] = 0x3b;// acc address
	  HAL_I2C_Master_Transmit(&mpu_i2cport,mpu_address,buffe,1,1);
	  HAL_I2C_Master_Receive(&mpu_i2cport,mpu_address,buffe,6,1);

	  k->accx=(int16_t)(buffe[0]<<8)|buffe[1];
	  k->accy=(int16_t)(buffe[2]<<8)|buffe[3];
	  k->accz=(int16_t)(buffe[4]<<8)|buffe[5];
#endif
#ifdef MPU_VIA_SPI
	  uint8_t buffe[6];
	  buffe[0] = 0x3b;// acc address
	  buffe[0] |=0x80;
	  HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_RESET);
	  HAL_SPI_Transmit(&mpu_spi_port,buffe,1,100);
	  HAL_SPI_Receive(&mpu_spi_port,buffe,6,100);
	  HAL_GPIO_WritePin(mpu_gpio_port,mpu_cs_pin,GPIO_PIN_SET);

	  k->accx=(int16_t)buffe[0]<<8|buffe[1];
	  k->accy=(int16_t)buffe[2]<<8|buffe[3];
	  k->accz=(int16_t)buffe[4]<<8|buffe[5];
#endif
}



static float Pitch_acc,Roll_acc;
static IMU_raw_t p;
void gyro_calib(float *x,float *y,float *z,uint16_t DT){
	float lsb2degre =(DT*0.000001)/LSB_gyr;
	mpu_get_gyro(&p);
    *x  = (float)(p.gyrox -gyr_offs_x)*lsb2degre;
    *y  = (float)(p.gyroy -gyr_offs_y)*lsb2degre;
    *z  = (float)(p.gyroz -gyr_offs_z)*lsb2degre;
}
void MPU_update(euler_angle_t *m,uint16_t DT){

	// gyro calibrate
	float lsb2degre =(DT*0.000001)/LSB_gyr;

	mpu_get_gyro(&p);
    m->pitch += (float)(p.gyrox -gyr_offs_x)*lsb2degre;
    m->roll  += (float)(p.gyroy -gyr_offs_y)*lsb2degre;
    m->yaw   = (float)(p.gyroz -gyr_offs_z)*lsb2degre;

	if(m->pitch>180.0f)m->pitch  -= 360.0f;
	else if(m->pitch<-180.0f)m->pitch += 360.0f;

    if(m->roll>180.0f)m->roll -= 360.0f;
    else if(m->roll<-180.0f)m->roll += 360.0f;

	//if(m->yaw>360.0f)m->yaw  -= 360.0f;
	//else if(m->yaw<0.0f)m->yaw  += 360.0f;

	m->pitch += m->roll   * sin_approx((p.gyroz -gyr_offs_z)*lsb2degre*RAD);
	m->roll  -= m->pitch  * sin_approx((p.gyroz -gyr_offs_z)*lsb2degre*RAD);

    //  acc calibrate
	mpu_get_acc(&p);
	Roll_acc   =(-atan2_approx((float)p.accx,(float)p.accz)*1/RAD - acc_roll_offset);
	Pitch_acc  = (atan2_approx((float)p.accy,(float)p.accz)*1/RAD - acc_pitch_offset);


#ifdef USE_LPF_1_ODER_ACC

	Roll_acc = pt1FilterApply(Roll_acc,ACC_FEQ_CUT,DT*0.000001);
	Pitch_acc= pt1FilterApply(Pitch_acc,ACC_FEQ_CUT,DT*0.000001);

	 print_float(Roll_acc);
	 print_char("\n");

	m->pitch +=GAIN*(Pitch_acc-m->pitch);
	m->roll  +=GAIN*(Roll_acc-m->roll);


#endif

#ifndef USE_LPF_1_ODER_ACC
	m->pitch +=GAIN*(Pitch_acc-m->pitch);
	m->roll  +=GAIN*(Roll_acc-m->roll);
	///m->yaw   +=GAIN*(MAG_yaw-m->yaw);
#endif

}

static float invSqrt_(float x)
{
	float halfx = 0.5f * x;
	float y = x;
	long i = *(long*)&y;
	i = 0x5f3759df - (i>>1);
	y = *(float*)&i;
	y = y * (1.5f - (halfx * y * y));
	y = y * (1.5f - (halfx * y * y));
	return y;
}


void mpu_get_gyro_calib(IMU_raw_t *k,uint16_t DT){
	// gyro calibrate
	float lsb2degre =(DT*0.000001)/LSB_gyr;
	mpu_get_gyro(k);
	k->gyrox += (float)(k->gyrox -gyr_offs_x)*lsb2degre;
    k->gyroy  += (float)(k->gyroy -gyr_offs_y)*lsb2degre;
    k->gyroz  += (float)(k->gyroz -gyr_offs_z)*lsb2degre;

	}
void computeAnglesFromQuaternion(euler_angle_t *m)
{
	m->roll = 57.29577*atan2_approx(q0*q1 + q2*q3, 0.5f - q1*q1 - q2*q2);
	m->pitch =  57.29577*asinf(-2.0f * (q1*q3 - q0*q2));
	m->yaw =  57.29577*atan2_approx(q1*q2 + q0*q3, 0.5f - q2*q2 - q3*q3);
	anglesComputed = 1;
}


void updateIMU(float gx, float gy, float gz, float ax, float ay, float az)
{
	float recipNorm;
	float halfvx, halfvy, halfvz;
	float halfex, halfey, halfez;
	float qa, qb, qc;

	// Convert gyroscope degrees/sec to radians/sec
	gx *= 0.0174533f;
	gy *= 0.0174533f;
	gz *= 0.0174533f;

	// Compute feedback only if accelerometer measurement valid
	// (avoids NaN in accelerometer normalisation)
	if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

		// Normalise accelerometer measurement
		recipNorm = invSqrt_(ax * ax + ay * ay + az * az);
		ax *= recipNorm;
		ay *= recipNorm;
		az *= recipNorm;

		// Estimated direction of gravity
		halfvx = q1 * q3 - q0 * q2;
		halfvy = q0 * q1 + q2 * q3;
		halfvz = q0 * q0 - 0.5f + q3 * q3;

		// Error is sum of cross product between estimated
		// and measured direction of gravity
		halfex = (ay * halfvz - az * halfvy);
		halfey = (az * halfvx - ax * halfvz);
		halfez = (ax * halfvy - ay * halfvx);

		// Compute and apply integral feedback if enabled
		if(twoKi > 0.0f) {
			// integral error scaled by Ki
			integralFBx += twoKi * halfex * invSampleFreq;
			integralFBy += twoKi * halfey * invSampleFreq;
			integralFBz += twoKi * halfez * invSampleFreq;
			gx += integralFBx;	// apply integral feedback
			gy += integralFBy;
			gz += integralFBz;
		} else {
			integralFBx = 0.0f;	// prevent integral windup
			integralFBy = 0.0f;
			integralFBz = 0.0f;
		}

		// Apply proportional feedback
		gx += twoKp * halfex;
		gy += twoKp * halfey;
		gz += twoKp * halfez;
	}

	// Integrate rate of change of quaternion
	gx *= (0.5f * invSampleFreq);		// pre-multiply common factors
	gy *= (0.5f * invSampleFreq);
	gz *= (0.5f * invSampleFreq);
	qa = q0;
	qb = q1;
	qc = q2;
	q0 += (-qb * gx - qc * gy - q3 * gz);
	q1 += (qa * gx + qc * gz - q3 * gy);
	q2 += (qa * gy - qb * gz + q3 * gx);
	q3 += (qa * gz + qb * gy - qc * gx);

	// Normalise quaternion
	recipNorm = invSqrt_(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
	q0 *= recipNorm;
	q1 *= recipNorm;
	q2 *= recipNorm;
	q3 *= recipNorm;
	anglesComputed = 0;
}

void update(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz)
{
	float recipNorm;
	float q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;
	float hx, hy, bx, bz;
	float halfvx, halfvy, halfvz, halfwx, halfwy, halfwz;
	float halfex, halfey, halfez;
	float qa, qb, qc;

	// Use IMU algorithm if magnetometer measurement invalid
	// (avoids NaN in magnetometer normalisation)
	if((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f)) {
		updateIMU(gx, gy, gz, ax, ay, az);
		return;
	}

	// Convert gyroscope degrees/sec to radians/sec
	gx *= 0.0174533f;
	gy *= 0.0174533f;
	gz *= 0.0174533f;

	// Compute feedback only if accelerometer measurement valid
	// (avoids NaN in accelerometer normalisation)
	if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

		// Normalise accelerometer measurement
		recipNorm = invSqrt_(ax * ax + ay * ay + az * az);
		ax *= recipNorm;
		ay *= recipNorm;
		az *= recipNorm;

		// Normalise magnetometer measurement
		recipNorm = invSqrt_(mx * mx + my * my + mz * mz);
		mx *= recipNorm;
		my *= recipNorm;
		mz *= recipNorm;

		// Auxiliary variables to avoid repeated arithmetic
		q0q0 = q0 * q0;
		q0q1 = q0 * q1;
		q0q2 = q0 * q2;
		q0q3 = q0 * q3;
		q1q1 = q1 * q1;
		q1q2 = q1 * q2;
		q1q3 = q1 * q3;
		q2q2 = q2 * q2;
		q2q3 = q2 * q3;
		q3q3 = q3 * q3;

		// Reference direction of Earth's magnetic field
		hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
		hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
		bx = sqrtf(hx * hx + hy * hy);
		bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));

		// Estimated direction of gravity and magnetic field
		halfvx = q1q3 - q0q2;
		halfvy = q0q1 + q2q3;
		halfvz = q0q0 - 0.5f + q3q3;
		halfwx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
		halfwy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
		halfwz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

		// Error is sum of cross product between estimated direction
		// and measured direction of field vectors
		halfex = (ay * halfvz - az * halfvy) + (my * halfwz - mz * halfwy);
		halfey = (az * halfvx - ax * halfvz) + (mz * halfwx - mx * halfwz);
		halfez = (ax * halfvy - ay * halfvx) + (mx * halfwy - my * halfwx);

		// Compute and apply integral feedback if enabled
		if(twoKi > 0.0f) {
			// integral error scaled by Ki
			integralFBx += twoKi * halfex * invSampleFreq;
			integralFBy += twoKi * halfey * invSampleFreq;
			integralFBz += twoKi * halfez * invSampleFreq;
			gx += integralFBx;	// apply integral feedback
			gy += integralFBy;
			gz += integralFBz;
		} else {
			integralFBx = 0.0f;	// prevent integral windup
			integralFBy = 0.0f;
			integralFBz = 0.0f;
		}

		// Apply proportional feedback
		gx += twoKp * halfex;
		gy += twoKp * halfey;
		gz += twoKp * halfez;
	}

	// Integrate rate of change of quaternion
	gx *= (0.5f * invSampleFreq);		// pre-multiply common factors
	gy *= (0.5f * invSampleFreq);
	gz *= (0.5f * invSampleFreq);
	qa = q0;
	qb = q1;
	qc = q2;
	q0 += (-qb * gx - qc * gy - q3 * gz);
	q1 += (qa * gx + qc * gz - q3 * gy);
	q2 += (qa * gy - qb * gz + q3 * gx);
	q3 += (qa * gz + qb * gy - qc * gx);

	// Normalise quaternion
	recipNorm = invSqrt_(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
	q0 *= recipNorm;
	q1 *= recipNorm;
	q2 *= recipNorm;
	q3 *= recipNorm;
	anglesComputed = 0;
}

