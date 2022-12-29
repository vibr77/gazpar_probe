/*
__   _____ ___ ___        Author: Vincent BESSON
 \ \ / /_ _| _ ) _ \      Release: 0.31
  \ V / | || _ \   /      Date: 20221227
   \_/ |___|___/_|_\      Description: Gazpar probe with NRF24L01 Module
                2022      Licence: Creative Commons
______________________

Release changelog:
  +20221227: Removing Base64 that increase payload size for nothing
*/                 


#include <SPI.h>
#include <RF24.h>
#include "ArduinoLowPower.h"
#include <FlashStorage_SAMD.h>
#include <RTCZero.h>
#include <Base64.h> // https://github.com/agdl/Base64
#include <AES.h>    // https://forum.arduino.cc/index.php?topic=88890.0
#include "pwd.h"

#define pinBAT  A7            // Warning D9 can not be used !!!
#define pinCE   11            
#define pinCSN  10

#define pinRF_ON 5
#define pinPulse 12
            
#define tunnel  "D6E1A"       // On définit un "nom de tunnel" (5 caractères), pour pouvoir communiquer d'un NRF24 à l'autre

AES aes ;
const uint16_t maxMessageSize = 32;                      // message size + 1 NEEDS to be a multiple of N_BLOCK (16)
uint8_t key128bits[] = AES_KEY; 
const uint8_t iv128bits[]  = AES_IV; 

uint32_t init_pulse_index=1000;
uint32_t last_pulse_index=0;
int ee_address = 0;

#define LOGDEBUG  1
//DEBUG 
#if LOGDEBUG==1
#define DEBUG_PRINT(x)       Serial.print(x)
#define DEBUG_PRINTDEC(x)    Serial.print(x, DEC)
#define DEBUG_PRINTHEX(x)    Serial.print(x, HEX)
#define DEBUG_PRINTLN(x)     Serial.println(x)
#define DEBUG_RADIO()        radio.printDetails()
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTDEC(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTHEX(x)
#define DEBUG_RADIO()
#endif

RF24 radio(pinCE, pinCSN);                    // Instanciation du NRF24L01

const byte adresse[6] = tunnel;               // Mise au format "byte array" du nom du tunnel
char message[32];                             // Message à transmettre à l'autre NRF24 (32 caractères maxi, avec cette librairie)

int crypted_len;

volatile uint32_t daLCount       =0;
volatile uint32_t measuredvbat    =0;

RTCZero zerortc;
const byte alarmSeconds = 0;
const byte alarmMinutes = 15;
const byte alarmHours   = 0;

volatile bool alarmFlag = true; // Start awake

const uint32_t epc=1451606400;
volatile uint32_t elapse=0;

void batterieVoltage(){
  measuredvbat = analogRead(pinBAT);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat *= 1000;
  measuredvbat /= 1024; // convert to voltage
  DEBUG_PRINT("VBAT:");
  DEBUG_PRINTLN(measuredvbat);
}

void pulseEvent(){
  DEBUG_PRINT("Pulse.");
  ++daLCount;
}

void radioSetup(){
  radio.begin();                      // Initialisation du module NRF24
  radio.openWritingPipe(adresse);     // Ouverture du tunnel en ÉCRITURE, avec le "nom" qu'on lui a donné
  radio.setPALevel(RF24_PA_HIGH);      // Sélection d'un niveau "MINIMAL" pour communiquer (pas besoin d'une forte puissance, pour nos essais)
  radio.setAutoAck(true);
  radio.stopListening(); 
}

const char* encode_128bits(const char* texteEnClair) {

  // static allocation of buffers to ensure the stick around when returning from the function until the next call
  static uint8_t b_message[maxMessageSize];
  static uint8_t cryptedMessage[maxMessageSize];

  memset(b_message, 0, maxMessageSize);           // padding with 0
  memset(cryptedMessage, 0, maxMessageSize);      // padding with 0

  uint8_t iv[N_BLOCK]; // memory is modified during the call
  memcpy(iv, iv128bits, N_BLOCK);

  uint16_t tailleTexteEnClair = strlen(texteEnClair) + 1; // we grab the trailing NULL char for encoding
  memcpy(b_message, texteEnClair, tailleTexteEnClair);

  if ((tailleTexteEnClair % N_BLOCK) != 0) tailleTexteEnClair = N_BLOCK * ((tailleTexteEnClair / N_BLOCK) + 1);
  int n_block = tailleTexteEnClair / N_BLOCK;
  
  crypted_len=n_block*16;
  
  DEBUG_PRINT("crypted_len:");
  DEBUG_PRINTLN(crypted_len);

  aes.set_key(key128bits, 128);
  aes.cbc_encrypt(b_message, cryptedMessage, n_block, iv); // iv will be modified
  aes.clean();

  return (char *)cryptedMessage;
}
 #if LOGDEBUG==1
  void debughex(const char * message,int len){
    int c;
    for (int n = 0; n <= len; n++){
      c=message[n];
      DEBUG_PRINT("0x");
      DEBUG_PRINT(c < 16 ? "0" : "");
      DEBUG_PRINTHEX(c);
      DEBUG_PRINT(" ");
    }
    DEBUG_PRINTLN(" ");
  }
#endif
void radioSendMessage(){
  
  digitalWrite(pinRF_ON, HIGH);         // Power the NRF24L01
  delay(500);                           // Wait 
  radioSetup();                         // Setup the Radio
  delay(1500);                          // Wait

    sprintf(message,"d:%lu;v:%lu;p:%lu;",elapse,measuredvbat,daLCount);
    
  #if LOGDEBUG==1
    DEBUG_PRINT("clear message:");
    DEBUG_PRINTLN(message);

    DEBUG_PRINT("len clear message:");
    DEBUG_PRINTLN(strlen(message));
  #endif  

    const char* encodedPtr = encode_128bits(message);
    
  #if LOGDEBUG==1
    DEBUG_PRINT("Encoded Msg:");
    debughex(encodedPtr,crypted_len);
  #endif

    radio.write(encodedPtr, crypted_len);   // send the message
   
    delay(1000);
    digitalWrite(pinRF_ON, LOW); 
}

void alarmMatch(void){
  alarmFlag = true; // Set flagx
}

void resetAlarm(void) {
  byte seconds = 0;
  byte minutes = 0;
  byte hours = 0;
  byte day = 1;
  byte month = 1;
  byte year = 1;
  
  zerortc.setTime(hours, minutes, seconds);
  zerortc.setDate(day, month, year);

  zerortc.setAlarmTime(alarmHours, alarmMinutes, alarmSeconds);
  zerortc.enableAlarm(zerortc.MATCH_HHMMSS);
  
  elapse++;

  // check if it is time to store the last index
  if ((elapse % 96) ==0){ // 96*15 min = 1 journée
    DEBUG_PRINT("Saving last pulse index to nvram:");
    
    last_pulse_index=daLCount;
    DEBUG_PRINTLN(last_pulse_index);
    EEPROM.put(ee_address,last_pulse_index);
    if (!EEPROM.getCommitASAP()){
      DEBUG_PRINTLN("CommitASAP not set. Need commit()");
      EEPROM.commit();
    }
  }
}

void setup() {

  Serial.begin(9600);
  delay(2000);
  
  pinMode(pinRF_ON, OUTPUT);
  DEBUG_PRINTLN("init");
 
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  pinMode(13, OUTPUT);              // Switch off Red Charge led
  digitalWrite(13, LOW);            // Save 1 mA

  EEPROM.get(ee_address,last_pulse_index );
  DEBUG_PRINT("Last stored pulse index:");
  DEBUG_PRINTLN(last_pulse_index);
 
  if ( last_pulse_index>4000000000 ||  last_pulse_index<init_pulse_index){
     last_pulse_index=init_pulse_index;
    EEPROM.put(ee_address, last_pulse_index );
    if (!EEPROM.getCommitASAP()){
      DEBUG_PRINTLN("CommitASAP not set. Need commit()");
      EEPROM.commit();
    }
  }

  daLCount= last_pulse_index;
  DEBUG_PRINT("dalCount:");
  DEBUG_PRINTLN(daLCount);

  LowPower.attachInterruptWakeup(pinPulse, pulseEvent, FALLING);

  zerortc.begin();                      // Set up clocks and such
  zerortc.setEpoch(epc);                // Jan 1, 2016
  resetAlarm();                         // Set alarm
  zerortc.attachInterrupt(alarmMatch);  // Set up a handler for the alarm

}

void loop() {
  DEBUG_PRINTLN("looping");
  
  if (alarmFlag==false){
    DEBUG_PRINT("pulse index: ");
    DEBUG_PRINTLN(daLCount);
  }
  else{
    DEBUG_PRINTLN("alarmFlag: TRUE");
    
    batterieVoltage();
    radioSendMessage();
    
    alarmFlag = false;    // Clear flag
    resetAlarm();         // Reset alarm before returning to sleep
  }

  zerortc.standbyMode();    // Sleep until next alarm match
}


/* END */

