#include "HardwareSerial.h"
#include "PMS.h"

HardwareSerial pmsSerial(1); // UART1

PMS pms(pmsSerial);
PMS::DATA data;

void setup() {
  Serial.begin(115200);  
  pmsSerial.begin(9600, SERIAL_8N1, 16, 17); // Налаштування UART для PMS
  pms.passiveMode();   
}

void loop()
{
  pms.wakeUp();
  delay(30000);

  pms.requestRead();
  delay(1000);

  if (pms.readUntil(data))
  {
    Serial.print("PM 1.0 (ug/m3): ");
    Serial.println(data.PM_AE_UG_1_0);

    Serial.print("PM 2.5 (ug/m3): ");
    Serial.println(data.PM_AE_UG_2_5);

    Serial.print("PM 10.0 (ug/m3): ");
    Serial.println(data.PM_AE_UG_10_0);
  }

  Serial.println("Going to sleep for 60 seconds.");
  pms.sleep();
  delay(60000);
}
