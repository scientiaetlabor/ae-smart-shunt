#include <vector>
#include <Arduino.h>
#include "shared_defs.h"
#include "ina226_adc.h"
#include "ble_handler.h"
#include "espnow_handler.h"
#include "passwords.h"
#include <esp_now.h>
#include <esp_err.h>
#define USE_ADC // if not defined, use victron BLE

float batteryCapacity = 100.0f; // Default rated battery capacity in Ah (used for SOC calc)

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct_message_ae_smart_shunt_1 ae_smart_shunt_struct;
INA226_ADC ina226_adc(I2C_ADDRESS, 0.0007191f, 100.00f); // shunt resistor, rated battery capacity
ESPNowHandler espNowHandler(broadcastAddress);           // ESP-NOW handler for sending data

// helper: read a trimmed line from Serial (blocks until newline)
static String SerialReadLineBlocking()
{
  String s;
  while (true)
  {
    while (Serial.available() == 0)
      delay(5);
    s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      return s;
    // allow empty Enter to be treated as empty string
    return s;
  }
}

// Helper: wait for enter or 'x' while optionally streaming debug raw vs calibrated values.
// Returns the user-entered line (possibly empty string if they just pressed Enter), or "x" if canceled.
static String waitForEnterOrXWithDebug(INA226_ADC &ina, bool debugMode)
{
  // Flush any existing chars
  while (Serial.available())
    Serial.read();

  unsigned long lastPrint = 0;
  const unsigned long printInterval = 300; // ms

  while (true)
  {
    if (Serial.available())
    {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line.length() == 0)
      {
        // Enter pressed (empty line) - record step
        return String("");
      }
      else
      {
        // Could be 'x' or other input
        if (line.equalsIgnoreCase("x"))
          return String("x");
        // treat any non-empty as confirmation as well
        return line;
      }
    }

    // Periodically print debug readings if enabled
    unsigned long now = millis();
    if (debugMode && (now - lastPrint >= printInterval))
    {
      ina.readSensors();
      float raw = ina.getRawCurrent_mA();
      float cal = ina.getCurrent_mA();
      Serial.printf("RAW: %.3f mA\tCAL: %.3f mA\n", raw, cal);
      lastPrint = now;
    }
    delay(20);
  }
}

// Run calibration via Serial UI
void runCalibrationMenu(INA226_ADC &ina)
{
  Serial.println(F("\n--- Calibration Menu ---"));
  Serial.println(F("Choose installed shunt rating (50..500 A in 50A steps) or 'x' to cancel:"));
  Serial.print(F("> "));

  String sel = SerialReadLineBlocking();
  if (sel.equalsIgnoreCase("x"))
  {
    Serial.println(F("Calibration canceled."));
    return;
  }

  int shuntA = sel.toInt();
  if (shuntA < 50 || shuntA > 500 || (shuntA % 50) != 0)
  {
    Serial.println(F("Invalid shunt rating. Aborting calibration."));
    return;
  }

  // Load existing calibration for that shunt (if any) so user can see values
  bool had = ina.loadCalibration(shuntA);
  float g0, o0;
  ina.getCalibration(g0, o0);
  if (had)
  {
    Serial.printf("Loaded stored calibration for %dA: gain=%.9f offset_mA=%.3f\n", shuntA, g0, o0);
  }
  else
  {
    Serial.printf("No stored calibration for %dA. Using defaults gain=%.9f offset_mA=%.3f\n", shuntA, g0, o0);
  }

  // Ask if user wants live debug streaming while waiting for each step
  Serial.println(F("Enable live debug stream (raw vs calibrated) while waiting to record each step? (y/N)"));
  Serial.print(F("> "));
  String dbgAns = SerialReadLineBlocking();
  bool debugMode = dbgAns.equalsIgnoreCase("y") || dbgAns.equalsIgnoreCase("yes");

  // build measurement percentages
  std::vector<float> perc;
  perc.push_back(0.01f);
  perc.push_back(0.05f);
  perc.push_back(0.10f);
  for (int p = 20; p <= 100; p += 10)
    perc.push_back(p / 100.0f);

  std::vector<float> measured_mA;
  std::vector<float> true_mA;

  for (size_t i = 0; i < perc.size(); ++i)
  {
    float p = perc[i];
    float trueA = shuntA * p;
    float true_milli = trueA * 1000.0f;
    Serial.printf("\nStep %u of %u: Target = %.3f A (%.1f%% of %dA).\nSet test jig to the target current, then press Enter to record. Enter 'x' to cancel and accept measured so far.\n",
                  (unsigned)(i + 1), (unsigned)perc.size(), trueA, p * 100.0f, shuntA);
    Serial.print("> ");

    // Wait for Enter or 'x', printing debug stream if enabled.
    String line = waitForEnterOrXWithDebug(ina, debugMode);
    if (line.equalsIgnoreCase("x"))
    {
      Serial.println("User canceled early; accepting tests recorded so far.");
      break;
    }

    // Take a short average of raw measurements
    const int samples = 8;
    float sumRaw = 0.0f;
    for (int s = 0; s < samples; ++s)
    {
      ina.readSensors();
      float raw = ina.getRawCurrent_mA();
      sumRaw += raw;
      delay(120);
    }
    float avgRaw = sumRaw / (float)samples;

    Serial.printf("Recorded avg raw reading: %.3f mA  (expected true: %.3f mA)\n", avgRaw, true_milli);

    measured_mA.push_back(avgRaw);
    true_mA.push_back(true_milli);
  }

  size_t N = measured_mA.size();
  if (N == 0)
  {
    Serial.println("No measurements taken; leaving calibration unchanged.");
    return;
  }

  // Fit linear model y = a*x + b where y=true_mA, x=measured_mA
  float gain = 1.0f;
  float offset_mA = 0.0f;

  if (N == 1)
  {
    // Single point: set gain to match magnitude, offset 0
    if (measured_mA[0] != 0.0f)
    {
      gain = true_mA[0] / measured_mA[0];
      offset_mA = 0.0f;
    }
    else
    {
      gain = 1.0f;
      offset_mA = true_mA[0]; // degenerate: raw zero, put offset
    }
  }
  else
  {
    // Least squares
    double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    for (size_t i = 0; i < N; ++i)
    {
      double x = measured_mA[i];
      double y = true_mA[i];
      sum_x += x;
      sum_y += y;
      sum_xx += x * x;
      sum_xy += x * y;
    }
    double denom = (N * sum_xx - sum_x * sum_x);
    if (fabs(denom) < 1e-12)
    {
      // fallback
      float mean_x = sum_x / (double)N;
      float mean_y = sum_y / (double)N;
      gain = (mean_x != 0.0) ? (mean_y / mean_x) : 1.0f;
      offset_mA = 0.0f;
    }
    else
    {
      gain = (float)((N * sum_xy - sum_x * sum_y) / denom);
      offset_mA = (float)((sum_y - gain * sum_x) / (double)N);
    }
  }

  // Save calibration
  ina.saveCalibration(shuntA, gain, offset_mA);

  Serial.println("\nCalibration complete.");
  Serial.printf("Final calibration for %dA: gain = %.9f, offset_mA = %.3f\n", shuntA, gain, offset_mA);
  Serial.println("These values are persisted and will be applied to subsequent current readings.");
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup()
{
  Serial.begin(115200);
  delay(100); // let Serial start

  ina226_adc.begin(6, 10);

  // Print calibration summary on boot
  Serial.println("Calibration summary:");
  for (int sh = 50; sh <= 500; sh += 50) {
    float g, o;
    if (ina226_adc.getStoredCalibrationForShunt(sh, g, o)) {
      Serial.printf("  %dA: gain=%.9f offset_mA=%.3f\n", sh, g, o);
    } else {
      Serial.printf("  %dA: No saved calibration (using defaults)\n", sh);
    }
  }
  // Also print currently applied calibration (if any)
  float curG, curO;
  ina226_adc.getCalibration(curG, curO);
  Serial.printf("Active calibration: gain=%.9f offset_mA=%.3f\n", curG, curO);

  // Initialize ESP-NOW
  if (!espNowHandler.begin())
  {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register callback for send status
  espNowHandler.registerSendCallback(onDataSent);
  // Ensure broadcast peer exists (some SDKs require an explicit broadcast peer)
  if (!espNowHandler.addPeer())
  {
    Serial.println("Warning: failed to add broadcast peer; esp_now_send may return ESP_ERR_ESPNOW_NOT_FOUND on some platforms");
  }
  else
  {
    Serial.println("Broadcast peer added");
  }

#ifndef USE_ADC
  // Code to use victron BLE
  bleHandler.startScan(scanTime);
#endif

  Serial.println("Setup done");
}

void loop()
{
#ifdef USE_ADC
  ina226_adc.readSensors();

  // Check serial for calibration command
  if (Serial.available())
  {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.equalsIgnoreCase("c"))
    {
      runCalibrationMenu(ina226_adc);
    }
    // else ignore — keep running
  }

  // Populate struct fields
  ae_smart_shunt_struct.messageID = 11;
  ae_smart_shunt_struct.dataChanged = true;

  ae_smart_shunt_struct.batteryVoltage = ina226_adc.getBusVoltage_V();
  ae_smart_shunt_struct.batteryCurrent = ina226_adc.getCurrent_mA() / 1000.0f;
  ae_smart_shunt_struct.batteryPower = ina226_adc.getPower_mW() / 1000.0f;

  ae_smart_shunt_struct.batteryState = 0; // 0 = Normal, 1 = Warning, 2 = Critical

  // Update remaining capacity in the INA226 helper (expects current in A)
  ina226_adc.updateBatteryCapacity(ina226_adc.getCurrent_mA() / 1000.0f);

  // Get remaining Ah from INA helper
  float remainingAh = ina226_adc.getBatteryCapacity();
  ae_smart_shunt_struct.batteryCapacity = remainingAh; // remaining capacity in Ah
  // batteryCapacity global holds the rated capacity in Ah for SOC calculation
  if (batteryCapacity > 0.0f)
  {
    ae_smart_shunt_struct.batterySOC = remainingAh / batteryCapacity; // fraction 0..1
  }
  else
  {
    ae_smart_shunt_struct.batterySOC = 0.0f;
  }

  if (ina226_adc.isOverflow())
  {
    Serial.println("Overflow! Choose higher current range");
    ae_smart_shunt_struct.batteryState = 3; // overflow indicator
  }

  // Calculate and print run-flat time with warning threshold
  bool warning = false;
  float currentA = ina226_adc.getCurrent_mA() / 1000.0f; // convert mA to A
  float warningThresholdHours = 10.0f;

  String avgRunFlatTimeStr = ina226_adc.getAveragedRunFlatTime(currentA, warningThresholdHours, warning);

  strncpy(ae_smart_shunt_struct.runFlatTime, avgRunFlatTimeStr.c_str(), sizeof(ae_smart_shunt_struct.runFlatTime));
  ae_smart_shunt_struct.runFlatTime[sizeof(ae_smart_shunt_struct.runFlatTime) - 1] = '\0'; // ensure null termination

#else
  // Code to use victron BLE
  bleHandler.startScan(scanTime);

  // Provide safe defaults so struct is still valid
  ae_smart_shunt_struct.messageID = 0;
  ae_smart_shunt_struct.dataChanged = false;
  ae_smart_shunt_struct.batteryVoltage = 0.0f;
  ae_smart_shunt_struct.batteryCurrent = 0.0f;
  ae_smart_shunt_struct.batteryPower = 0.0f;
  ae_smart_shunt_struct.batteryCapacity = 0.0f;
  ae_smart_shunt_struct.batterySOC = 0.0f;
  ae_smart_shunt_struct.batteryState = 0;
  strncpy(ae_smart_shunt_struct.runFlatTime, "N/A", sizeof(ae_smart_shunt_struct.runFlatTime));
#endif

  // Send the data via ESP-NOW
  espNowHandler.setAeSmartShuntStruct(ae_smart_shunt_struct);
  espNowHandler.sendMessageAeSmartShunt();

#ifdef USE_ADC
  Serial.print("Average estimated run-flat time: ");
  Serial.println(String(ae_smart_shunt_struct.runFlatTime));
  if (ina226_adc.isOverflow())
  {
    Serial.println("Warning: Overflow condition!");
  }
#endif

  Serial.println();
  delay(10000);
}