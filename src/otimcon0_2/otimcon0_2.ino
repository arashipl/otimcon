
#define SW_VERSION "0.22"
/*
 *************************************************************
    Project: Open Timing Control (OTIMCON)
    Version: 0.21
    Date: 20180331
    Created by: Stipe Predanic (stipe.predanic (at) gmail.com)
 **************************************************************

   Required:
    HW:
    - Arduino based board (tested with Arduino Uno - AtMega328 board, should work on other Arduino systems if they support I2C and SPI)
       - NOTE: from version 0.2 string comparing is done against the PROGMEM, which isn't supported in other Arduino systems!
               In that cse change the CMP_STRINGS macro.
    - MRFC522 module http://playground.arduino.cc/Learning/MFRC522
    - RTC clock supported by RTCLib (eg. DS1307, RTC_PCF8523, RTC_DS3231)
    - EEPROM (optional - used for backup, comes with DS3231 on popular cheap Chinese modules)
    - LED (optional, depending on the way to give feedback)
    - piezo buzzer (optional, depending on the way to give feedback),
      + SerialCommand Library ( https://github.com/kiwisincebirth/Arduino-SerialCommand )
      + Low-Power Library by RocketScream (available in Arduino IDE)
      + PinChangeInterrupt by NicoHood (available in Arduino IDE)

 **************************************************************

   HW connection (if used by 328 type board)
    - DS3231 module connected via I2C on A4 (SDA) and A5 (SCL)
    - MFRC522 conected via SPI (D13 SCK, D12 MISO, D11 MOSI, with RESET AND SS are selected in code (in this case SS is D10, and RESET D9)
    - LED and or piezo are connected to pin selected in code (in this case LED is on D8 and PIEZO on D7).
    - an optional buzzer with integrated generator is connected to pin selected in code (in this case D2).

    Don't forget the resistors for current (LED, piezo) or for pull-up and pull-down on SPI/I2C

 **************************************************************

   See documentation/CHANGELOG for history of OTIMCON

 **************************************************************
*/


#include <SPI.h>          //include the SPI bus library
#include <MFRC522.h>      //include the RFID reader library
#include <Wire.h>
#include <RTClib.h>       // include timer library 
#include <SerialCommand.h>  // include serial command system
#include <EEPROM.h>
#include <LowPower.h>


#define SERIAL_BAUD 38400

// used for debugging purposes. Should be commented out in production or set to 0. Higher number, more information
#define DEBUG 0


// comment these if you don't have them, so the code will be smaller
// preprocessing command are used!
#define USE_EEPROM_BACKUP
#define USE_LED
//#define USE_PIEZO
#define USE_BUZZER

// comment this if you want to use serial only if neccessary. If this is uncommented it will always use serial port, even if nothing is connected.
#define USE_SERIAL_ALWAYS

// comment this if you don't need low power work (the system won't go to sleep but serial will always work)
#define LOW_POWER

// uncomment this if you want to use a little more energy - needed when powered from cheep powerbanks which auto shut off.
// The control will not go to sleep, and will blink a LED on HIGH_POWER_LED (by default pin 4, can be changed)
//#define HIGH_POWER


#define DS3231_I2C_ADDRESS 0x68    // I2C address for DS3231
#define EEEPROM_I2C_ADDRESS 0x57   // default I2C address for EEPROM on DS3231 modules


#define SS_PIN             10                 // slave select pin for SPI (used by MFRC522)
#define PD_PIN              4                 // MFRC522 power down pin
#define RST_PIN             9                 // reset pin

#define FEEDBACK_LED        8                 // pin for LED
#define FEEDBACK_PIEZO      7                 // piezo
#define FEEDBACK_BUZZER     2                 // buzzer

#define SERIAL_ACTIVE_PIN   6                // if you want to use serial only when needed, then select a pin (this is D6) and connect it HIGH 

#define RTC_POWER_PIN       5                // power RTC only when needed, from this pin. Can be changed
#define RTC_POWER_ON        1
#define RTC_POWER_OFF       0

#define HIGH_POWER_LED      8                // will blink when HIGH_POWER is used, as an additional power usage, when cheap powerbanks with auto shut off are used.
#define HIGH_POWER_LED_PERCENTAGE 5          // define, in percentage of a block time of around 10 seconds, how long should a LED be turned on. The bigger the number, the longer LED stays on, the bigger is consumption. Cannot be over 100. 
#define LAST_TIME_FROM_WRITING 30            // number of seconds between two consecutive writes if the same card user comes to the station (from now on called control)
#define PRINT_END_LINE      4                // used in mode PRINT, number of empty lines after the text "Timing by OTIMCON", used to cut the paper off the POS printer

#define ANALOG_REFERENCE  EXTERNAL
//#define VOLTAGE_REFERENCE 2                   // used for measuring the voltage, easier to code if 5V is used (error is 2.4%). It will output 512 for 5V.
#define VOLTAGE_REFERENCE 1024. / 500       // used for measuring the voltage, voltage will be as fixed point arithmetic (100 = 1V)  500 = 5V


//#define VOLTAGE_REFERENCE 3                 // used for measuring the voltage, easier to code if 3.3V is used (error is around 3.3%)
//#define VOLTAGE_REFERENCE 1024. / 330       // used for measuring the voltage, voltage will be as fixed point arithmetic (100 = 1V)  330 = 3.3V


// using internal refence for ADC is suggested but it needs to be done right. So by default this is not done!
//#define ANALOG_REFERENCE  INTERNAL
//#define VOLTAGE_REFERENCE 9                 // used for measuring the voltage, easier to code if 1.1V internal reference is used (error is around 3.4%)
//#define VOLTAGE_REFERENCE 1024. / 110       // used for measuring the voltage, voltage will be as fixed point arithmetic (100 = 1V)  330 = 1.1V



// define ID's for START and FINISH controls. as 1-250 are for controls, available numbers are 253-255 and 0
#define START_CONTROL_ID 251
#define FINISH_CONTROL_ID 252


// define the function of the control
// should convert to enum but I like it this way
#define CLEAR                1                  // works as a clear, and prepares a card for OTIMCON
#define CONTROL              2                  // works as a standard control with id
#define CONTROL_WITH_READOUT 3                  // works as a control which has a readout of all controls in the card
#define READOUT              4                  // works as a readout only of all controls in the card
#define FULL_READOUT         5                  // works as a readout only for the whole card
#define PRINT                6                  // works as a printout, calculates time
#define WRITER               7                  // works as a specialty writer for additional data into cards, and to write controls if needed
#define FROZEN_CONTROL       8                  // works as a control with id, but with time frozen 


#define EEPROM_ADDRESS_MODE       10  // address in EEPROM for saving mode
#define EEPROM_ADDRESS_CONTROL_ID 11  // address in EEPROM for saving control ID  


#define EEPROM_ADDRESS_FROZENTIME 12  // address where the frozen time will be saved, if the mode frozen control is used. Note, it will use 4 bytes!
#define EEPROM_ADDRESS_OPTIONS    16  // different options stored - for POS printers, and time freeze


// change this from time to time for wear leveling, as write is done after _every card_
#define EEPROM_ADDRESS_24C32_LOCATION 20  // location of high part of address in external EEPROM for saving backup of used cards   

#define EXTERNAL_EEPROM_SIZE       4096  // maximum number of bytes in external EEPROM 


// a special code found on byte 0 of block 4, which detects that card is prepared for OTIMCON.
// THIS constant _MUST_ be the _SAME_ at all OTIMCON controls in one competition!
// This is a security if other cards (non-race cards) are put at the OTIMCON control.
// !! Nothing will be written if card is not prepared by touching the CLEAR control.!!
// !! DON'T TOUCH UNLESS YOU KNOW WHAT YOU ARE DOING !!
#define OTIMCON_CARD_TYPE      0xAD


static RTC_DS3231 rtc;
static MFRC522 mfrc522(SS_PIN, RST_PIN);        // instatiate a MFRC522 reader object.
static MFRC522::MIFARE_Key key;                 //create a MIFARE_Key struct named 'key', which will hold the card information

static byte controlId;                         // current ID of this station (from now on called control)
static byte readbackblock[18];                 //This array is used for reading out a block. The MIFARE_Read method requires a buffer that is at least 18 bytes to hold the 16 bytes of a block.
static long currentTime;                       // currentTime, used for saving the time from RTC (without the need of continuos read in functions).


// I know this is very wasteful, but it's easier for anyone who wants to change the code
static boolean useSerial = true;
static boolean cardPresent = false;
static boolean writerCardPresent = false;

static boolean printEscPos = false;        // used in mode PRINT, use EPSON ECS/POS format or not. Set to true if connected directly to POS printer, false if viewed on a serial monitor

static boolean timeToBleep;                // the boolean which starts the bleeping (LED & PIEZO activation)

static byte writerData[30] = {0};
static byte writerJob;

byte controlFunction;

#ifdef USE_EEPROM_BACKUP
static short int locationOnExternalEEPROM = 0;
#endif


static unsigned short deepSleepCounter;
static unsigned short shallowSleepCounter;
static unsigned short wakePowerBankCounter;

static SerialCommand  sCmd   = SerialCommand();       // The demo SerialCommand object
static CommandHandler sHand1 = CommandHandler(sCmd);  // the main command handler
static CommandHandler sHand2 = CommandHandler(sCmd);  // the sub command handler for SET command
static CommandHandler sHand3 = CommandHandler(sCmd);  // the sub command handler for GET command
static CommandHandler sHand4 = CommandHandler(sCmd);  // the sub command handler for WRITE command


/**
   setup function of the Arduino toolchain

*/
void setup() {
  pinMode(RTC_POWER_PIN, OUTPUT); // set pin for output, as RTC and EEPROM will get power from microcontroller
  powerToRTC(RTC_POWER_ON);

  Serial.begin(SERIAL_BAUD);        // Initialize serial communications with the PC
  SPI.begin();               // Init SPI bus

  mfrc522.PCD_Init();        // Init MFRC522 card (in case you wonder what PCD means: proximity coupling device)


  Serial.println(F("Starting..."));
  // if RTC cannot be started, then there is no RTC.
  if (! rtc.begin()) {
    Serial.println(F("No RTC"));
    while (1);   // sketch halts in an endless loop
  }

  // if RTC is running, check the time. If the time in the chip is in the past, then it's set the same as computer clock
  // warning: it will reset back to uploading time, if there is no backup battery on DS3231
  DateTime now = rtc.now();
  DateTime compiled = DateTime(F(__DATE__), F(__TIME__));

  if (now.unixtime() < compiled.unixtime() ) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }


  controlId = EEPROM.read(EEPROM_ADDRESS_CONTROL_ID);              // set ID of the control station. Any number between 1-250. Beware: it can be anything if new Arduino is used.

  controlFunction = EEPROM.read(EEPROM_ADDRESS_MODE);              // set function (mode) of control. Beware: it can be anything if new Arduino is used.
  if (controlFunction > WRITER || controlFunction == 0) controlFunction = READOUT;  // if controlFunction is something crazy (eg. new Arduino), set it to READOUT.

  printEscPos =  ((EEPROM.read(EEPROM_ADDRESS_OPTIONS) & 0x1) > 0) ? true : false;  // set the Epson POS standard or not in PRINT mode
  if (controlFunction == FROZEN_CONTROL) {
    currentTime = EEPROM.get(EEPROM_ADDRESS_FROZENTIME, currentTime);  // sets the time for usage if frozen control is used
  }
  writerJob = 0;


#ifdef USE_EEPROM_BACKUP
  EEPROM.get(EEPROM_ADDRESS_24C32_LOCATION, locationOnExternalEEPROM);
#endif

  // define pin modes for LED and PIEZO, if they are used
#ifdef USE_LED
  pinMode(FEEDBACK_LED, OUTPUT);
#endif
#ifdef USE_PIEZO
  pinMode(FEEDBACK_PIEZO, OUTPUT);
#endif
#ifdef USE_BUZZER
  pinMode(FEEDBACK_BUZZER, OUTPUT);
#endif

  // if USE_SERIAL_ALWAYS is not set, then it's needed to set a pin SERIAL_ACTIVE_PIN
#ifndef USE_SERIAL_ALWAYS
  pinMode(SERIAL_ACTIVE_PIN, INPUT);
#endif

  powerToRTC(RTC_POWER_OFF);



  // Prepare the security key for the read and write functions - all six key bytes are set to 0xFF at chip delivery from the factory.
  // Since the cards in the kit are new and the keys were never defined, they are 0xFF
  // if we had a card that was programmed by someone else, we would need to know the key to be able to access it. This key would then need to be stored in 'key' instead.

  // NOTICE: If you want to block reading and writing cards by somebody else, change block3 of each sector on a card and then change this lines to something else.
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;//keyByte is defined in the "MIFARE_Key" 'struct' definition in the .h file of the library
  }

  /****
  */
#if ANALOG_REFERENCE == INTERNAL
  // uncomment this if you want to use internal analog reference of 1.1V
  // BE CAREFUL WITH SETTING THE INPUT.
  // analogReference(INTERNAL);
#endif
  /*
  ****** */

  // set the main handler for serial command
  sCmd.setHandler(sHand1);

  // Setup callbacks for the main SerialCommand. These are the main commands
  sHand1.addHandler("SET",    sHand2);          // Handler for everything starting with SET
  sHand1.addHandler("GET",    sHand3);          // Handler for everything starting with GET
  sHand1.addHandler("WRITE",  sHand4);          // Handler for everything starting with WRITE
  sHand1.addCommand("PING",   s_pong);          // Handler for PING keepalive messages
  sHand1.addCommand("BLEEP",   bleep);           // Handler for BLEEP
  sHand1.addCommand("?",      s_help);            // Handler for help
  sHand1.setDefault(          s_unrecognized);    // Handler for command that isn't matched  (says "What?")


  // Setup the commands the SET
  sHand2.addCommand("TIME",  s_setTime);         // Set time
  sHand2.addCommand("CTRL",  s_setControl);      // Set control number
  sHand2.addCommand("MODE",  s_setMode);         // Set mode
  sHand2.addCommand("RESET_BACKUP",   s_setResetBackup); // reset backup memory pointer to 0
  sHand2.setDefault(         s_unrecognized);    // Handler for command that isn't matched  (says "What?")

  sHand3.addCommand("TIME",  s_getTime);         // Get current time
  sHand3.addCommand("CTRL",  s_getControl);      // Get current control number
  sHand3.addCommand("MODE",  s_getMode);         // Get current mode
  sHand3.addCommand("VOLTAGE",  s_getVoltage);   // Get battery voltage
  sHand3.addCommand("VERSION",  s_getVersion);   // Get current firmware version
  sHand3.addCommand("BACKUP",  s_getBackup);     // Get current memory backup in external EEPROM
  sHand3.setDefault(         s_unrecognized);    // Handler for command that isn't matched  (says "What?")

  sHand4.addCommand("INFO",  s_writeInfo);         // Write info1 on the card
  sHand4.addCommand("CONTROL",  s_writeControl);     // Writes arbritary control to the next position





#if DEBUG > 0
  Serial.println(F("Setup finished!"));
#endif
  Serial.print(F(">"));

  shallowSleepCounter = 0;
  deepSleepCounter = 0;
  wakePowerBankCounter = 0;
}


void loop()
{
  static byte oldRFIDCardUid[4];
  static long lastBleepTime;


#ifdef HIGH_POWER
  if (!timeToBleep) {
    wakePowerBankCounter++;

    // set this number to define how long the LED should blink.
    if (wakePowerBankCounter < HIGH_POWER_LED_PERCENTAGE)  {
      digitalWrite(HIGH_POWER_LED, HIGH);
    }
    else if (wakePowerBankCounter > 100) {
      wakePowerBankCounter = 0;
    }
    else {
      digitalWrite(HIGH_POWER_LED, LOW);
    }
  }
#endif




  /***
      check serial state - check the #define, by default is on pin D6
  */
#ifdef USE_SERIAL_ALWAYS
  useSerial = true;
#else
  useSerial = (HIGH == digitalRead(SERIAL_ACTIVE_PIN)) ? true : false ;
#endif



  // serial will work for about 15 seconds after the card is read (serial work reset the shallowSleepCounter),
  // but other times the system is just sleep <-> RFID
  if (shallowSleepCounter > 0x1FF) {
    //Serial.flush();
    sleepOTC();
  }

  // check serial, if there is something new on connected link
  if (useSerial) {
    sCmd.readSerial();     // We don't do much, just process serial commands
  }
  shallowSleepCounter++;

  // if LOW_POWER is commented out, then it it will never go to sleep and Serial will be apsolutly responsive.
#ifndef LOW_POWER
  Serial.flush();
  shallowSleepCounter = 0;
#endif


  /*****************************************establishing contact with a tag/card**********************************************************************/

  // should always fail, but needed to have the correct detection of new cards
  // Look for new cards (in case you wonder what PICC means: proximity integrated circuit card)
  if ( ! mfrc522.PICC_IsNewCardPresent()) {     //if PICC_IsNewCardPresent returns 1, a new card has been found and we continue
    return;                                     //if it did not find a new card is returns a '0' and we return to the start of the loop
  }
  else // Select one of the cards
    if ( ! mfrc522.PICC_ReadCardSerial()) {       //if PICC_ReadCardSerial returns 1, the "uid" struct (see MFRC522.h lines 238-45)) contains the ID of the read card.
      cardPresent = false;
      return;                                     //if it returns a '0' something went wrong and we return to the start of the loop
    }


  // If the code comes to here, then a card is found and selected!

  // Sometimes new card is not recognized correctly:
  // 1. check if it's the same card as it was before.
  // 2. even if it's the same card, check the time difference, sometimes it's OK for a runner to revisit the control after a while. Don't use the RTC, use millis for fast check as
  // control shouldn't be in sleep for the last 2 seconds, there is additional check with RTC later on.
  if ( cardPresent ) {
    byte xorId = 0;
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      xorId = xorId ^ mfrc522.uid.uidByte[i] ^ oldRFIDCardUid[i];
    }
    // if different card, say false!
    if ( xorId ) {
      cardPresent = false;
    } // even if same card, if time is over a second, say false and recheck it later
    else if (millis() - lastBleepTime > 1000) {
      cardPresent = false;
    } else { // same card, long push
      cardPresent = true;

      // move the time so doesn't repeat in case of very long card push
      lastBleepTime = millis();
    }
  }



  // DEBUG CODE
#if DEBUG > 0
  if (useSerial) Serial.println(F("card selected"));
#endif
  // END DEBUG CODE


  // at the end of this code cardPresent is set to true. If there is no card on the reader, then it's set to false.
  //
  // Why is that done? If you hold a card for long time, it will constantly try to read it, and then try to write it or whatever.
  // This way a user _must_ remove the card (cardPresent becomes false) before the same or some other card can be presented!
  // It will bleep the same (or not to bleep if there is an error) until card is removed.
  // This is _not_ used if the WRITER mode is in effect, as it expects card to be always present

  if ( ! cardPresent ) {
    timeToBleep = false;
    // controlId 1-252 are controls.
    // 1-250 regular controls, 251 is start, 252 is finish
    // work as control should be set up in controlSetup
    if ( controlFunction == CONTROL || controlFunction == CONTROL_WITH_READOUT  || controlFunction == FROZEN_CONTROL) {
      timeToBleep = writeCurrentControl();
      if ( timeToBleep && useSerial ) {
        Serial.print(F("^"));
        Serial.print(controlId);
        Serial.print(F(","));

        serialPrintUid();

        Serial.print(F(","));
        Serial.print(currentTime);             // this will print just the UNIX timestamp format. This is _by design_!
        Serial.println(F("$"));


        if (controlFunction == CONTROL_WITH_READOUT) {
          readOutControls();
          Serial.println(F("%"));
        }
        Serial.println(F(">"));
      }
    }
    if ( controlFunction == READOUT || controlFunction == FULL_READOUT ) {
      if (  useSerial ) {
        Serial.print(F("Card:"));
        serialPrintUid();
        Serial.println("");
        timeToBleep = readOutControls();
        if (timeToBleep) {
          Serial.println(F("%"));
        }
        Serial.println(F(">"));

      }
    }
    if ( controlFunction == PRINT ) {
      if (  useSerial ) {
        timeToBleep = printControls();
      }
    }


    if ( controlFunction == CLEAR  ) {
      timeToBleep = clearCard();
      if ( useSerial && timeToBleep) {
        Serial.print(F("Cleared card:"));
        serialPrintUid();
        Serial.println(F("$"));
        Serial.println(F(">"));
      }
    }
    if ( controlFunction == WRITER) {

      if (writerJob != 0) {
        timeToBleep = writerWriteData();
        Serial.println(timeToBleep ? "OK" : "FAIL");
      }
    }
  }

  // for some reason, my hardware doesn't work without reinitialization.
  // usually there should be some delay between new use (they say 100ms is enough), so do it prior to bleep which is longer
  //if ((controlFunction != WRITER))
  //     mfrc522.PCD_Init();
  mfrc522.PCD_StopCrypto1();
  delay(1);


  // mark the card as not present before checking the bleep!
  cardPresent = false;

  // if it's time to bleep, then bleep! :D
  // also mark that there is a valid card, and there is no read in reading it again
  if (timeToBleep) {
    bleep();
    cardPresent = true;
    lastBleepTime = millis();
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      oldRFIDCardUid[i] = mfrc522.uid.uidByte[i];
    }
  }

  // this should give a seconds of active time before shallow sleep
  shallowSleepCounter = 0;

  // turn off deep sleep as there are runners who want to use their cards
  deepSleepCounter = 0;





  /*  DEBUG DATA
          for (int block=0; block<60; block++) {
             readBlock(block, readbackblock);//read the block back
             Serial.print(F("read block: "));Serial.println(block);
             for (int j=0 ; j<16 ; j++)//print the block contents
             {
               Serial.print (readbackblock[j],HEX);//Serial.write() transmits the ASCII numbers as human readable characters to serial monitor
               Serial.print(" ");
             }
           }
  */
}
