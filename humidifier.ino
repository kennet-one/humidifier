#include "HardwareSerial.h"
#include "PMS.h"
#include "painlessMesh.h"
#include "Wire.h"
#include "FastLED.h"

#define   MESH_PREFIX     "kennet"
#define   MESH_PASSWORD   "kennet123"
#define   MESH_PORT       5555

painlessMesh  mesh;

CRGB waterLed[1];

HardwareSerial pmsSerial(1); // UART1

PMS pms(pmsSerial);

enum WLed {
  BLED,
  RLED,
  GLED,
  WLED
} wLed = BLED;

void stateWled (){
  switch (wLed) {
    case BLED : {
      waterLed[0] = CRGB::Black;
      FastLED.show();
    } break;

    case RLED : {
      waterLed[0] = CRGB::Red;
      FastLED.show();
    } break;

    case GLED : {
      waterLed[0] = CRGB::Green;
      FastLED.show();
    } break;

    case WLED : {
      waterLed[0] = CRGB::White;
      FastLED.show();
    } break;
  }
}

class RelayControl {
public:
void activRelay(int y) {
  byte x = rdRelayBlock();
  x &= ~(1 << y);
  wrRelayBlock(x);
}
void deactivRelay(int y) {
  byte x = rdRelayBlock();
  x |= (1 << y);
  wrRelayBlock(x);
}
void wrRelayBlock(byte x) {
  Wire.beginTransmission(relayBlock);
  Wire.write(x);
  Wire.endTransmission();
}
bool turbo_State = true; 
bool turboM_State = true; 
bool turboH_State = true; 
bool pomp_State = true; 
bool flowSpin = true; 
bool ionic = true; 

void pimp () {
  if (pomp_State) { activRelay(2);
  } else { deactivRelay(2);
  }
  pomp_State = !pomp_State;
}
void turbine1 () {
  if (turbo_State) { activRelay(5);
  } else { deactivRelay(5);
  }
  turbo_State = !turbo_State; 
}
void flo () {
  if (flowSpin) { activRelay(1);
  } else { deactivRelay(1);
  }
  flowSpin = !flowSpin;
}
void ionn () {
  if (ionic) { activRelay(0);
  } else { deactivRelay(0);
  }
  ionic = !ionic; 
}
private:
const int relayBlock = 0x38; // Адреса PCA8574AD

byte rdRelayBlock() {
  Wire.requestFrom(relayBlock, (byte)1);
  if (Wire.available()) {
    return Wire.read();
  }
}
} relControl;

class Pmm {
public:
  Pmm() : lastReadTime(0), lastWakeTime(0), lastRequestTime(0), state(SLEEP),
            readInterval(30000), wakeInterval(60000), waitTime(1000) {}

  enum State {
    WAKE,
    WAIT_FOR_READ,
    READ,
    SLEEP
  } state;
  
  void pmsIn(){
  unsigned long currentMillis = millis();
  switch (state) {
    case WAKE:
      if (currentMillis - lastWakeTime >= wakeInterval) {
        lastWakeTime = currentMillis;
        pms.wakeUp();
        state = WAIT_FOR_READ;
      }
    break;

    case WAIT_FOR_READ:
      if (currentMillis - lastWakeTime >= readInterval) {
        pms.requestRead();
        lastRequestTime = currentMillis;
        state = READ;
      }
    break;

    case READ:
      if (currentMillis - lastRequestTime >= waitTime) {
        if (pms.readUntil(data)) {

          String x = "10"+ String(data.PM_AE_UG_1_0);
          mesh.sendSingle(624409705,x);

          String y = "11"+ String(data.PM_AE_UG_2_5);
          mesh.sendSingle(624409705,y);

          String w = "12"+ String(data.PM_AE_UG_10_0);
          mesh.sendSingle(624409705,w);
        }

        pms.sleep();
        lastReadTime = currentMillis;
        state = SLEEP;
      }
      break;
    }
  }

private:
  const unsigned long readInterval; // Час до наступного читання (30 секунд)
  const unsigned long wakeInterval; // Час до пробудження (60 секунд)
  const unsigned long waitTime;     // Час очікування даних після запиту

  unsigned long lastReadTime;
  unsigned long lastWakeTime;
  unsigned long lastRequestTime;
  PMS::DATA data;
  
};
Pmm pmm;

void eho() {
  String x = (relControl.turbo_State == true) ? "1" : "0";
  String y = (relControl.pomp_State == true) ? "1" : "0";
  String u = (relControl.flowSpin == true) ? "1" : "0";
  String i = (relControl.ionic == true) ? "1" : "0";
  String q = "15" + x + y + u + i;
  mesh.sendSingle(624409705,q);
}

void power(){
  relControl.pimp ();
  relControl.turbine1 ();
  relControl.flo ();
  relControl.ionn ();
  eho();
}

void receivedCallback( uint32_t from, String &msg ) {

  if (msg.equals("pm1")) { 
    String x = ("pm155555555555555"); 
    mesh.sendSingle(624409705,x);
    pmm.state = Pmm::WAKE;
   }
  if (msg.equals("pomp")) { 
    String x = (relControl.pomp_State == true) ? "1" : "0";
    x = "13" + x;
    mesh.sendSingle(624409705,x);
    relControl.pimp ();
   }
  if (msg.equals("turbo1")) { 
    String x = (relControl.turbo_State == true) ? "1" : "0";
    x = "14" + x;
    mesh.sendSingle(624409705,x);
    relControl.turbine1 ();
   }
  if (msg.equals("flow")) { 
    String x = (relControl.flowSpin == true) ? "1" : "0";
    x = "16" + x;
    mesh.sendSingle(624409705,x);
    relControl.flo ();
   }
  if (msg.equals("ion")) { 
    String x = (relControl.ionic == true) ? "1" : "0";
    x = "17" + x;
    mesh.sendSingle(624409705,x);
    relControl.ionn ();
   }
  if (msg.equals("echo_turb")) { eho(); }
  if (msg.equals("watG")) { wLed = GLED; }
  if (msg.equals("watR")) { wLed = RLED; }
  if (msg.equals("watB")) { wLed = BLED; }
  if (msg.equals("watW")) { wLed = WLED;} 
  if (msg.equals("huOn")) { power(); }
}


void setup() {
  Serial.begin(115200);  

  Wire.begin(21, 22);

  FastLED.addLeds<WS2811, 25, BRG>(waterLed, 1);
  
  FastLED.setBrightness(50);

  relControl.wrRelayBlock(0xFF);    //всі піни у стан (HIGH) 

  pmsSerial.begin(9600, SERIAL_8N1, 16, 17); // Налаштування UART для PMS
  pms.passiveMode();   

  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);
}

void loop(){

  stateWled();
  pmm.pmsIn();
  mesh.update();

}