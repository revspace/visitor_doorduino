#include <Entropy.h>
#include <OneWire.h>
#include "ds1961.h"
#include "hexutil.c"
/*

  5V -----\/\/\-----+
          R=2k      |
  10 ---------------+------------o   o-----1wire----+
                                                    |
                            +----o   o-----door     |
                            |              opener   |
                         |--+                |      |
  11 -----\/\/\---+------| NPN mosfet        |      |
          R=150   |      |--+ (e.g. irf540)  |      |
                  /         |              +12V     |
                  \         |                       |
                  / R=10k   |                       |
                  \         |                       |
                  |         |                       |
 GND -------------+---------+----o   o--------------+
                                                    |
                                                    |
  12 -----\/\/\------------------o   o------>|------+
          R=220                             red     |
                                                    |
  13 -----\/\/\------------------o   o------>|------+
          R=220                            green    |
                                                    |
  Optional:                                  |      |
                                           -----    |
   9 ----------------------------o   o-----o   o----+
                    |
  5V -----\/\/\-----+
          R=1k

*/


#define PIN_LED_GREEN 13
#define PIN_LED_RED   12
#define PIN_UNLOCK    11
#define PIN_1WIRE     10
#define PIN_BUTTON     9

#define PIN_STEP1 2
#define PIN_STEP2 3
#define PIN_OK 4
#define PIN_ERROR 5

#define OFF    0
#define GREEN  1
#define RED    2
#define YELLOW (GREEN | RED)

OneWire ds(PIN_1WIRE);
DS1961  sha(&ds);

const int delay_access   =  6000;
const int delay_noaccess = 10000;

byte id[8];

void led (byte color) {
  digitalWrite(PIN_LED_GREEN, color & GREEN);
  digitalWrite(PIN_LED_RED,   color & RED);
}

void setup () {
  Serial.begin(115200);
  Serial.println("RESET");
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED,   OUTPUT);
  pinMode(PIN_UNLOCK,    OUTPUT);
  pinMode(PIN_BUTTON,    INPUT);

  pinMode(PIN_STEP1, OUTPUT);
  pinMode(PIN_STEP2, OUTPUT);
  pinMode(PIN_OK,    OUTPUT);
  pinMode(PIN_ERROR, OUTPUT);

  digitalWrite(PIN_BUTTON, HIGH);
  Entropy.Initialize();

  led(YELLOW);
}

static bool connected = false;
static unsigned long error_flash;

void error () {
  connected = false;
  error_flash = millis() + 200;
}

void loop () {
  char challenge[3];
  
  static unsigned long keepalive = 0;

  if (! (connected && ds.reset())) {  // read ds.reset() as ds.still_connected()
    connected = false;
    ds.reset_search();
    if (ds.search(id)) {
      if (OneWire::crc8(id, 7) != id[7]) return;
      connected = true;
      led(OFF);
      Serial.print("<");
      for (byte i = 0; i < 8; i++) {
        if (id[i] < 16) Serial.print("0");
        Serial.print(id[i], HEX);
      }
      Serial.println(">");
    }
  }
  
  int button = 0;
  for (; button < 10 && !digitalRead(PIN_BUTTON); button++) delay(1);
  if (button >= 10) {
    Serial.println("<BUTTON>");
  }

  if (!connected && error_flash && error_flash < millis()) {
    error_flash = 0;
    for (int i = 0; i < 5; i++) {
      led(OFF);
      delay(50);
      led(GREEN);
      delay(25);
    }
  }

  if (connected) {
    led(OFF);
  } else {
    unsigned int m = millis() % 3000;
    bool have_comm = (keepalive && millis() - 5000 < keepalive);
    led( 
      ((m > 2600 && m <= 2700) || (m > 2900))
      ? (have_comm ? OFF : RED)
      : (have_comm ? YELLOW : OFF)
    );
  }

  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == 'A') {
      // XXX Wat als een challenge ooit "A" bevat?
      Serial.println("ACCESS");
      led(GREEN);
      digitalWrite(PIN_UNLOCK, HIGH);
      delay(delay_access);
      digitalWrite(PIN_UNLOCK, LOW);
      led(YELLOW);
      keepalive = millis();
      error_flash = 0;
    }
    else if (c == 'N') {
      Serial.println("NO ACCESS");
      led(RED);
      delay(delay_noaccess);
      led(YELLOW);
      keepalive = millis();
      error_flash = 0;
    } else if (c == 'C') {
      led(OFF);

      unsigned char page[1];

      if (Serial.readBytes(page, 1) != 1) return;
      if (Serial.readBytes(challenge, 3) != 3) return;

      if (! ibutton_challenge(page[0], (byte*) challenge)) {
        Serial.println("CHALLENGE ERROR");
        if (!ds.reset()) error();
        return;
      }
    } else if (c == 'X') {
      led(OFF);

      unsigned char page[1];
      char newdata[8];
      char mac[20];
      unsigned char offset[1];
      if (Serial.readBytes(page, 1) != 1) return;
      if (Serial.readBytes(challenge, 3) != 3) return;
      if (Serial.readBytes(offset, 1) != 1) return;
      if (Serial.readBytes(newdata, 8) != 8) return;
      if (Serial.readBytes(mac, 20) != 20) return;

      if (! sha.WriteData(NULL, page[0] * 32 + offset[0], (uint8_t*) newdata, (uint8_t*) mac)) {
        Serial.println("EEPROM WRITE ERROR");
        error();
        return;
      }
      if (! ibutton_challenge(page[0], (byte*) challenge)) {
        Serial.println("EXTENDED CHALLENGE ERROR");
        error();
        return;
      }
    } else if (c == 'K') {
      keepalive = millis();
      Serial.println("<K>");
    } else if (c == 'P') { //Program
      Serial.println("<P>");
      byte secret[8];
      int i;
      
      for (i = 0; i < 8; i++) {
        secret[i] = Entropy.random(256);
      }
      int x;
      
      while (1) { // Have different way to cancel?
        ds.reset_search();
        if (x > 150) {
          led(OFF);
          if (x > 300) {
            x = 0;
          }
        } else {
          if (x%2 == 0) {
            led(GREEN);
          } else {
            led(RED);
          }
        }
        x++;

        if (ds.search(id)) {
          if (OneWire::crc8(id, 7) != id[7]) return;
          delay(100);
          hexdump(id, 8);
          Serial.print(":");
    
          if (!sha.WriteSecret(id, secret)) {
            Serial.println("ERROR");
            led(RED);
          } else {
            hexdump(secret, 8);
            Serial.println("");
            led(GREEN);
          }
          delay(5000);
          break;
        }
      }
      led( millis() % 150 > 120 ? OFF : RED);

    } else if (c == 'D') { //Stap 1: deelnemer
      Serial.println("<D>");
      digitalWrite(PIN_STEP1, HIGH);
      digitalWrite(PIN_STEP2, LOW);
      digitalWrite(PIN_OK, LOW);
      digitalWrite(PIN_ERROR, LOW);
    } else if (c == 'V') { //Stap 2: visitor
      Serial.println("<V>");
      digitalWrite(PIN_STEP1, LOW);
      digitalWrite(PIN_STEP2, HIGH);
      digitalWrite(PIN_OK, LOW);
      digitalWrite(PIN_ERROR, LOW);
    } else if (c == 'O') { //OK
      Serial.println("<O>");
      digitalWrite(PIN_STEP1, LOW);
      digitalWrite(PIN_STEP2, LOW);
      digitalWrite(PIN_OK, HIGH);
      digitalWrite(PIN_ERROR, LOW);
    } else if (c == 'E') { //ERROR
      Serial.println("<E>");
      digitalWrite(PIN_STEP1, LOW);
      digitalWrite(PIN_STEP2, LOW);
      digitalWrite(PIN_OK, LOW);
      digitalWrite(PIN_ERROR, HIGH);
    }

  }
  
  //while (Serial.available()) Serial.read();
}

bool ibutton_challenge(byte page, byte* challenge) {
  uint8_t data[32];
  uint8_t mac[20];
  
  if (! sha.ReadAuthWithChallenge(NULL, page * 32, challenge, data, mac)) {
    return false;
  }
  Serial.print("<");
  hexdump(data, 32);
  Serial.print(" ");
  hexdump(mac, 20);
  Serial.println(">");  
  return true;
}

void hexdump(byte* string, int size) {
  for (int i = 0; i < size; i++) {
    Serial.print(string[i] >> 4, HEX);
    Serial.print(string[i] & 0xF, HEX);
  }
}
