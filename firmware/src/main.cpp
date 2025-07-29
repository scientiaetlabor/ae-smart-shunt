#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEadvertisedDevice.h>
#include "NimBLEBeacon.h"
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <INA226_WE.h>
#include <aes/esp_aes.h>        // AES library for decrypting the Victron manufacturer data.

#include "passwords.h"

#define USE_ADC // allows the ADC to be polled
#define manDataSizeMax 31
#define I2C_ADDRESS 0x40

const float ohms = 0.001078; // 100A shunt
float batteryCapacity = 100.00; // 100AH battery
unsigned long lastUpdateTime = 0;
float remainingBatteryTime;

const int scanTime = 5; //In seconds
const int alertPin = 7; // ToDo: use this
float shuntVoltage_mV = -1;
float loadVoltage_V = -1;
float busVoltage_V = -1;
float current_mA = -1;
float power_mW = -1; 
int keyBits = 128;  // Number of bits for AES-CTR decrypt.

char savedDeviceName[32];   // cached copy of the device name (31 chars max) + \0

// REPLACE WITH THE MAC Address of your receiver 
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Variable to store if sending data was successful
String success;

BLEScan* pBLEScan;
BLEUtils utils;

INA226_WE ina226 = INA226_WE(I2C_ADDRESS);

// Must use the "packed" attribute to make sure the compiler doesn't add any padding to deal with
// word alignment.
typedef struct {
  uint16_t vendorID;                    // vendor ID
  uint8_t beaconType;                   // Should be 0x10 (Product Advertisement) for the packets we want
  uint8_t unknownData1[3];              // Unknown data
  uint8_t victronRecordType;            // Should be 0x01 (Solar Charger) for the packets we want
  uint16_t nonceDataCounter;            // Nonce
  uint8_t encryptKeyMatch;              // Should match pre-shared encryption key byte 0
  uint8_t victronEncryptedData[21];     // (31 bytes max per BLE spec - size of previous elements)
  uint8_t nullPad;                      // extra byte because toCharArray() adds a \0 byte.
} __attribute__((packed)) victronManufacturerData;

typedef struct {
   uint8_t deviceState;
   uint8_t outputState;
   uint8_t errorCode;
   uint16_t alarmReason;
   uint16_t warningReason;
   uint16_t inputVoltage;
   uint16_t outputVoltage;
   uint32_t offReason;
   uint8_t  unused[32];                  // Not currently used by Victron, but it could be in the future.
} __attribute__((packed)) victronPanelData;


typedef struct struct_message_voltage0 { // Voltage message
  int messageID = 3;
  bool dataChanged = 0; // stores whether or not the data in the struct has changed
  float frontMainBatt1V = -1;
  float frontAuxBatt1V = -1;
  float rearMainBatt1V = -1;
  float rearAuxBatt1V = -1;
  float frontMainBatt1I = -1;
  float frontAuxBatt1I = -1;
  float rearMainBatt1I = -1;
  float rearAuxBatt1I = -1; 
} struct_message_voltage0;

struct_message_voltage0 localVoltage0Struct;

esp_now_peer_info_t peerInfo;

void sendMessage ()
{
  // create the data
  //localReadings0StructStruct.incomingio1Name[0] = 'Test';

  // Send message via ESP-NOW
  esp_err_t result1 = esp_now_send(broadcastAddress, (uint8_t *) &localVoltage0Struct, sizeof(localVoltage0Struct));
  if (result1 == ESP_OK) {
    Serial.println("Sent message 3 with success");
  }
  else {
    Serial.println("Error sending the data");
  }
}

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  if (status ==0){
    success = "Delivery Success :)";
  }
  else{
    success = "Delivery Fail :(";
  }
}

char convertCharToHex(char ch)
{
  char returnType;
  switch(ch)
  {
    case '0':
    returnType = 0;
    break;
    case  '1' :
    returnType = 1;
    break;
    case  '2':
    returnType = 2;
    break;
    case  '3':
    returnType = 3;
    break;
    case  '4' :
    returnType = 4;
    break;
    case  '5':
    returnType = 5;
    break;
    case  '6':
    returnType = 6;
    break;
    case  '7':
    returnType = 7;
    break;
    case  '8':
    returnType = 8;
    break;
    case  '9':
    returnType = 9;
    break;
    case  'a':
    returnType = 10;
    break;
    case  'b':
    returnType = 11;
    break;
    case  'c':
    returnType = 12;
    break;
    case  'd':
    returnType = 13;
    break;
    case  'e':
    returnType = 14;
    break;
    case  'f' :
    returnType = 15;
    break;
    default:
    returnType = 0;
    break;
  }
  return returnType;
}

void prtnib(int n) {
  if (n>=8) {Serial.print("1"); n-=8;} else {Serial.print("0");}
  if (n>=4) {Serial.print("1"); n-=4;} else {Serial.print("0");}
  if (n>=2) {Serial.print("1"); n-=2;} else {Serial.print("0");}
  if (n>=1) {Serial.print("1");} else {Serial.print("0");}
}

void updateBatteryCapacity(float current) {
    unsigned long currentTime = millis();
    
    // On first run, initialize the timer
    if (lastUpdateTime == 0) {
        lastUpdateTime = currentTime;
        return;
    }

    // Time delta in seconds
    float deltaTimeSec = (currentTime - lastUpdateTime) / 1000.0;

    // Calculate used charge in Ah (Coulombs = A * s, Ah = A * s / 3600)
    float usedAh = (current * deltaTimeSec) / 3600.0;

    // Subtract from battery capacity
    batteryCapacity -= usedAh;

    // Clamp to 0
    if (batteryCapacity < 0) batteryCapacity = 0;

    // Update timestamp
    lastUpdateTime = currentTime;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    /*** Only a reference to the advertised device is passed now
      void onResult(BLEAdvertisedDevice advertisedDevice) { **/
    void onResult(BLEAdvertisedDevice *advertisedDevice) {
    String addr = advertisedDevice->getAddress().toString().c_str();
    String mfdata = advertisedDevice->getManufacturerData().c_str();
    uint8_t* payload = advertisedDevice->getPayload();
    uint8_t payloadlen = advertisedDevice->getPayloadLength();
    char *payloadhex = utils.buildHexData(nullptr, payload, payloadlen);
    if (addr.startsWith("ac:15:85")) {  // my sensors MAC start with ac:15:85
      Serial.print("Payload: "); Serial.print(payloadhex);

      // convert hex-payload to array
      char *pPL = utils.buildHexData(nullptr, (uint8_t*)advertisedDevice->getPayload(), advertisedDevice->getPayloadLength());
      String sPL = (String) pPL;
      byte plByte[16];
      byte plNib[31];
      sPL.getBytes(plNib,31);
      for (int i=0; i<30; i=i+2) {
        plByte[i/2] = convertCharToHex(plNib[i])*16 + convertCharToHex(plNib[i+1]);
      }
        
      Serial.print("  ADDR: "); Serial.print(addr.substring(12));
      char *pHex = utils.buildHexData(nullptr, (uint8_t*)advertisedDevice->getManufacturerData().data(), advertisedDevice->getManufacturerData().length());
      Serial.print("  MFG DATA: "); Serial.print(pHex);
      String sHex = (String) pHex;
      byte nib[16];
      sHex.getBytes(nib,15);
      for (int i=0; i<14; i++) {
        nib[i] = convertCharToHex(nib[i]);
      }
      float Press = (float)((nib[7]*256+nib[8]*16+nib[9])-145)/10.0;
      String sPress = (String)Press;
      Serial.print("  p: "); Serial.print(sPress.substring(0,sPress.length()-1));
      int Temp = nib[4]*16+nib[5];
      Serial.print("  T: "); Serial.print(Temp);
      float Batt = (float)(nib[2]*16+nib[3])/10.0;
      String sBatt = (String)Batt;
      Serial.print("  b: "); Serial.print(sBatt.substring(0,sBatt.length()-1));
      Serial.print("  BIN: ");
      for (int i=0; i<2; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=2; i<4; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=4; i<6; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=6; i<10; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=10; i<14; i++) {prtnib(nib[i]);}

      bool nl = false;
      if (nib[0]==8) {Serial.print("   ALARM"); nl=true;}
      if (nib[0]==4) {Serial.print("   ROTAT"); nl=true;}
      if (nib[0]==2) {Serial.print("   STILL"); nl=true;}
      if (nib[0]==1) {Serial.print("   BGROT"); nl=true;}
      if (nib[1]==8) {Serial.println("   DECR2"); nl=false;}
      if (nib[1]==4) {Serial.println("   RISIN"); nl=false;}
      if (nib[1]==2) {Serial.println("   DECR1"); nl=false;}
      if ((nib[0]*16+nib[1])==0xff) {Serial.println("   LBATT");}
      if (nl) {Serial.println();}
    }
    else {
      // See if we have manufacturer data and then look to see if it's coming from a Victron device.
      if (advertisedDevice->haveManufacturerData() == true) {

        uint8_t manCharBuf[manDataSizeMax+1];

        #ifdef USE_String
          String manData = advertisedDevice.getManufacturerData().c_str();  ;      // lib code returns String.
        #else
          std::string manData = advertisedDevice->getManufacturerData(); // lib code returns std::string
        #endif
        int manDataSize=manData.length(); // This does not count the null at the end.

        // Copy the data from the String to a byte array. Must have the +1 so we
        // don't lose the last character to the null terminator.
        #ifdef USE_String
          manData.toCharArray((char *)manCharBuf,manDataSize+1);
        #else
          manData.copy((char *)manCharBuf, manDataSize+1);
        #endif

        // Now let's setup a pointer to a struct to get to the data more cleanly.
        victronManufacturerData * vicData=(victronManufacturerData *)manCharBuf;

        // ignore this packet if the Vendor ID isn't Victron.
        if (vicData->vendorID!=0x02e1) {
          return;
        } 

        // ignore this packet if it isn't type 0x01 (Solar Charger).
        if (vicData->victronRecordType != 0x09) {
          Serial.printf("Packet victronRecordType was 0x%x doesn't match 0x09\n",
               vicData->victronRecordType);
          return;
        }

        // Not all packets contain a device name, so if we get one we'll save it and use it from now on.
        if (advertisedDevice->haveName()) {
          // This works the same whether getName() returns String or std::string.
          strcpy(savedDeviceName,advertisedDevice->getName().c_str());
        }
        
        if (vicData->encryptKeyMatch != key[0]) {
          Serial.printf("Packet encryption key byte 0x%2.2x doesn't match configured key[0] byte 0x%2.2x\n",
              vicData->encryptKeyMatch, key[0]);
          return;
        }

        uint8_t inputData[16];
        uint8_t outputData[16]={0};  // i don't really need to initialize the output.

        // The number of encrypted bytes is given by the number of bytes in the manufacturer
        // data as a whole minus the number of bytes (10) in the header part of the data.
        int encrDataSize=manDataSize-10;
        for (int i=0; i<encrDataSize; i++) {
          inputData[i]=vicData->victronEncryptedData[i];   // copy for our decrypt below while I figure this out.
        }

        esp_aes_context ctx;
        esp_aes_init(&ctx);

        auto status = esp_aes_setkey(&ctx, key, keyBits);
        if (status != 0) {
          Serial.printf("  Error during esp_aes_setkey operation (%i).\n",status);
          esp_aes_free(&ctx);
          return;
        }
        
        // construct the 16-byte nonce counter array by piecing it together byte-by-byte.
        uint8_t data_counter_lsb=(vicData->nonceDataCounter) & 0xff;
        uint8_t data_counter_msb=((vicData->nonceDataCounter) >> 8) & 0xff;
        u_int8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};
        
        u_int8_t stream_block[16] = {0};

        size_t nonce_offset=0;
        status = esp_aes_crypt_ctr(&ctx, encrDataSize, &nonce_offset, nonce_counter, stream_block, inputData, outputData);
        if (status != 0) {
          Serial.printf("Error during esp_aes_crypt_ctr operation (%i).",status);
          esp_aes_free(&ctx);
          return;
        }
        esp_aes_free(&ctx);

        // Now do our same struct magic so we can get to the data more easily.
        victronPanelData * victronData = (victronPanelData *) outputData;

        // Getting to these elements is easier using the struct instead of
        // hacking around with outputData[x] references.
        uint8_t deviceState=victronData->deviceState;
        uint8_t outputState=victronData->outputState;
        uint8_t errorCode=victronData->errorCode;
        uint16_t alarmReason=victronData->alarmReason;
        uint16_t warningReason=victronData->warningReason;
        float inputVoltage=float(victronData->inputVoltage)*0.01;
        float outputVoltage=float(victronData->outputVoltage)*0.01;
        uint32_t offReason=victronData->offReason;

        localVoltage0Struct.rearAuxBatt1V = outputVoltage; // ToDo: unhack me

        sendMessage(); // sends the data out via ESP Now

        Serial.printf("%s, Battery: %.2f Volts, Load: %4.2f Volts, Alarm Reason: %d, Device State: %d, Error Code: %d, Warning Reason: %d, Off Reason: %d\n",
          savedDeviceName,
          inputVoltage, outputVoltage,
          alarmReason, deviceState,
          errorCode, warningReason,
          offReason
        );
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  
  #ifdef USE_ADC
    Wire.begin(6,10);
    ina226.init();
    ina226.waitUntilConversionCompleted(); //if you comment this line the first data might be zero
  #endif
    
  WiFi.mode(WIFI_MODE_STA);

  Serial.println(WiFi.macAddress());

  /* Set Number of measurements for shunt and bus voltage which shall be averaged
  * Mode *     * Number of samples *
  AVERAGE_1            1 (default)
  AVERAGE_4            4
  AVERAGE_16          16
  AVERAGE_64          64
  AVERAGE_128        128
  AVERAGE_256        256
  AVERAGE_512        512
  AVERAGE_1024      1024
  */
  ina226.setAverage(AVERAGE_16); // choose mode and uncomment for change of default

  /* Set conversion time in microseconds
     One set of shunt and bus voltage conversion will take: 
     number of samples to be averaged x conversion time x 2
     
     * Mode *         * conversion time *
     CONV_TIME_140          140 µs
     CONV_TIME_204          204 µs
     CONV_TIME_332          332 µs
     CONV_TIME_588          588 µs
     CONV_TIME_1100         1.1 ms (default)
     CONV_TIME_2116       2.116 ms
     CONV_TIME_4156       4.156 ms
     CONV_TIME_8244       8.244 ms  
  */
  ina226.setConversionTime(CONV_TIME_8244); //choose conversion time and uncomment for change of default
  
  /* Set measure mode
  POWER_DOWN - INA226 switched off
  TRIGGERED  - measurement on demand
  CONTINUOUS  - continuous measurements (default)
  */
  //ina226.setMeasureMode(TRIGGERED); // choose mode and uncomment for change of default
  
  /* If the current values delivered by the INA226 differ by a constant factor
     from values obtained with calibrated equipment you can define a correction factor.
     Correction factor = current delivered from calibrated equipment / current delivered by INA226
  */
  // ina226.setCorrectionFactor(0.95);
  ina226.setResistorRange(ohms,100.0); // choose resistor 5 mOhm and gain range up to 10 A
  
  Serial.println("INA226 Current Sensor Example Sketch - Continuous");

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  
  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Scanning...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value

 Serial.println("Setup done");

}

void loop() {
  #ifdef USE_ADC
    ina226.readAndClearFlags();
    shuntVoltage_mV = ina226.getShuntVoltage_mV();
    busVoltage_V = ina226.getBusVoltage_V();
    current_mA = ina226.getCurrent_mA();
    power_mW = ina226.getBusPower();
    loadVoltage_V  = busVoltage_V + (shuntVoltage_mV/1000);

    localVoltage0Struct.rearAuxBatt1V = ina226.getBusVoltage_V();
    localVoltage0Struct.rearAuxBatt1I = ina226.getCurrent_A();
    
    Serial.print("Battery Voltage [V]: "); Serial.println(busVoltage_V);
    Serial.print("Load Voltage [V]: "); Serial.println(loadVoltage_V);
    Serial.print("Current [A]: "); Serial.println(current_mA/1000);
    Serial.print("Bus Power [W]: "); Serial.println(power_mW/1000);

    float measuredCurrent = current_mA/1000; // in Amps
    updateBatteryCapacity(measuredCurrent);

    Serial.print("Remaining Capacity [Ah]: ");
    Serial.println(batteryCapacity, 4);

    if(!ina226.overflow){
      Serial.println("Values OK - no overflow");
    }
    else{
      Serial.println("Overflow! Choose higher current range");
    }
  #endif
  
  Serial.println();

  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
  
  delay(10000);
}