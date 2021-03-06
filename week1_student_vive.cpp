//Alex Samland and Josh Zaugg

#include <stdio.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <stdint.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <curses.h>
#include "vive.h"

//gcc -o week1 week_1.cpp -lwiringPi -lncurses -lm

#define frequency 25000000.0
#define CONFIG           0x1A
#define SMPLRT_DIV       0x19
#define GYRO_CONFIG      0x1B
#define ACCEL_CONFIG     0x1C
#define ACCEL_CONFIG2    0x1D
#define USER_CTRL        0x6A  // Bit 7 enable DMP, bit 3 reset DMP
#define PWR_MGMT_1       0x6B // Device defaults to the SLEEP mode
#define PWR_MGMT_2       0x6C

#define PWM_MAX 2000
#define NEUTRAL_PWM 1725
#define frequency 25000000.0
#define LED0 0x6
#define LED0_ON_L 0x6
#define LED0_ON_H 0x7
#define LED0_OFF_L 0x8
#define LED0_OFF_H 0x9
#define LED_MULTIPLYER 4

enum Ascale {
  AFS_2G = 0,
  AFS_4G,
  AFS_8G,
  AFS_16G
};

enum Gscale {
  GFS_250DPS = 0,
  GFS_500DPS,
  GFS_1000DPS,
  GFS_2000DPS
};




struct Keyboard {
  int key_press;
  int pitch;
  int roll;
  int yaw;
  int thrust;
  int heartbeat;
};

int setup_imu();
void calibrate_imu();
void calibrate_vive();
void read_imu();
void update_filter();
void setup_keyboard();
void trap(int);
void safety_check(void);
void init_pwm();
void init_motor(uint8_t);
void set_PWM(uint8_t, float);
void stop_motors();
void safety_fail();
void pid_update();

//global variables
Keyboard* shared_memory;
int run_program=1;
int run_pwm = 1;
int imu;
float x_gyro_calibration=0;
float y_gyro_calibration=0;
float z_gyro_calibration=0;
float pitch_calibration=0;
float roll_calibration=0;
float vive_x_target;
float vive_y_target;
float accel_z_calibration=0;
float imu_data[6]; //gyro xyz, accel xyz
long time_curr;
long time_prev;
struct timespec te;
float yaw=0;
float gyro_pitch = 0;
float gyro_roll = 0;
Position local_p;
float roll_angle=0;
float pitch_angle=0;

int pwm;


int main (int argc, char *argv[])
{

    setup_imu();

    init_pwm();
    init_motor(0);
    init_motor(1);
    init_motor(2);
    init_motor(3);
    delay(1000);

    //calibrate imu to zero gyro and accel readings
    init_shared_memory();
    calibrate_imu();
    local_p= *position;
    calibrate_vive();
    read_imu();
    setup_keyboard();
    //assign trap function as signal handler for SIGINT
    signal(SIGINT, &trap);
    printf("pitch_accel\tpitch angle\tmotor0\tmotor1\tmotor2\tmotor3\r\n");

    while(run_program==1)
    {
      local_p=*position;
      //read raw data from imu with
      read_imu();
      //put raw imu data through complementary filter
      update_filter();
      //check if any conditions to terminate program are met
      safety_check();
      pid_update();
      //print imu values to see that program is running

    }
    return 0;

}

void calibrate_vive(){
  vive_x_target = local_p.x;
  vive_y_target = local_p.y;
}

void calibrate_imu()
{
  int count = 0;
  const int NUMSAMP = 1000;
  float xtemp = 0;
  float ytemp = 0;
  float ztemp = 0;
  float pitchtemp = 0;
  float rolltemp = 0;
  float zacctemp = 0;

  x_gyro_calibration=0;
  y_gyro_calibration=0;
  z_gyro_calibration=0;
  pitch_calibration=0;
  roll_calibration=0;
  accel_z_calibration=0;

  while (count<NUMSAMP) {
    read_imu();

    xtemp += imu_data[0]/NUMSAMP;
    ytemp += imu_data[1]/NUMSAMP;
    ztemp += imu_data[2]/NUMSAMP;
    pitchtemp += imu_data[3]/NUMSAMP;
    rolltemp += imu_data[4]/NUMSAMP;
    zacctemp += imu_data[5]/NUMSAMP;

    count++;
  }

  x_gyro_calibration=xtemp;
  y_gyro_calibration=ytemp;
  z_gyro_calibration=ztemp;
  pitch_calibration=pitchtemp;
  roll_calibration=rolltemp;
  accel_z_calibration=zacctemp;


//print calibration data to screen
printf("calibration complete, %f %f %f %f %f %f\n\r",x_gyro_calibration,y_gyro_calibration,z_gyro_calibration,pitch_calibration,roll_calibration,accel_z_calibration);


}

void read_imu()
{
  int address=59;
  float ax=0;
  float az=0;
  float ay=0;
  int vh,vl;

  //read in data
  vh=wiringPiI2CReadReg8(imu,address);
  vl=wiringPiI2CReadReg8(imu,address+1);
  //convert 2 complement
  int vw=(((vh<<8)&0xff00)|(vl&0x00ff))&0xffff;
  if(vw>0x8000)
  {
    vw=vw ^ 0xffff;
    vw=-vw-1;
  }
  float xacc=vw/16384.;//  DONE: convert vw from raw values to "g's"


  address=61;
  vh=wiringPiI2CReadReg8(imu,address);
  vl=wiringPiI2CReadReg8(imu,address+1);
  vw=(((vh<<8)&0xff00)|(vl&0x00ff))&0xffff;
  if(vw>0x8000)
  {
    vw=vw ^ 0xffff;
    vw=-vw-1;
  }
  float yacc =vw/16384.; //DONE: convert vw from raw valeus to "g's"


  address=63;
  vh=wiringPiI2CReadReg8(imu,address);
  vl=wiringPiI2CReadReg8(imu,address+1);
  vw=(((vh<<8)&0xff00)|(vl&0x00ff))&0xffff;
  if(vw>0x8000)
  {
    vw=vw ^ 0xffff;
    vw=-vw-1;
  }
  imu_data[5]=-vw/16384.;


  //pitch
  imu_data[3]=-(atan2(yacc,imu_data[5])*57.29578) - pitch_calibration;
  //roll
  imu_data[4]=atan2(xacc,imu_data[5])*57.29578 - roll_calibration;



  address=67;
  vh=wiringPiI2CReadReg8(imu,address);
  vl=wiringPiI2CReadReg8(imu,address+1);
  vw=(((vh<<8)&0xff00)|(vl&0x00ff))&0xffff;
  if(vw>0x8000)
  {
    vw=vw ^ 0xffff;
    vw=-vw-1;
  }
  imu_data[0]=-x_gyro_calibration+vw*0.015259;

  address=69;
  vh=wiringPiI2CReadReg8(imu,address);
  vl=wiringPiI2CReadReg8(imu,address+1);
  vw=(((vh<<8)&0xff00)|(vl&0x00ff))&0xffff;
  if(vw>0x8000)
  {
    vw=vw ^ 0xffff;
    vw=-vw-1;
  }
 imu_data[1]=-y_gyro_calibration+vw*0.015259;

  address=71;
  vh=wiringPiI2CReadReg8(imu,address);
  vl=wiringPiI2CReadReg8(imu,address+1);
  vw=(((vh<<8)&0xff00)|(vl&0x00ff))&0xffff;
  if(vw>0x8000)
  {
    vw=vw ^ 0xffff;
    vw=-vw-1;
  }
  imu_data[2]=-z_gyro_calibration+vw*0.015259;




}
//fix the filter
void update_filter()
{

  //get current time in nanoseconds
  timespec_get(&te,TIME_UTC);
  time_curr=te.tv_nsec;
  //compute time since last execution
  float imu_diff=time_curr-time_prev;

  //check for rollover
  if(imu_diff<=0)
  {
    imu_diff+=1000000000;
  }
  //convert to seconds
  imu_diff=imu_diff/1000000000;
  time_prev=time_curr;

  //comp. filter for pitch, roll here:
  float pitch_gyro_delta = imu_data[0]*imu_diff;
  float roll_gyro_delta = imu_data[1]*imu_diff;
  gyro_pitch += pitch_gyro_delta;
  gyro_roll += roll_gyro_delta;

  const float A = 0.01;
  pitch_angle = imu_data[3]*A+(1-A)*(pitch_gyro_delta+pitch_angle);
  roll_angle = imu_data[4]*A+(1-A)*(roll_gyro_delta+roll_angle);



}

void safety_check()
{
  static long time_start;
  static long time_last;
  static long time_elapsed;
  static int heartbeat_old = 0;
  static int vive_heartbeat_old  = 0;
  static long vtime_start;
  static long vtime_last;
  static long vtime_elapsed;

  //if any distance from the x,y is greater than 1000, STOPP
  if (abs(local_p.x-vive_x_target)>1000 || abs(local_p.y-vive_y_target)>1000){
	    printf("too far from the vive center !\r\n");
	    safety_fail();
	  }

  //if any gyro rate >300 deg/s, kill program
  if (fmax(abs(imu_data[0]),fmax(abs(imu_data[1]),abs(imu_data[2])))>500.00)
  {
    printf("gyro too extreme !\r\n");
    safety_fail();

  //if pitch/roll greater than 45 deg, kill program
  } else if (fmax(abs(pitch_angle),abs(roll_angle))>45.0) {
    printf("pitch/roll too extreme !\r\n");
    safety_fail();
  }

  //read keyboard values from shared memory
  Keyboard keyboard=*shared_memory;
  //printf("%d", keyboard.heartbeat);
  //if space is pressed, kill program
  switch(keyboard.key_press) {
    case 34:
            stop_motors();
            run_pwm = 0;
            break;
    case 33:
            printf("space pressed !\r\n");
            safety_fail();
            break;
    case 32:
            run_pwm = 1;
            break;
    case 35:
            stop_motors();
            run_pwm = 0;
            calibrate_imu();
            calibrate_vive();
            local_p.x =0;
            local_p.y=0;
            break;
	  }

  // if heartbeat is the same as it was last time through loop, check elapsed time
  if (keyboard.heartbeat==heartbeat_old) {
    timespec_get(&te,TIME_UTC);
    time_last=te.tv_nsec;
    time_elapsed=(time_last-time_start);
    if(time_elapsed<=0)
    {
      time_elapsed+=1000000000;
    }
    // if heartbeat has not incremented for 0.25s, kill program
    if (time_elapsed>=250000000)
    {
      printf("joystick timeout !\r\n");
      safety_fail();
    }
  //if heartbeat has incremented restart timer
  } else {
    timespec_get(&te,TIME_UTC);
    time_start=te.tv_nsec;
    heartbeat_old=keyboard.heartbeat;
  }
  //vive heartbeat
  if (local_p.version==vive_heartbeat_old) {
    timespec_get(&te,TIME_UTC);
    vtime_last=te.tv_nsec;
    vtime_elapsed=(vtime_last-vtime_start);
    if(vtime_elapsed<=0){
      vtime_elapsed+=1000000000;
    }
    // if heartbeat has not incremented for 0.25s, kill program
    if (vtime_elapsed>=500000000){
      printf("vive timeout !\r\n");
      safety_fail();
    }
  //if heartbeat has incremented restart timer
  } else {
    timespec_get(&te,TIME_UTC);
    vtime_start=te.tv_nsec;
    vive_heartbeat_old=local_p.version;
  }
}


int setup_imu()
{
  wiringPiSetup ();


  //setup imu on I2C
  imu=wiringPiI2CSetup (0x68) ; //accel/gyro address

  if(imu==-1)
  {
    printf("-----cant connect to I2C device %d --------\n",imu);
    return -1;
  }
  else
  {

    printf("connected to i2c device %d\n",imu);
    printf("imu who am i is %d \n",wiringPiI2CReadReg8(imu,0x75));

    uint8_t Ascale = AFS_2G;     // AFS_2G, AFS_4G, AFS_8G, AFS_16G
    uint8_t Gscale = GFS_500DPS; // GFS_250DPS, GFS_500DPS, GFS_1000DPS, GFS_2000DPS


    //init imu
    wiringPiI2CWriteReg8(imu,PWR_MGMT_1, 0x00);
    printf("                    \n\r");
    wiringPiI2CWriteReg8(imu,PWR_MGMT_1, 0x01);
    wiringPiI2CWriteReg8(imu, CONFIG, 0x00);
    wiringPiI2CWriteReg8(imu, SMPLRT_DIV, 0x00); //0x04
    int c=wiringPiI2CReadReg8(imu,  GYRO_CONFIG);
    wiringPiI2CWriteReg8(imu,  GYRO_CONFIG, c & ~0xE0);
    wiringPiI2CWriteReg8(imu, GYRO_CONFIG, c & ~0x18);
    wiringPiI2CWriteReg8(imu, GYRO_CONFIG, c | Gscale << 3);
    c=wiringPiI2CReadReg8(imu, ACCEL_CONFIG);
    wiringPiI2CWriteReg8(imu,  ACCEL_CONFIG, c & ~0xE0); // Clear self-test bits [7:5]
    wiringPiI2CWriteReg8(imu,  ACCEL_CONFIG, c & ~0x18); // Clear AFS bits [4:3]
    wiringPiI2CWriteReg8(imu,  ACCEL_CONFIG, c | Ascale << 3);
    c=wiringPiI2CReadReg8(imu, ACCEL_CONFIG2);
    wiringPiI2CWriteReg8(imu,  ACCEL_CONFIG2, c & ~0x0F); //
    wiringPiI2CWriteReg8(imu,  ACCEL_CONFIG2,  c | 0x00);
  }
  return 0;
}

void setup_keyboard()
{

  int segment_id;
  struct shmid_ds shmbuffer;
  int segment_size;
  const int shared_segment_size = 0x6400;
  int smhkey=33222;

  /* Allocate a shared memory segment.  */
  segment_id = shmget (smhkey, shared_segment_size,IPC_CREAT | 0666);
  /* Attach the shared memory segment.  */
  shared_memory = (Keyboard*) shmat (segment_id, 0, 0);
  printf ("shared memory attached at address %p\n", shared_memory);
  /* Determine the segment's size. */
  shmctl (segment_id, IPC_STAT, &shmbuffer);
  segment_size  =               shmbuffer.shm_segsz;
  printf ("segment size: %d\n", segment_size);
  /* Write a string to the shared memory segment.  */
  //sprintf (shared_memory, "test!!!!.");

}

//when cntrl+c pressed, kill motors
void trap(int signal)
{
  printf("Cntrl+C presses !\n\r");
  safety_fail();
}


void init_pwm()
{

    pwm=wiringPiI2CSetup (0x40);
    if(pwm==-1)
    {
      printf("-----cant connect to I2C device %d --------\n",pwm);

    }
    else
    {

      float freq =400.0*.95;
      float prescaleval = 25000000;
      prescaleval /= 4096;
      prescaleval /= freq;
      prescaleval -= 1;
      uint8_t prescale = floor(prescaleval+0.5);
      int settings = wiringPiI2CReadReg8(pwm, 0x00) & 0x7F;
      int sleep = settings | 0x10;
      int wake  = settings & 0xef;
      int restart = wake | 0x80;
      wiringPiI2CWriteReg8(pwm, 0x00, sleep);
      wiringPiI2CWriteReg8(pwm, 0xfe, prescale);
      wiringPiI2CWriteReg8(pwm, 0x00, wake);
      delay(10);
      wiringPiI2CWriteReg8(pwm, 0x00, restart|0x20);
    }
}



void init_motor(uint8_t channel)
{
  int on_value=0;

  int time_on_us=900;
  uint16_t off_value=round((time_on_us*4096.f)/(1000000.f/400.0));

  wiringPiI2CWriteReg8(pwm, LED0_ON_L + LED_MULTIPLYER * channel, on_value & 0xFF);
  wiringPiI2CWriteReg8(pwm, LED0_ON_H + LED_MULTIPLYER * channel, on_value >> 8);
  wiringPiI2CWriteReg8(pwm, LED0_OFF_L + LED_MULTIPLYER * channel, off_value & 0xFF);
  wiringPiI2CWriteReg8(pwm, LED0_OFF_H + LED_MULTIPLYER * channel, off_value >> 8);
  delay(100);

   time_on_us=1200;
   off_value=round((time_on_us*4096.f)/(1000000.f/400.0));

  wiringPiI2CWriteReg8(pwm, LED0_ON_L + LED_MULTIPLYER * channel, on_value & 0xFF);
  wiringPiI2CWriteReg8(pwm, LED0_ON_H + LED_MULTIPLYER * channel, on_value >> 8);
  wiringPiI2CWriteReg8(pwm, LED0_OFF_L + LED_MULTIPLYER * channel, off_value & 0xFF);
  wiringPiI2CWriteReg8(pwm, LED0_OFF_H + LED_MULTIPLYER * channel, off_value >> 8);
  delay(100);

   time_on_us=1000;
   off_value=round((time_on_us*4096.f)/(1000000.f/400.0));

  wiringPiI2CWriteReg8(pwm, LED0_ON_L + LED_MULTIPLYER * channel, on_value & 0xFF);
  wiringPiI2CWriteReg8(pwm, LED0_ON_H + LED_MULTIPLYER * channel, on_value >> 8);
  wiringPiI2CWriteReg8(pwm, LED0_OFF_L + LED_MULTIPLYER * channel, off_value & 0xFF);
  wiringPiI2CWriteReg8(pwm, LED0_OFF_H + LED_MULTIPLYER * channel, off_value >> 8);
  delay(100);

}


void set_PWM( uint8_t channel, float time_on_us)
{
  if(run_program==1 && run_pwm == 1)
  {
    if(time_on_us>PWM_MAX)
    {
      time_on_us=PWM_MAX;
    }
    else if(time_on_us<1000)
    {
      time_on_us=1000;
    }
    uint16_t off_value=round((time_on_us*4096.f)/(1000000.f/400.0));
    wiringPiI2CWriteReg16(pwm, LED0_OFF_L + LED_MULTIPLYER * channel,off_value);
  }
}

void stop_motors(){
    set_PWM(0,1000);
    set_PWM(1,1000);
    set_PWM(2,1000);
    set_PWM(3,1000);
}

void safety_fail(){
  stop_motors();
  run_program = 0;
  printf("Shutdown\r\n");
}

void pid_update(){
  Keyboard keyboard=*shared_memory;
  static float pitch_previous = 0;
  static float i_pitch_error = 0;
  static float roll_previous = 0;
  static float i_roll_error = 0;
  static float i_max = 80;
  static int count = 0;

  float yaw_rate = imu_data[2];
  float P_yaw = 0*1.5;
  float P_yaw_vive = 150.2173;
  float yaw_error= local_p.yaw-0; //yaw target is 0
  float yaw_target_speed = P_yaw_vive*yaw_error;//(keyboard.yaw-128)*1.5;
  float yaw_error_speed = -(yaw_target_speed-yaw_rate);

  int Thrust = NEUTRAL_PWM+(keyboard.thrust-128)*160.0/112.0;
  int motor0PWM, motor1PWM, motor2PWM, motor3PWM;

  float pitch_target = -(keyboard.pitch-128)*8.00/112.0;
  float P_pitch = 12;
  float D_pitch = 220;
  float I_pitch = 0.06;

  float pitch_error = pitch_target-pitch_angle;
  float dpitch = (pitch_previous-pitch_angle);
  i_pitch_error += I_pitch*pitch_error;
  if (i_pitch_error > i_max)
  {
    i_pitch_error = i_max;
  }
  else if (i_pitch_error < -i_max)
  {
    i_pitch_error = -i_max;
  }

  float roll_target = (keyboard.roll-128)*8.0/112.0;
  float P_roll = P_pitch;
  float D_roll = D_pitch;
  float I_roll = I_pitch;



  float roll_error = roll_target-roll_angle;
  float droll = (roll_previous-roll_angle);
  i_roll_error += I_roll*roll_error;
  if (i_roll_error > i_max)
  {
    i_roll_error = i_max;
  }
  else if (i_roll_error < -i_max)
  {
    i_roll_error = -i_max;
  }

  motor0PWM = Thrust - pitch_error*P_pitch - dpitch*D_pitch - i_pitch_error + ( -roll_error*P_roll - droll*D_roll - i_roll_error)+yaw_error_speed*P_yaw;
  motor1PWM = Thrust + pitch_error*P_pitch + dpitch*D_pitch + i_pitch_error + ( -roll_error*P_roll - droll*D_roll - i_roll_error)-yaw_error_speed*P_yaw;
  motor2PWM = Thrust - pitch_error*P_pitch - dpitch*D_pitch - i_pitch_error + ( roll_error*P_roll + droll*D_roll + i_roll_error)-yaw_error_speed*P_yaw;
  motor3PWM = Thrust + pitch_error*P_pitch + dpitch*D_pitch + i_pitch_error + ( roll_error*P_roll + droll*D_roll + i_roll_error)+yaw_error_speed*P_yaw;
  set_PWM(0,motor0PWM);
  set_PWM(1,motor1PWM);
  set_PWM(2,motor2PWM);
  set_PWM(3,motor3PWM);
  //printf("%f\t%f\t%d\t%d\t%d\t%d\r\n",imu_data[3]*100.0, pitch_angle*100.00, motor0PWM, motor1PWM, motor2PWM, motor3PWM);
  if (count%5==0)
  {
    //printf("%f\t%f\t%f\r\n", pitch_angle, pitch_target, i_pitch_error);
    //printf("%f\t%f\t%f\t%f\r\n", pitch_angle, dpitch*D_pitch, roll_angle, droll*D_roll );
  }

  pitch_previous = pitch_angle;
  roll_previous = roll_angle;
  count++;
}
