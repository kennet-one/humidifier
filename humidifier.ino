// сенсорний датчик того шо вода кіньчилася
// сенсор уровня 50 і 100 процент води
// почистити код від мусора
// проблема з помпою, мені кажеця шо якшо підключити помпу до 12в лінії то баг пройде
#include "HardwareSerial.h"
#include "PMS.h"
#include "painlessMesh.h"
#include "Wire.h"
#include "FastLED.h"
#include <Adafruit_AW9523.h>
#include "mash_parameter.h"

Scheduler userScheduler;
painlessMesh  mesh;

Adafruit_AW9523 aw;

CRGB waterLed[1];

HardwareSerial pmsSerial(1); // UART1

PMS pms(pmsSerial);

uint8_t L0 = 0;  // 0 thru 15
uint8_t L1 = 1;  
uint8_t L2 = 2;  
uint8_t L3 = 3;  
uint8_t L4 = 4;  
uint8_t L5 = 5;  

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
  byte x = rdRelayBlock();  // Зчитуємо поточний стан блоку реле
  
  switch (y) {
    case 0:
      x &= 0xFE;  // Змінюємо тільки перший біт (00000001 -> 11111110)
      break;
    case 1:
      x &= 0xFD;  // Змінюємо тільки другий біт (00000010 -> 11111101)
      break;
    case 2:
      x &= 0xFB;  // Змінюємо тільки третій біт (00000100 -> 11111011)
      break;
    case 3:
      x &= 0xF7;  // Змінюємо тільки четвертий біт (00001000 -> 11110111)
      break;
    case 4:
      x &= 0xEF;  // Змінюємо тільки п'ятий біт (00010000 -> 11101111)
      break;
    case 5:
      x &= 0xDF;  // Змінюємо тільки шостий біт (00100000 -> 11011111)
      break;
    case 6:
      x &= 0xBF;  // Змінюємо тільки сьомий біт (01000000 -> 10111111)
      break;
    case 7:
      x &= 0x7F;  // Змінюємо тільки восьмий біт (10000000 -> 01111111)
      break;
  }
  
  wrRelayBlock(x);  // Записуємо оновлений стан назад у блок реле
}

void deactivRelay(int y) {
  byte x = rdRelayBlock();  // Зчитуємо поточний стан блоку реле
  
  switch (y) {
    case 0:
      x |= 0x01;  // Встановлюємо перший біт (00000001)
      break;
    case 1:
      x |= 0x02;  // Встановлюємо другий біт (00000010)
      break;
    case 2:
      x |= 0x04;  // Встановлюємо третій біт (00000100)
      break;
    case 3:
      x |= 0x08;  // Встановлюємо четвертий біт (00001000)
      break;
    case 4:
      x |= 0x10;  // Встановлюємо п'ятий біт (00010000)
      break;
    case 5:
      x |= 0x20;  // Встановлюємо шостий біт (00100000)
      break;
    case 6:
      x |= 0x40;  // Встановлюємо сьомий біт (01000000)
      break;
    case 7:
      x |= 0x80;  // Встановлюємо восьмий біт (10000000)
      break;
  }
  
  wrRelayBlock(x);  // Записуємо оновлений стан назад у блок реле
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
  if (pomp_State) { activRelay(0); 
  } else { deactivRelay(0); 
  }
  pomp_State = !pomp_State;
  mesh.sendSingle(624409705, "13" + (pomp_State ? String("0") : String("1")));
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

      break;
    case TURBO1:
      deactivRelay(4);
      deactivRelay(3);


      activRelay(5);

      break;
    case TURBO2:
      deactivRelay(5);
      deactivRelay(3);


      activRelay(4);

      break;
    case TURBO3:
      deactivRelay(5);
      deactivRelay(4);


      activRelay(3);

      break;
  }
}
void flo () {
  if (flowSpin) { activRelay(1);
  } else { deactivRelay(1);
  }
  flowSpin = !flowSpin;
  mesh.sendSingle(624409705, "16" + (flowSpin ? String("0") : String("1")));
}
void ionn () {
  if (ionic) { activRelay(2);
  } else { deactivRelay(2);
  }
  ionic = !ionic; 
  mesh.sendSingle(624409705, "17" + (ionic ? String("0") : String("1")));
}

void power(){
  
  if (tpower == false) {
    deactivRelay(2); 
    ionic = true; 
    //ionn();
    deactivRelay(1);
    flowSpin = true;
    //flo();
    deactivRelay(0);
    pomp_State = true;
    //pimp();
    x = tuurbo;
    tuurbo = TURBOOFF;
    tpower = true;
    eho();
  } else {
    activRelay(2);
    ionic = false; 
    //ionn();
    activRelay(1);
    flowSpin = false;
    //flo ();
    activRelay(0);
    pomp_State = false;
    //pimp();
    tuurbo = x;
    tpower = false;
    eho();
  }
}

void printFirstSixRelaysState() {
  byte x = rdRelayBlock();
  for (int i = 0; i < 6; ++i) {
    bool state = (x & (1 << i)) == 0; // true, якщо реле увімкнене (0)
    Serial.print("Relay ");
    Serial.print(i);
    switch (i) {
      case 0: 
       Serial.print("00000");
       Serial.print(state);
       break;

      case 1: 
       Serial.print("11111");
       Serial.print(state);
       break;

      case 2: 
       Serial.print("22222");
       Serial.print(state);
       break;

      case 3: 
       Serial.print("33333");
       Serial.print(state);
       break;

      case 4: 
       Serial.print("44444");
       Serial.print(state);
       break;

      case 5: 
       Serial.print("55555");
       Serial.print(state);
       break;

      case 6: 
       Serial.print("666666");
       Serial.print(state);
       break;

      case 7: 
       Serial.print("77777");
       Serial.print(state);
       break;
    }
    Serial.print(": ");
    Serial.println(state ? "ON" : "OFF");
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

class Pmm {        //мкг/м3
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
  relControl.printFirstSixRelaysState();
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

    relControl.pimp ();    
    //mesh.sendSingle(624409705, "13" + (relControl.pomp_State ? String("1") : String("0")));
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

    relControl.flo ();
    //mesh.sendSingle(624409705, "16" + (relControl.flowSpin ? String("1") : String("0")));
   }
  if (msg.equals("ion")) { 

    relControl.ionn ();
    //mesh.sendSingle(624409705, "17" + (relControl.ionic ? String("1") : String("0")));
   }
  if (msg.equals("echo_turb")) { eho(); }
  if (msg.equals("huOn")) { relControl.power(); }
  
}

class TouchMe {
public:
void touchDetect() {
  //if (touchRead(T7) < 40) {
    //if (millis() - lostTime1 > delay) { 

      //mesh.sendSingle(624409705, "13" + (relControl.pomp_State ? String("1") : String("0")));
      //relControl.pimp ();
      //lostTime1 = millis();
      //}
  //}
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

  Wire.begin(21, 22);

  FastLED.addLeds<WS2811, 25, BRG>(waterLed, 1);
  
  FastLED.setBrightness(50);

  relControl.wrRelayBlock(0xFF);    //всі піни у стан (HIGH) 

  pmsSerial.begin(9600, SERIAL_8N1, 16, 17); // Налаштування UART для PMS
  pms.passiveMode();   

  mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT );
  mesh.onReceive(&receivedCallback);

  aw.pinMode(L0, OUTPUT);
  aw.pinMode(L1, OUTPUT);
  aw.pinMode(L2, OUTPUT);
  aw.pinMode(L3, OUTPUT);
  aw.pinMode(L4, OUTPUT);
  aw.pinMode(L5, OUTPUT);
}

void loop(){

  aw.digitalWrite(L0, HIGH);
  delay(100);
  aw.digitalWrite(L0, LOW);
  delay(100);
  aw.digitalWrite(L1, HIGH);
  delay(100);
  aw.digitalWrite(L1, LOW);
  delay(100);
  aw.digitalWrite(L2, HIGH);
  delay(100);
  aw.digitalWrite(L2, LOW);
  delay(100);
  aw.digitalWrite(L3, HIGH);
  delay(100);
  aw.digitalWrite(L3, LOW);
  delay(100);
  aw.digitalWrite(L4, HIGH);
  delay(100);
  aw.digitalWrite(L4, LOW);
  delay(100);
  aw.digitalWrite(L5, HIGH);
  delay(100);
  aw.digitalWrite(L5, LOW);
  delay(100);

  relControl.fan();
  //touchMe.touchDetect();
  combiboxi.combi();
  stateWled();
  pmm.pmsIn();
  mesh.update();

}