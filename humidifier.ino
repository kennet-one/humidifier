#include "HardwareSerial.h"
#include "PMS.h"
#include "painlessMesh.h"

#define   MESH_PREFIX     "kennet"
#define   MESH_PASSWORD   "kennet123"
#define   MESH_PORT       5555

painlessMesh  mesh;

HardwareSerial pmsSerial(1); // UART1

PMS pms(pmsSerial);
PMS::DATA data;

unsigned long lastReadTime = 0;
unsigned long lastWakeTime = 0;
unsigned long lastRequestTime = 0;
const unsigned long readInterval = 30000; // Час до наступного читання (30 секунд)
const unsigned long wakeInterval = 60000; // Час до пробудження (60 секунд)
const unsigned long waitTime = 1000; // Час очікування даних після запиту

enum State {
  WAKE,
  WAIT_FOR_READ,
  READ,
  SLEEP
};
State state = WAKE;

void receivedCallback( uint32_t from, String &msg ) {

  String str1 = msg.c_str();
  String str2 = "PMS";

  if (str1.equals(str2)) {
    String x = ("pm1"); 
    mesh.sendSingle(624409705,x);
  }
}

void setup() {
  Serial.begin(115200);  

  pmsSerial.begin(9600, SERIAL_8N1, 16, 17); // Налаштування UART для PMS
  pms.passiveMode();   

  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);
}

void loop(){
  
  mesh.update();

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
          Serial.print("PM 1.0 (ug/m3): ");
          Serial.println(data.PM_AE_UG_1_0);

          Serial.print("PM 2.5 (ug/m3): ");
          Serial.println(data.PM_AE_UG_2_5);

          Serial.print("PM 10.0 (ug/m3): ");
          Serial.println(data.PM_AE_UG_10_0);
        }

        pms.sleep();
        lastReadTime = currentMillis;
        state = SLEEP;
      }
      break;
  }

}
