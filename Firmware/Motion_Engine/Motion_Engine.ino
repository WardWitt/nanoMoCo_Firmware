/*


Motion Engine

See dynamicperception.com for more information


(c) 2008-2012 C.A. Church / Dynamic Perception LLC

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


*/

/***************************************

		   Library Includes

****************************************/




#include "OMMoCoPrint.h"
#include "Debug.h"
#include <MsTimer2.h>
#include <TimerOne.h>
#include <EEPROM.h>
#include <AltSoftSerial.h>
#include <MemoryFree.h>
#include <hermite_spline.h>
#include <key_frames.h>
#include <CubicBezier.h>

// openmoco standard libraries
#include <OMComHandler.h>
#include <OMMotorMaster.h>
#include <OMMotorFunctions.h>
#include <OMState.h>
#include <OMCamera.h>
#include <OMMotor.h>
#include <OMMoCoBus.h>
#include <OMMoCoNode.h>
#include <OMEEPROM.h>


/***************************************

	   EEPROM Address Constants

****************************************/


/* 
 Need to declare these as early as possible
 
 (position count starts at zero)
  
 dev_addr        = 0
 name            = 2
 
*/

const int EE_ADDR       = 0;				// device_address (2 bytes)
const int EE_NAME       = 2;				// device name (16 bytes)

const int EE_POS_0   = EE_NAME    + 10;		// Motor 0 current position (long int)
const int EE_END_0   = EE_POS_0   + 4;		// Motor 0 end limit position (long int)
const int EE_START_0 = EE_END_0   + 4;		// Motor 0 program start position (long int)
const int EE_STOP_0  = EE_START_0 + 4;		// Motor 0 program stop position (long int)
const int EE_MS_0    = EE_STOP_0  + 4;		// Motor 0 microstep value (byte)
const int EE_SLEEP_0 = EE_MS_0    + 1;		// Motor 0 sleep state (byte)

const int EE_POS_1   = EE_SLEEP_0 + 1;		// Motor 1 current position (long int)
const int EE_END_1   = EE_POS_1   + 4;		// Motor 1 end limit position (long int)
const int EE_START_1 = EE_END_1   + 4;		// Motor 1 program start position (long int)
const int EE_STOP_1  = EE_START_1 + 4;		// Motor 1 program stop position (long int)
const int EE_MS_1    = EE_STOP_1  + 4;		// Motor 1 microstep value (byte)
const int EE_SLEEP_1 = EE_MS_1    + 1;		// Motor 0 sleep state (byte)

const int EE_POS_2   = EE_SLEEP_1 + 1;		// Motor 2 current position (long int)
const int EE_END_2   = EE_POS_2   + 4;		// Motor 2 end limit position (long int)
const int EE_START_2 = EE_END_2   + 4;		// Motor 2 program start position (long int)
const int EE_STOP_2  = EE_START_2 + 4;		// Motor 2 program stop position (long int)
const int EE_MS_2    = EE_STOP_2  + 4;		// Motor 2 microstep value (byte)
const int EE_SLEEP_2 = EE_MS_2    + 1;		// Motor 0 sleep state (byte)

const int EE_LOAD_POS		 = EE_SLEEP_2 + 1;			// Whether to load the motors' current positions after power cycle (byte)
const int EE_LOAD_START_STOP = EE_LOAD_POS + 1;			// Whether to load the motors' start/stop positions after power cycle (byte)
const int EE_LOAD_END		 = EE_LOAD_START_STOP + 1;	// Whether to load the motors' end positions after power cycle (byte)

const int EE_MOTOR_MEMORY_SPACE = 18;		//Number of bytes required for storage for each motor's variables

// Variables that are loaded from EEPROM that determine whether the motors' various positions should be restored
uint8_t ee_load_curPos = false;
uint8_t ee_load_endPos = false;
uint8_t ee_load_startStop = false;

/***************************************

	Controller Constants and Vars

****************************************/

// Serial node types
#define MOCOBUS 1
#define BLE 2
#define USB 3

const char SERIAL_TYPE[]			= "OMAXISVX";		// Serial API name
const int SERIAL_VERSION			= 70;				// Serial API version
byte node							= MOCOBUS;			// default node to use (MoCo Serial = 1; AltSoftSerial (BLE) = 2; USBSerial = 3)
byte device_name[]					= "DEFAULT   ";		// default device name, exactly 9 characters + null terminator
int device_address					= 3;				// NMX address (default = 3)
const byte START_FLASH_CNT			= 5;				// # of flashes of debug led at startup
const byte FLASH_DELAY				= 100;				// Time between flashes in milliseconds
const unsigned int START_RST_TM		= 5000;				// # of milliseconds PBT must be held low to do a factory reset
uint8_t debug_led_enable			= false;			// Debug led state
uint8_t timing_master				= true;				// Do we generate timing for all devices on the network? i.e. -are we the timing master?
bool graffik_mode					= false;			// Indicates whether the controller is currently communicating with the Graffik application
bool app_mode						= false;			// Indicates whether the controller is currently communicating with the mobile app
bool df_mode						= false;			// Indicates whether DragonFrame mode is enabled
byte controller_count				= 1;				// Number of controllers running concurrently. This is just a reference value for the app / Graffik

/***************************************

	  Controller I/O Pin Constants

****************************************/


// digital I/O line definitions
const byte DE_PIN = 28;
const byte DEBUG_PIN = 12;
const byte VOLTAGE_PIN = 42;
const byte CURRENT_PIN = 41;
const byte ESTOP_PIN = 40;
const byte BLUETOOTH_ENABLE_PIN = 0;


/***************************************

	  Aux Port Constants and Vars

****************************************/


// I/O modes
const byte         ALT_OFF = 0;		//Turns off the I/O
const byte       ALT_START = 1;		//INPUT  - starts program
const byte        ALT_STOP = 2;		//INPUT  - stops program
const byte      ALT_TOGGLE = 3;		//INPUT  - starts the program if it's stop, stops the program if it's running
const byte      ALT_EXTINT = 4;		//INPUT  - external interrupt that triggers the camera to shoot
const byte         ALT_DIR = 5;		//INPUT  - switch the direction of the motors
const byte  ALT_OUT_BEFORE = 6;		//OUTPUT - triggers output before camera shot
const byte   ALT_OUT_AFTER = 7;		//OUTPUT - triggers output after camera shot
const byte ALT_STOP_MOTORS = 8;		//INPUT  - stops the motors, lets the camera run if it's not done
const byte	  ALT_SET_HOME = 9;		//INPUT  - sets the motor's home position
const byte	   ALT_SET_END = 10;	//INPUT  - sets the motor's end position

// These defines are used for the Limit Switch Pin Change Registers

byte            altInputs[]		= { ALT_OFF, ALT_OFF };
unsigned int altBeforeDelay		= 100;
unsigned int  altAfterDelay		= 100;
unsigned int    altBeforeMs		= 1000;
unsigned int     altAfterMs		= 1000;
uint8_t        altForceShot		= false;
uint8_t           altExtInt		= false;
byte           altDirection		= FALLING;	// The detection edge for aux events
byte             altOutTrig		= HIGH;		// The voltage the trigger pin should be held in an idle state (i.e. when set HIGH, grounding the port triggers an aux event)
bool external_intervalometer	= false;	// Indicates whether the aux port has been set to external trigger mode via physical button press


/***************************************

	    Camera Constants and Vars

****************************************/


// Default camera settings
const byte CAM_DEFAULT_EXP		= 120;
const byte CAM_DEFAULT_WAIT		= 0;
const byte CAM_DEFAULT_FOCUS	= 0;


// necessary camera control variables
unsigned int  camera_fired		= 0;
uint8_t		  camera_test_mode	= false;
uint8_t		  fps				= 1;
boolean		  keep_camera_alive	= false;


/***************************************

		Motor Constants and Vars

****************************************/

// Set type def to simplify syntax of accessing static vars and functions from motor class
typedef OMMotorFunctions Motors;

// Deafult motor settings
const unsigned int MOT_DEFAULT_MAX_STEP		= 5000;			// Default maximum controller step rate output
const unsigned int MOT_DEFAULT_MAX_SPD		= 5000;			// Default maximum motor speed in steps / sec
const float MOT_DEFAULT_CONT_ACCEL			= 15000.0;		// Default motor accel/decel rate for non-program continuous moves
const unsigned int MOT_DEFAULT_BACKLASH		= 0;			// Default number of backlash steps to take up when reversing direction
const byte MOTOR_COUNT = 3;									// Number of motors possibly attached to controller

// plan move types
#define SMS				0		// Shoot-move-shoot mode
#define CONT_TL			1		// Continuous time lapse mode
#define CONT_VID		2		// Continuous video mode

// Valid microstep settings
#define FULL			1
#define HALF			2
#define QUARTER			4
#define EIGHTH			8
#define SIXTHEENTH		16

uint8_t ISR_On = false;
char byteFired = 0;				// Byte used to toggle the step pin for each motor within the ISR

// This is used because setting the end position in the motor library causes the NMX communications to lock up.
// That really ought to be looked into...
long endPos[] = { 0, 0, 0 };

/***************************************

	General Computational Constants

****************************************/


const float MILLIS_PER_SECOND	= 1000.0;			
const int	FLOAT_TO_FIXED		= 100;				// Multiply any floats to be transmitted in a serial response by this constant. Float responses don't seem to work correctly
const int	PERCENT_CONVERT		= 100;				// Multiplier to convert 0.0-1.0 range to percent


/***************************************

	   Program Constants and Vars

****************************************/


// program timer counters
unsigned long	run_time			= 0;				// Amount of time since the program has started (ms)
unsigned long	last_run_time		= 0;				// Stores the run time, even after the program has ended
unsigned long	start_time			= 0;				// Current time when program starts
uint8_t			running				= false;			// Program run status
volatile byte	force_stop			= false;	
uint8_t			ping_pong_mode		= false;			// ping pong mode variable
unsigned long	ping_pong_time		= 0;				// Run time that has elapsed during previous ping-pong passes
unsigned long	kf_ping_pong_time	= 0;				// Run time that has elapsed during previous ping-pong passes in kf mode
unsigned int	ping_pong_shots		= 0;				// Number of shots that have been taken during previous ping-pong passes
unsigned long	max_time			= 0;				// maximum run time
unsigned long	start_delay			= 0;				// Time delay for program starting
bool			delay_flag			= 0;				// If true, the program run time has not exceeded the start delay time
bool			pause_flag			= false;			// pause flag for later call of pauseProgram() 
bool			still_shooting_flag = false;			// If true, the program moves have completed, but the camera is still shooting
bool			ping_pong_flag		= false;			// If true, the program has completed its first cycle, but is continuing in ping-pong mode
bool			program_complete	= false;			// program completion flag



void stopProgram(uint8_t force_clear = true);	// Predefine this function to declare the default argument

extern void df_setup();
extern void df_loop();
extern void df_TimerHandler(void);


/***************************************

    Joystick Mode Constants and Vars

****************************************/


//Variables for joystick move, if watchdog is true the system expects a command at least once
//every WATCHDOG_MAX_TIME (mS), if it doesn't receive a command it will stop the motors
const unsigned int WATCHDOG_MAX_TIME = 1000;
uint8_t watchdog_mode = false;
uint8_t watchdog_active = false;
unsigned long commandTime = 0;
byte joystick_mode = false;


/***************************************

	  Key-frame Constants and Vars

****************************************/

KeyFrames kf[MOTOR_COUNT] = { KeyFrames(), KeyFrames(), KeyFrames() };
unsigned long kf_start_time;
unsigned long kf_last_update;
unsigned long kf_run_time;
unsigned long kf_pause_start;
unsigned long kf_this_pause;
unsigned long kf_pause_time;
unsigned long kf_last_shot_tm;
boolean kf_just_started = true;
boolean kf_running = false;
boolean kf_paused = false;

/***************************************

	     Object Initialization

****************************************/

AltSoftSerial altSerial;																			// altSerial library object
OMMoCoNode   NodeBlue = OMMoCoNode(&altSerial, device_address, SERIAL_VERSION, (char*)SERIAL_TYPE);	// Bluetooth Node Object
OMMoCoNode   NodeUSB = OMMoCoNode(&USBSerial, device_address, SERIAL_VERSION, (char*)SERIAL_TYPE);	// USB Serial Node Object
OMMoCoNode   Node = OMMoCoNode(&Serial, device_address, SERIAL_VERSION, (char*)SERIAL_TYPE);		// MoCoBus Node object
OMComHandler ComMgr = OMComHandler();																// Communications handler object
OMCamera     Camera = OMCamera();																	// Camera object
OMMotorFunctions motor[MOTOR_COUNT] = {																// Motor object
	OMMotorFunctions(OM_MOT1_DSTEP, OM_MOT1_DDIR, OM_MOT1_DSLP, OM_MOT1_DMS1, OM_MOT1_DMS2, OM_MOT1_DMS3, OM_MOT1_STPREG, OM_MOT1_STPFLAG),
	OMMotorFunctions(OM_MOT2_DSTEP, OM_MOT2_DDIR, OM_MOT2_DSLP, OM_MOT2_DMS1, OM_MOT2_DMS2, OM_MOT2_DMS3, OM_MOT2_STPREG, OM_MOT2_STPFLAG),
	OMMotorFunctions(OM_MOT3_DSTEP, OM_MOT3_DDIR, OM_MOT3_DSLP, OM_MOT3_DMS1, OM_MOT3_DMS2, OM_MOT3_DMS3, OM_MOT3_STPREG, OM_MOT3_STPFLAG) };

OMState      Engine = OMState(7);			// State engine object with 7 possible states. See state declarations below.

 //  state transitions 
const byte ST_BLOCK = 0;	// ST_BLOCK - do not allow any action to occur (some event is in process, block the state engine)
const byte ST_CLEAR = 1;	// ST_CLEAR - clear to start cycle
const byte ST_MOVE  = 2;	// ST_MOVE  - clear to move motor
const byte ST_RUN   = 3;	// ST_RUN   - motor is currently running
const byte ST_EXP   = 4;	// ST_EXP   - clear to expose camera (or not...)
const byte ST_WAIT  = 5;	// ST_WAIT  - in camera delay
const byte ST_ALTP  = 6;	// ST_ALTP  - check for alt output post

/***************************************

Debugging Vars and Objects

****************************************/

boolean debug_LED = false;
OMMoCoPrintClass mocoPrint = OMMoCoPrintClass(&Node);
DebugClass debug = DebugClass(&mocoPrint);


/* 

 =========================================
		Setup and loop functions
 =========================================
 
*/


void setup() {
	
	// Start USB serial communications
	USBSerial.begin(19200);
	delay(100);
  
	// Start Bluetooth communications
	altSerial.begin(9600);
	debug.functln("setup() - Done setting things up!");
  
	// Set controller I/O pin modes
	pinMode(DEBUG_PIN, OUTPUT);
	pinMode(BLUETOOTH_ENABLE_PIN, OUTPUT);
	digitalWrite(BLUETOOTH_ENABLE_PIN,HIGH);
	pinMode(VOLTAGE_PIN, INPUT);
	pinMode(CURRENT_PIN, INPUT);    
  
	// initalize state engine
	setupControlCycle();
	Engine.state(ST_BLOCK);

	// setup KeyFrames vars
	KeyFrames::setMaxVel(4000);
	KeyFrames::setMaxAccel(20000);	
 
	// default to master timing node
	ComMgr.master(true);
 
	// set handler for watched common lines
	ComMgr.watchHandler(motor_com_line);
 
	// setup camera defaults
	Camera.triggerTime(CAM_DEFAULT_EXP);
	Camera.delayTime(CAM_DEFAULT_WAIT);
	Camera.focusTime(CAM_DEFAULT_FOCUS);
	Camera.setHandler(camCallBack);

	// setup serial connection OM_SER_BPS is defined in OMMoCoBus library
	Serial.begin(OM_SER_BPS);

	// setup MoCoBus Node object
	Node.address(device_address);
	Node.setHandler(serNode1Handler);
	Node.setNotUsHandler(serNotUsNode1Handler);
	Node.setBCastHandler(serBroadcastHandler);
	Node.setSoftSerial(false);
 
	// setup MoCoBus Node object for bluetooth
	NodeBlue.address(device_address);
	NodeBlue.setHandler(serNodeBlueHandler);
	NodeBlue.setNotUsHandler(serNotUsNodeBlueHandler);
	NodeBlue.setBCastHandler(serBroadcastHandler);
	NodeBlue.setSoftSerial(true);


	// setup MoCoBus Node object for USB Serial
	NodeUSB.address(device_address);
	NodeUSB.setHandler(serNodeUSBHandler);
	NodeUSB.setNotUsHandler(serNotUsNodeUSBHandler);
	NodeUSB.setBCastHandler(serBroadcastHandler);
	NodeUSB.setSoftSerial(true);

 
 
	// Listen for address change
	Node.addressCallback(changeNodeAddr);
 
	NodeBlue.addressCallback(changeNodeAddr);
 
	NodeUSB.addressCallback(changeNodeAddr);
 
  
	// defaults for motor
	for( int i = 0; i < MOTOR_COUNT; i++){
		motor[i].enable(true);
		motor[i].maxStepRate(MOT_DEFAULT_MAX_STEP);
		motor[i].maxSpeed(MOT_DEFAULT_MAX_SPD);
		motor[i].contSpeed(MOT_DEFAULT_MAX_SPD);
		motor[i].contAccel(MOT_DEFAULT_CONT_ACCEL);
		motor[i].sleep(false);
		motor[i].backlash(MOT_DEFAULT_BACKLASH);
		motor[i].easing(OM_MOT_QUAD);
		motor[i].startPos(0);
		motor[i].stopPos(0);
		motor[i].units(INCH);
		motor[i].gboxRatio(1);
		motor[i].platRatio(1);
		// Set the slide motor to 4th stepping and pan/tilt motors to 16th
		if (i == 0)
			motor[i].ms(4);
		else
			motor[i].ms(16);
		motor[i].programBackCheck(false);	 
	}

	// restore/store eeprom memory
	eepromCheck();
 
	// enable limit switch handler
	// limitSwitch(true);
 
	// startup LED signal
	flasher(DEBUG_PIN, START_FLASH_CNT);

	//startISR();

	// Attach interrupt to watch for e-stop button press
	attachInterrupt(1, eStop, FALLING); 

	// Ensure that the axis array is set
	KeyFrames::setAxisArray(kf, MOTOR_COUNT);
}


void loop() {

	static unsigned long estop_time = 0; // Remembers last time eStop was NOT pressed
	static unsigned long df_time = 0;
	static unsigned long debug_time = 0;

	// check to see if we have any commands waiting      
	Node.check();
	NodeBlue.check();
	NodeUSB.check();

	// Only do these things every 100ms so we don't waste cycles during every loop
	if ((millis() - df_time) > 100) {		
		// If eStop button has been held more than 3 sec, switch to DF mode
		if (digitalRead(ESTOP_PIN) == HIGH)
			estop_time = millis();
		else if (!df_mode && millis() - estop_time > 3000) {
			ledChase(2);
			debug.funct("Entering DF mode");
			// Change motors to 8th stepping before starting DF mode
			for (byte i = 0; i < MOTOR_COUNT; i++){
				motor[i].ms(8);
			}
			df_mode = true;
			return;
		}
					
		// If the the DB_STEPS debug flag is true, print diagnostic info
		if (df_mode) {
			df_setup();
			df_loop();
			return;
		}
		df_time = millis();
	}	

	// Print debug information if necessary
	if ((millis() - debug_time) > 500) {
		//USBSerial.print("Free memory: ");
		//USBSerial.println(freeMemory());
		//motorDebug();
		debug_time = millis();
	}		

	//Stop the motors if they're running, watchdog is active, and time since last received command has exceeded timeout
	if (watchdog_active && (millis() - commandTime > WATCHDOG_MAX_TIME)){
		for (byte i = 0; i < MOTOR_COUNT; i++){
			if (motor[i].running()){
				stopAllMotors();
				break;
			}
		}
	}	
   
	// Update motor splines
	for(int i = 0; i < MOTOR_COUNT; i++){
		if(motor[i].running())
			motor[i].updateSpline();
	}	  
	  
	// If a classic-style program is running   
   if( running ) {
	   updateLegacyProgram();
   }
   // If a key frame program is running
   else if (kf_running){
	   kf_updateProgram();	   
   }

   // Check if any motors are being sent and restore their old microstep settings when they stop
   for (int i = 0; i < MOTOR_COUNT; i++){
	   if (motor[i].isSending() && !motor[i].running()){
		   motor[i].setSending(false);
		   motor[i].restoreLastMs();
	   }
   }
}

void updateLegacyProgram(){
	// update program run time
	unsigned long cur_time = millis();
	static unsigned long last_blink;
	const int BLINK_DELAY = 500;
	if (run_time == 0){
		last_blink = cur_time;
	}
	run_time += cur_time - start_time;
	start_time = cur_time;

	// Saving the run time to last_run_time makes it accessible even after the program as ended and run_time has been reset
	if (run_time != 0)
		last_run_time = run_time;

	// Got an external stop somewhere that wasn't a command?
	if (force_stop == true)
		stopProgram();

	// Hit max runtime? Done!
	if (ComMgr.master() && max_time > 0 && run_time > max_time)
		stopProgram();

	// If we're the slave and a interrupt has been triggered by the master, set to clear to fire mode (for multi-node sync)
	if (ComMgr.master() == false && ComMgr.slaveClear() == true)
		Engine.state(ST_CLEAR);

	// If the start delay is done then check current engine state and handle appropriately
	// Skip the delay if the ping_pong_flag is set
	if (run_time >= start_delay || ping_pong_flag){		
		// If we're in external intervalometer mode, keep the debug LED on, otherwise turn it off
		if (external_intervalometer)
			debugOn();
		else
			debugOff();
		// Proceed with the program
		Engine.checkCycle();
		delay_flag = false;
	}
	// Otherwise, set the delay flag true and toggle the debug LED if necessary
	else if (run_time < start_delay){
		delay_flag = true;
		if (cur_time - last_blink > BLINK_DELAY){
			debugToggle();
			last_blink = cur_time;
		}
	}
}

/*

=========================================
	  Program Control Functions
=========================================

*/


void pauseProgram() {
	// pause program
	Camera.stop();
	stopAllMotors();
	running = false;
}


void stopProgram(uint8_t force_clear) {

	// stop/clear program
	stopAllMotors();
	if( force_clear == true ) {
		run_time     = 0;
		camera_fired = 0;
		for( int i = 0; i < MOTOR_COUNT; i++){
			//resets the program move
			motor[i].resetProgramMove();
		}
	}
	
	running = false;
	still_shooting_flag = false;
	ping_pong_flag = false;

	// clear out motor moved data and stop motor 
	clearAll();	
	Camera.stop(); 
}


void startProgram() {
  // start program
  start_time = millis();

  running = true;
	for( int i = 0; i < MOTOR_COUNT; i++){
		if(motor[i].enable())
			motor[i].programDone(false);
	}
  
    // debug pin may have been brought high with a force stop
  if( force_stop == true ) {
    digitalWrite(DEBUG_PIN, LOW);
    force_stop = false;
  }
  
    // set ready to check for camera
    // we only do this for master nodes, not slaves
    // as slaves get their ok to fire state from OMComHandler
  if (ComMgr.master() == true) {
	  Engine.state(ST_CLEAR);
  }
                    
}

void eStop() {

	static unsigned long last_interrupt_time = 0;
	unsigned long interrupt_time = millis();

	static byte enable_count = 0;
	const byte THRESHOLD = 3;

	// If interrupts come faster than 200ms, assume it's a bounce and ignore
	if (interrupt_time - last_interrupt_time > 150) {

		if (running && !camera_test_mode)
			stopProgram();	// This previously paused the running program, but that caused weird state issues with the mobile app

		else if (running && camera_test_mode)
			stopProgram();
		
		else if (kf_running){
			kf_stopProgram();
			stopAllMotors();
		}

		else if (!motor[0].running() && !motor[1].running() && !motor[2].running()) {
			// If the button was pressed recently enough, increase the enable count, otherwise reset it to 0.
			if (interrupt_time - last_interrupt_time < 1000)
				enable_count++;
			else
				enable_count = 1;
						
			debug.funct("eStop() - Switch count ");
			debug.functln(enable_count);

			// If the user has pressed the e-stop enough times within the alloted time span, reset the USB connection and toggle the external intervalometer mode.
			if (enable_count >= THRESHOLD) {								
				

				if (!external_intervalometer){
					setIntervalometerMode(true);					
				}
				else{
					setIntervalometerMode(false);
				}
				enable_count = 0;
				delay(100);
				resetUSBconnection();
			}
		}

		else
			stopAllMotors();
	}
	last_interrupt_time = interrupt_time;
}

void setIntervalometerMode(boolean enabled){
	if (enabled){
		limitSwitchAttach(0);
		altConnect(0, ALT_EXTINT);
		altConnect(1, ALT_EXTINT);
		altSetup();
		external_intervalometer = true;
		// Turn the debug light on to confirm the setting
		debugOn();
	}
	else{
		altConnect(0, ALT_OFF);
		altConnect(1, ALT_OFF);
		altSetup();
		external_intervalometer = false;
		// Turn the debug light off to confirm the setting
		debugOff();
	}
}

boolean getIntervalometerMode(){
	return external_intervalometer;
}

byte getRunStatus(){
	
	byte status				= B00000000;
	const byte RUNNING		= B00000001;
	const byte PAUSED		= B00000010;
	const byte KEYFRAME		= B00000100;
	const byte DELAY		= B00001000;
	const byte KEEPALIVE	= B00010000;
	const byte PINGPONG		= B00100000;

	if (running){
		status |= RUNNING;
	}
	else if (kf_running){
		status |= RUNNING;
		status |= KEYFRAME;
	}

	if (pause_flag){
		// The "running" var for the legacy program is false when paused, so indicate it manually
		status |= RUNNING;
		status |= PAUSED;		
	}
	else if (kf_paused){
		status |= PAUSED;
		status |= KEYFRAME;
	}
	if (delay_flag){
		status |= DELAY;
	}
	if (keepAliveMode()){		
		status |= KEEPALIVE;
	}
	if (pingPongMode()){
		status |= PINGPONG;
	}	
	return status;
}

/*

=========================================
		Program Query Functions
=========================================

*/

uint8_t programPercent() {

	unsigned long time = millis();
	static uint8_t percent = 0;

	unsigned long longest_move = 0;

	// Check the total length of each motor's move and save the longest one
	for (byte i = 0; i < MOTOR_COUNT; i++) {

		// If the motor isn't enabled, don't check its move length
		if (!motor[i].enable())
			continue;

		unsigned long current_move;

		current_move = motor[i].planLeadIn() + motor[i].planTravelLength() + motor[i].planLeadIn();

		// Update the longest move if necessary
		if (current_move > longest_move)
			longest_move = current_move;

	}

	uint8_t percent_new;

	// If in SMS mode and the camera max shots is less than the longest motor move, use that value instead
	if (Motors::planType() == SMS && Camera.getMaxShots() < longest_move)
		longest_move = Camera.getMaxShots();

	// Determine the program percent completion by dividing the current shots by the max shots.
	// Multiply by 100 to give whole number percent.

	// Determine the percent completion for SMS based on shots
	if (Motors::planType() == SMS)
		percent_new = round((float)camera_fired / (float)longest_move * 100.0);

	// Otherwise determine the percent completion based on run-time (don't include the start delay)
	else
		percent_new = round((float)(run_time - start_delay) / (float)longest_move * 100.0);

	// If the newly calculated percent complete is 0 and the last percent complete was non-zero, then the program has finished and the program should report 100% completion
	// Don't execute this behavior in Graffik mode
	if (percent_new == 0 && percent != 0 && !graffikMode())
		percent = 100;
	else
		percent = percent_new;
	return(percent);
}

// Returns the total run time of the currently set program in milliseconds
unsigned long totalProgramTime() {

	unsigned long longest_time = 0;
	unsigned long motor_time = 0;

	for (byte i = 0; i < MOTOR_COUNT; i++) {
		// If the motor is enabled, check its program time
		if (motor[i].enable()) {
			// SMS: Total the exposures for the program and multiply by the interval
			if (motor[i].planType() == SMS) {
				motor_time = Camera.intervalTime() * (motor[i].planLeadIn() + motor[i].planTravelLength() + motor[i].planLeadOut());
			}
			// CONT_TL AND CONT_VID: all segments are in milliseconds, no need to multiply anything
			else
				motor_time = motor[i].planLeadIn() + motor[i].planTravelLength() + motor[i].planLeadOut();
			// Overwrite longest_time if the last checked motor is longer
			if (motor_time > longest_time)
				longest_time = motor_time;
		}
	}

	// Add the program delay
	longest_time += start_delay;

	return(longest_time);
}


uint8_t programComplete() {

	// This function will respond true the first time it is
	// called after program completes

	uint8_t status = false;

	if (program_complete == true)
		status = true;

	program_complete = false;
	return(status);
}


/*

=========================================
		   Helper Functions
=========================================

*/

void resetUSBconnection(){
	USBSerial.end();
	delay(100);
	USBSerial.begin(19200);
	delay(100);	
}

void flasher(byte pin, int count) {
    // flash a pin several times (blink)
    
   for(int i = 0; i < count; i++) {
      digitalWrite(pin, HIGH);
	  delay(FLASH_DELAY);
      digitalWrite(pin, LOW);
	  delay(FLASH_DELAY);
   }
   
}

/*
void ledChase(byte p_chases)

Wipes LEDs forward and back a specified number of times. Can be used as an indicator,
but if holding position is critical, be aware that motors will be briefly put into sleep mode.

*/

void ledChase(byte p_chases) {
	
	bool current_sleep[MOTOR_COUNT];
	
	for (byte i = 0; i < MOTOR_COUNT; i++) {
		// Save the current sleep state of all the motors so they can be restored later
		current_sleep[i] = motor[i].sleep();
		// Put them into sleep mode in case it isn't already
		motor[i].sleep(true);
	}

	for (byte chase_count = 0; chase_count < p_chases; chase_count++) {
		int bounce_count = p_chases * MOTOR_COUNT;
		int current_motor = 0;
		int chase_dir = 1;
		
		for (int i = 0; i < bounce_count; i++){
			motor[current_motor].sleep(false);
			delay(75);
			motor[current_motor].sleep(true);
			current_motor += chase_dir;
			
			// Change direction when at the last LED
			if (current_motor == MOTOR_COUNT)
				chase_dir *= -1;

		}
	}
	// Restore the motor sleep states
	for (byte i = 0; i < MOTOR_COUNT; i++) {
		motor[i].sleep(current_sleep[i]);
	}
}


byte powerCycled() {
	
	// This function will respond true the first time it is
	// called after a power cycle and false thereafter
	static byte cycled = true;
	byte response = cycled;
	cycled = false;

	return(response);
}


uint8_t checkMotorAttach() {

	// If any of the motors is moving or a program is currently running return the error value
	for (byte i = 0; i < MOTOR_COUNT; i++) {
		if (motor[i].running() || running)
			return(B1000); // B1000 = 8
	}

	bool current_sleep[MOTOR_COUNT];
	uint8_t attached = B000;

	
	for (byte i = 0; i < MOTOR_COUNT; i++) {
		// Save the current sleep state of all the motors so they can be restored when done with the attach check
		current_sleep[i] = motor[i].sleep();
		// Put them into sleep mode in case it isn't already
		motor[i].sleep(true);
	}

	for (int i = 0; i < MOTOR_COUNT; i++) {
		motor[i].sleep(false);
		delay(100);
		// Read the analog value from current sensing pin
		int current = analogRead(CURRENT_PIN);
		// Convert the value to current in millamps
		float amps = (float)current / 1023 * 5;
		// This is the threshold in amps above which a motor will register as being detected;
		const float THRESHOLD = 0.15;
		// If the draw is greater than <THRESHOLD> amps, then a motor is connected to the enabled channel
		if (amps > THRESHOLD)
			attached |= (1 << i);
		// Put the motor back to sleep so it doesn't interfere with reading of the next motor
		motor[i].sleep(true);
		
		debug.funct("Motor ");
		debug.funct(i);
		debug.funct(" current draw: ");
		debug.functln(amps);
	}

	// Restore the saved sleep states
	for (byte i = 0; i < MOTOR_COUNT; i++) {
		motor[i].sleep(current_sleep[i]);
	}

	// The bits of the attached byte indicate each motor's attached status
	return(attached);
}

/*

=========================================
		   Debug Functions
=========================================

*/

void motorDebug() {

	if (debug.getState() & DebugClass::DB_STEPS){
		

		for (byte i = 0; i < MOTOR_COUNT; i++){
			debug.steps("Current Steps ");
			debug.steps(motor[i].currentPos());
			debug.steps(" continious Speed: ");
			debug.steps(motor[i].contSpeed());
			debug.steps(" backlash: ");
			debug.steps(motor[i].backlash());
			debug.steps(" startPos: ");
			debug.steps(motor[i].startPos());
			debug.steps(" stopPos: ");
			debug.steps(motor[i].stopPos());
			debug.steps(" endPos: ");
			debug.steps(motor[i].endPos());
			debug.steps(" running: ");
			debug.steps(motor[i].running());
			debug.steps(" enable: ");
			debug.steps(motor[i].enable());
			debug.steps(" Type: ");
			debug.stepsln(Motors::planType());
			debug.steps(" shots: ");
			debug.steps(camera_fired);
			debug.steps(" leadIn: ");
			debug.stepsln(motor[i].planLeadIn());
		}
	}
}


/*

=========================================
	  Mode Specific Functions
=========================================

*/

void graffikMode(bool p_setting) {

	// Ignore non-boolean input
	if (p_setting != 0 && p_setting != 1)
		return;

	graffik_mode = p_setting;

	// Don't allow app mode to be active at the same time
	if (graffik_mode)
		app_mode = false;

}

bool graffikMode() {
	return graffik_mode;
}

void appMode(bool p_setting) {

	// Ignore non-boolean input
	if (p_setting != 0 && p_setting != 1)
		return;

	app_mode = p_setting;

	// Don't allow graffik mode to be active at the same time
	if (app_mode)
		graffik_mode = false;

}

bool appMode() {
	return app_mode;
}

void watchdogMode(bool enabled){
	watchdog_mode = enabled;	
	watchdog_active = enabled;	
}

bool watchdogMode(){
	return watchdog_mode;
}
