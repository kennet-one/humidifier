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

class Combiboxi {
  public:
  void echoBri(){
    String briEcho = "20" + String(brightness);
    mesh.sendSingle(624409705,briEcho);
  }
  void watLBox(String msg) {
    if (msg.substring(0, 2) == "19") {
      if (msg.endsWith(String("0"))) {      brightness = 0;   echoBri();
      } else if (msg.endsWith(String("1"))) {      brightness = 26;   echoBri();
      } else if (msg.endsWith(String("2"))) {      brightness = 51;   echoBri();
      } else if (msg.endsWith(String("3"))) {      brightness = 77;   echoBri();
      } else if (msg.endsWith(String("4"))) {      brightness = 102;  echoBri();
      } else if (msg.endsWith(String("5"))) {      brightness = 128;  echoBri();
      } else if (msg.endsWith(String("6"))) {      brightness = 153;  echoBri();
      } else if (msg.endsWith(String("7"))) {      brightness = 179;  echoBri();
      } else if (msg.endsWith(String("8"))) {      brightness = 204;  echoBri();
      } else if (msg.endsWith(String("9"))) {      brightness = 230;  echoBri();
      } else if (msg.endsWith(String("M"))) {      brightness = 255;  echoBri();
      }
    }
  }
  void combi(){FastLED.setBrightness( brightness );}

  void watMod(String msg) {
    if (msg.substring(0, 2) == "18") {
      if (msg.endsWith(String("0"))) { wLed = BLED;  mesh.sendSingle(624409705,"210");
      } else if (msg.endsWith(String("1"))) { wLed = RLED;  mesh.sendSingle(624409705,"211");
      } else if (msg.endsWith(String("2"))) { wLed = GLED;  mesh.sendSingle(624409705,"212");
      } else if (msg.endsWith(String("3"))) { wLed = WLED;  mesh.sendSingle(624409705,"213");
      }
    }
  }
  
  private:
  int brightness = 51;
} combiboxi;


void power(){
  relControl.pimp ();
  relControl.turbine1 ();
  relControl.flo ();
  relControl.ionn ();
  eho();
}

void receivedCallback( uint32_t from, String &msg ) {
  combiboxi.watLBox(msg);
  combiboxi.watMod(msg);

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
  if (msg.equals("huOn")) { power(); }
  
}

class TouchMe {
public:
void touchDetect () {
  if(touch1detected){
    touch1detected = false;
    Serial.println("Touch 7 detected");
  }
  if(touch2detected){
    touch2detected = false;
    Serial.println("Touch 8 detected");
  }
  if(touch3detected){
    touch3detected = false;
    Serial.println("Touch 6 detected");
  }
  if(touch4detected){
    touch4detected = false;
    Serial.println("Touch 5 detected");
  }
  if(touch5detected){
    touch5detected = false;
    Serial.println("Touch 4 detected");
  }
  if(touch5detected){
    touch5detected = false;
    Serial.println("Touch 0 detected");
  }
  if(touch6detected){
    touch6detected = false;
    Serial.println("Touch 9 detected");
  }
}

void touchSet () {
  touchAttachInterrupt(T7, goTouch1, 40);
  touchAttachInterrupt(T8, goTouch2, 40);
  touchAttachInterrupt(T6, goTouch3, 40);
  touchAttachInterrupt(T5, goTouch4, 40);
  touchAttachInterrupt(T4, goTouch5, 40);
  touchAttachInterrupt(T9, goTouch6, 40);
}
bool touch1detected = false;
bool touch2detected = false;
bool touch3detected = false;
bool touch4detected = false;
bool touch5detected = false;
bool touch6detected = false;
private:

} touchMe;

void goTouch1(){ touchMe.touch1detected = true;}
void goTouch2(){ touchMe.touch2detected = true;}
void goTouch3(){ touchMe.touch3detected = true;}
void goTouch4(){ touchMe.touch4detected = true;}
void goTouch5(){ touchMe.touch5detected = true;}
void goTouch6(){ touchMe.touch6detected = true;}

void setup() {
  Serial.begin(115200);

  touchMe.touchSet();

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

  touchMe.touchDetect();
  combiboxi.combi();
  stateWled();
  pmm.pmsIn();
  mesh.update();

}