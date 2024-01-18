#include "HardwareSerial.h"
#include "PMS.h"
#include "painlessMesh.h"
#include "Wire.h"

#define   MESH_PREFIX     "kennet"
#define   MESH_PASSWORD   "kennet123"
#define   MESH_PORT       5555

painlessMesh  mesh;

HardwareSerial pmsSerial(1); // UART1

PMS pms(pmsSerial);

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

enum Cback {
  PMF,
  ECHO,
  POMP,
  TURBO1,
  PM1,
  FLOWS,
  IONIC
};

void receivedCallback( uint32_t from, String &msg ) {
  Cback fitback = PMF;

  if (msg.equals("pm1")) { fitback = PM1; }
  if (msg.equals("pomp")) { fitback = POMP; }
  if (msg.equals("turbo1")) { fitback = TURBO1; }
  if (msg.equals("flow")) { fitback = FLOWS; }
  if (msg.equals("ion")) { fitback = IONIC; }
  if (msg.equals("echo_turb")) { fitback = ECHO; }

  switch (fitback) {
    case PM1 : {
        String x = ("pm155555555555555"); 
        mesh.sendSingle(624409705,x);
        pmm.state = Pmm::WAKE;
    } break;

    case POMP : {
        String x = (relControl.pomp_State == true) ? "1" : "0";
        x = "13" + x;
        mesh.sendSingle(624409705,x);
        if (relControl.pomp_State) { relControl.activRelay(2);
        } else { relControl.deactivRelay(2);
        }
        relControl.pomp_State = !relControl.pomp_State;
    }break;

    case TURBO1 : {
        String x = (relControl.turbo_State == true) ? "1" : "0";
        x = "14" + x;
        mesh.sendSingle(624409705,x);
        if (relControl.turbo_State) { relControl.activRelay(5);
        } else { relControl.deactivRelay(5);
        }
        relControl.turbo_State = !relControl.turbo_State; 
    }break;

    case FLOWS : {
        String x = (relControl.flowSpin == true) ? "1" : "0";
        x = "16" + x;
        mesh.sendSingle(624409705,x);
        if (relControl.flowSpin) { relControl.activRelay(1);
        } else { relControl.deactivRelay(1);
        }
        relControl.flowSpin = !relControl.flowSpin;
    }break;

    case IONIC : {
        String x = (relControl.ionic == true) ? "1" : "0";
        x = "17" + x;
        mesh.sendSingle(624409705,x);
        if (relControl.ionic) { relControl.activRelay(0);
        } else { relControl.deactivRelay(0);
        }
        relControl.ionic = !relControl.ionic; 
    }break;

      case ECHO : {
        String x = (relControl.turbo_State == true) ? "1" : "0";
        String y = (relControl.pomp_State == true) ? "1" : "0";
        String u = (relControl.flowSpin == true) ? "1" : "0";
        String i = (relControl.ionic == true) ? "1" : "0";
        String q = "15" + x + y + u + i;
        mesh.sendSingle(624409705,q);
    }break;
  }
}


void setup() {
  Serial.begin(115200);  

  Wire.begin(21, 22);

  relControl.wrRelayBlock(0xFF);    //всі піни у стан (HIGH) 

  pmsSerial.begin(9600, SERIAL_8N1, 16, 17); // Налаштування UART для PMS
  pms.passiveMode();   

  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);
}

void loop(){

  pmm.pmsIn();
  mesh.update();

}