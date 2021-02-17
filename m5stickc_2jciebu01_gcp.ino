#include <M5StickC.h>
#include <WiFi.h>
#include <Preferences.h>
#include "time.h"
#include "BLEDevice.h"
//#include "BLEScan.h"

Preferences preferences;
static char wifi_ssid[33];
static char wifi_key[65];
static char omronSensorAddress[18];

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

static float temp;
static float hum;
static uint16_t light;
static float pressure;

TaskHandle_t xhandle_blescan = NULL;

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
	/**
	 * Called for each advertising BLE server.
	 */
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		Serial.print("BLE Advertised Device found: ");
		Serial.println(advertisedDevice.toString().c_str());
		String deviceAddress = advertisedDevice.getAddress().toString().c_str();

		// We have found a device, let us now see if it contains the service we are looking for.
		//if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
		if (deviceAddress.equalsIgnoreCase(omronSensorAddress))
		{
			uint8_t *payload = advertisedDevice.getPayload();
			size_t paylen = advertisedDevice.getPayloadLength();

			Serial.print("Payload: ");
			for (uint32_t i = 0; i < paylen; i++)
			{
				Serial.printf("%02X", payload[i]);
			}
			Serial.println("");
			temp = (float)((payload[10] << 8) | payload[9]) / 100.0;
			hum = (float)((payload[12] << 8) | payload[11]) / 100.0;
			light = (uint16_t)((payload[14] << 8) | payload[13]);
			pressure = (float)((payload[18] << 24) | (payload[17] << 16) | (payload[16] << 8) | payload[15]) / 1000.0;

			BLEDevice::getScan()->stop();
			//doConnect = true;
			doScan = true;
		} // Found our server
	}	  // onResult
};		  // MyAdvertisedDeviceCallbacks

void bleScanTask(void *arg)
{
	while (true)
	{
		BLEDevice::getScan()->start(5, false);
		delay(5000);
	}
}

void setup()
{
	M5.begin();
	Serial.begin(115200);
	Serial.println("Starting Arduino BLE Client application...");
	M5.Axp.ScreenBreath(8);
	M5.Lcd.setRotation(3);
	M5.Lcd.setCursor(0, 0, 2);

	preferences.begin("Network", true);
	preferences.getString("ssid", wifi_ssid, sizeof(wifi_ssid));
	preferences.getString("key", wifi_key, sizeof(wifi_key));
	preferences.getString("senaddr", omronSensorAddress, sizeof(omronSensorAddress));
	preferences.end();

	M5.Lcd.printf("Connecting to %s ", wifi_ssid);
	WiFi.begin(wifi_ssid, wifi_key);
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		M5.Lcd.print(".");
	}
	M5.Lcd.println(" CONNECTED");

	configTime(9 * 3600, 0, "ntp.nict.jp"); // Set ntp time to local

	BLEDevice::init("");

	// Retrieve a Scanner and set the callback we want to use to be informed when we
	// have detected a new device.  Specify that we want active scanning and start the
	// scan to run for 5 seconds.
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setInterval(100);
	pBLEScan->setWindow(90);
	pBLEScan->setActiveScan(true);

	xTaskCreate(bleScanTask, "BLEScanTask", 1024 * 2, (void *)0, 5, &xhandle_blescan);
	M5.Lcd.fillScreen(BLACK);
} // End of setup.

// This is the Arduino main loop function.
void loop()
{
	static uint8_t displayoffcount = 50;

	M5.update();
	if (M5.BtnA.wasReleased())
	{
		Serial.print("BtnA: ");
		Serial.println(displayoffcount);
		if (displayoffcount == 0)
		{
			// wake up display and turn on back light
			M5.Lcd.writecommand(ST7735_SLPOUT);
			delay(150);
			M5.Axp.SetLDO2(true);
		}
		displayoffcount = 50;
	}

	if (0 < displayoffcount)
	{
		struct tm timeInfo;
		getLocalTime(&timeInfo);

		char now[20];
		sprintf(now, "%04d/%02d/%02d %02d:%02d:%02d",
				timeInfo.tm_year + 1900,
				timeInfo.tm_mon + 1,
				timeInfo.tm_mday,
				timeInfo.tm_hour,
				timeInfo.tm_min,
				timeInfo.tm_sec);

		M5.Lcd.setCursor(0, 0, 2);
		M5.Lcd.println(now);
		M5.Lcd.printf("temp: %g ", temp);
		M5.Lcd.println("");
		M5.Lcd.printf("hum: %g ", hum);
		M5.Lcd.println("");
		M5.Lcd.printf("lx: %d ", light);
		M5.Lcd.println("");
		M5.Lcd.printf("pressure: %g ", pressure);
	}

	if (displayoffcount == 1)
	{
		// sleep display and turn off backlight
		M5.Axp.SetLDO2(false);
		M5.Lcd.writecommand(ST7735_SLPIN);
		displayoffcount = 0;
	}
	else if (0 < displayoffcount)
	{
		displayoffcount--;
	}

	delay(100); // Delay a second between loops.
} // End of loop
