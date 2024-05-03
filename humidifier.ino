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

WLed getWLed() {
  return wLed;
}

void ledfeedback () {
  WLed ledf = getWLed();
  switch (ledf) {
    case BLED:
      mesh.sendSingle(624409705,"210");
      break;
    case RLED:
      mesh.sendSingle(624409705,"211");
      break;
    case GLED:
      mesh.sendSingle(624409705,"212");
      break;
    case WLED:
      mesh.sendSingle(624409705,"213");
      break;
  }
}

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
bool pomp_State = true; 
bool flowSpin = true; 
bool ionic = true; 

void pimp () {
  if (pomp_State) { activRelay(2); digitalWrite(5, HIGH);
  } else { deactivRelay(2); digitalWrite(5, LOW);
  }
  pomp_State = !pomp_State;
}

enum Tuurbo {
  TURBOOFF,
  TURBO1,
  TURBO2,
  TURBO3
} tuurbo = TURBOOFF;

void fan () {
  switch (tuurbo) {
    case TURBOOFF:
      deactivRelay(5);
      deactivRelay(4);
      deactivRelay(3);

      digitalWrite(15, LOW);
      digitalWrite(2, LOW);
      digitalWrite(4, LOW);
      break;
    case TURBO1:
      deactivRelay(4);
      deactivRelay(3);

      digitalWrite(2, LOW);
      digitalWrite(4, LOW);

      activRelay(5);

      digitalWrite(15, HIGH);
      break;
    case TURBO2:
      deactivRelay(5);
      deactivRelay(3);

      digitalWrite(4, LOW);
      digitalWrite(15, LOW);

      activRelay(4);

      digitalWrite(2, HIGH);
      break;
    case TURBO3:
      deactivRelay(5);
      deactivRelay(4);

      digitalWrite(2, LOW);
      digitalWrite(15, LOW);

      activRelay(3);

      digitalWrite(15, HIGH);
      break;
  }
}
void flo () {
  if (flowSpin) { activRelay(1); digitalWrite(18, HIGH);
  } else { deactivRelay(1); digitalWrite(18, LOW);
  }
  flowSpin = !flowSpin;
}
void ionn () {
  if (ionic) { activRelay(0);
  } else { deactivRelay(0);
  }
  ionic = !ionic; 
}

void power(){
  
  if (tpower == false) {
    digitalWrite(19, LOW);
    deactivRelay(2);
    deactivRelay(1);
    deactivRelay(0);
    x = tuurbo;
    tuurbo = TURBOOFF;
    tpower = true;
    eho();
  } else {
    digitalWrite(19, HIGH);
    activRelay(2);
    activRelay(1);
    activRelay(0);
    tuurbo = x;
    tpower = false;
    eho();
  }
}
private:
const int relayBlock = 0x38; // Адреса PCA8574AD

bool tpower = true;
RelayControl::Tuurbo x = TURBO1;

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

void eho() {
  String x = String (static_cast<int>(relControl.tuurbo));
  String y = (relControl.pomp_State == true) ? "1" : "0";
  String u = (relControl.flowSpin == true) ? "1" : "0";
  String i = (relControl.ionic == true) ? "1" : "0";
  String q = "15" + x + y + u + i;
  mesh.sendSingle(624409705,q);
  combiboxi.echoBri ();
  ledfeedback ();
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
    mesh.sendSingle(624409705, "13" + (relControl.pomp_State ? String("1") : String("0")));
    relControl.pimp ();
   }
  if (msg.equals("140")) { 
    relControl.tuurbo = RelayControl::TURBOOFF;
    mesh.sendSingle(624409705, "140");
   }
  if (msg.equals("141")) { 
    relControl.tuurbo = RelayControl::TURBO1;
    mesh.sendSingle(624409705, "141");
   }
  if (msg.equals("142")) { 
    relControl.tuurbo = RelayControl::TURBO2;
    mesh.sendSingle(624409705, "142");
   }
  if (msg.equals("143")) { 
    relControl.tuurbo = RelayControl::TURBO3;
    mesh.sendSingle(624409705, "143");
   }
  if (msg.equals("flow")) { 

    mesh.sendSingle(624409705, "16" + (relControl.flowSpin ? String("1") : String("0")));
    relControl.flo ();
   }
  if (msg.equals("ion")) { 

    mesh.sendSingle(624409705, "17" + (relControl.ionic ? String("1") : String("0")));
    relControl.ionn ();
   }
  if (msg.equals("echo_turb")) { eho(); }
  if (msg.equals("huOn")) { relControl.power(); }
  
}

class TouchMe {
public:
void touchDetect() {
  if (touchRead(T7) < 40) {
    if (millis() - lostTime1 > delay) { 

      mesh.sendSingle(624409705, "13" + (relControl.pomp_State ? String("1") : String("0")));
      relControl.pimp ();
      lostTime1 = millis();
      }
  }
  if (touchRead(T8) < 40) {
    if (millis() - lostTime2 > delay) { 

      mesh.sendSingle(624409705, "17" + (relControl.ionic ? String("1") : String("0")));
      relControl.ionn ();
      lostTime2 = millis();
      }
  }
  if (touchRead(T6) < 40) {
    if (millis() - lostTime3 > delay) { 

      relControl.power();
      lostTime3 = millis();
    }
  }
  if (touchRead(T5) < 40) {
    if (millis() - lostTime4 > delay) { 

      mesh.sendSingle(624409705, "16" + (relControl.flowSpin ? String("1") : String("0")));
      relControl.flo ();
      
      lostTime4 = millis();
    }
  }
  if (touchRead(T4) < 40) {
    if (millis() - lostTime5 > delay) { 

      //mesh.sendSingle(624409705, "14" + (relControl.turbo_State ? String("1") : String("0")));
      //relControl.turbine1 ();
      
      lostTime5 = millis();
    }
  }
  if (touchRead(T9) < 40) {
    if (millis() - lostTime6 > delay) { 
      Serial.println("Touch 9 detected");
      lostTime6 = millis();
    }
  }
}

private:
unsigned long delay = 500;

unsigned long lostTime1 = 0;
unsigned long lostTime2 = 0;
unsigned long lostTime3 = 0;
unsigned long lostTime4 = 0;
unsigned long lostTime5 = 0;
unsigned long lostTime6 = 0;
}touchMe;


void setup() {
  Serial.begin(115200);

  pinMode(19, OUTPUT);   //повер
  pinMode(18, OUTPUT);   //поворот
  pinMode(5, OUTPUT);    //помпа
  pinMode(4, OUTPUT);    //турбо високий
  pinMode(2, OUTPUT);    //турбо середній
  pinMode(15, OUTPUT);   //турбо низький

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

  relControl.fan();
  touchMe.touchDetect();
  combiboxi.combi();
  stateWled();
  pmm.pmsIn();
  mesh.update();

}