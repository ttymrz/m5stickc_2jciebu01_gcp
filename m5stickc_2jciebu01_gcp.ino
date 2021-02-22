#include <M5StickC.h>
#include <WiFi.h>
#include <Preferences.h>
#include "time.h"
#include "BLEDevice.h"
#include <Client.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <MQTT.h>

#include <CloudIoTCore.h>
#include <CloudIoTCoreMqtt.h>
#include "ciotc_config.h"

Preferences preferences;
static char wifi_ssid[33];
static char wifi_key[65];
static char omronSensorAddress[18];

static boolean bleDetect;

static float temp;
static float hum;
static uint16_t light;
static float pressure;
static uint16_t etvoc;
static uint16_t eco2;

TaskHandle_t xhandle_blescan = NULL;
TaskHandle_t xhandle_ledblink = NULL;
TaskHandle_t xhandle_cloudiot = NULL;

void messageReceived(String &topic, String &payload)
{
	Serial.println("incoming: " + topic + " - " + payload);
}

// Initialize MQTT for this board
Client *netClient;
CloudIoTCoreDevice *device;
CloudIoTCoreMqtt *mqtt;
MQTTClient *mqttClient;
unsigned long iat = 0;
String jwt;

///////////////////////////////
// Helpers specific to this board
///////////////////////////////
String getDefaultSensor()
{
	return "Wifi: " + String(WiFi.RSSI()) + "db";
}

String getJwt()
{
	iat = time(nullptr);
	Serial.println("Refreshing JWT");
	jwt = device->createJWT(iat, jwt_exp_secs);
	return jwt;
}

void setupCloudIoT()
{
	device = new CloudIoTCoreDevice(
		project_id, location, registry_id, device_id,
		private_key_str);

	netClient = new WiFiClientSecure();
	mqttClient = new MQTTClient(1024);
	mqttClient->setOptions(180*2, true, 1000*2); // keepAlive, cleanSession, timeout
	mqtt = new CloudIoTCoreMqtt(mqttClient, netClient, device);
	mqtt->setUseLts(true);
	mqtt->startMQTT();
	mqtt->mqttConnect();
}

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
		// Serial.print("BLE Advertised Device found: ");
		// Serial.println(advertisedDevice.toString().c_str());
		String deviceAddress = advertisedDevice.getAddress().toString().c_str();

		// We have found a device, let us now see if it contains the address we are looking for.
		if (deviceAddress.equalsIgnoreCase(omronSensorAddress))
		{
			uint8_t *payload = advertisedDevice.getPayload();
			size_t paylen = advertisedDevice.getPayloadLength();

			if (31 <= paylen)
			{
				temp = (float)((payload[10] << 8) | payload[9]) / 100.0;
				hum = (float)((payload[12] << 8) | payload[11]) / 100.0;
				light = (uint16_t)((payload[14] << 8) | payload[13]);
				pressure = (float)((payload[18] << 24) | (payload[17] << 16) | (payload[16] << 8) | payload[15]) / 1000.0;
				etvoc = (uint16_t)((payload[22] << 8) | payload[21]);
				eco2 = (uint16_t)((payload[24] << 8) | payload[23]);
			}

			BLEDevice::getScan()->stop();
			bleDetect = true;
		}
	}
};

void ledBlinkingTask(void *arg)
{
	pinMode(M5_LED, OUTPUT);
	while (true)
	{
		digitalWrite(M5_LED, LOW);
		delay(1);
		digitalWrite(M5_LED, HIGH);
		delay(4999);
	}
}

void bleScanTask(void *arg)
{
	while (true)
	{
		bleDetect = false;
		BLEDevice::getScan()->start(5, false);
		delay(5000);
	}
}

void cloudIoTTask(void *arg)
{
	while (true)
	{
		delay(5000);
		// mqtt->loop();
		// delay(10);
		if (WiFi.status() != WL_CONNECTED)
		{
			Serial.println("WiFi reconnect");
			WiFi.begin(wifi_ssid, wifi_key);
			delay(500);
		}
		if (!mqttClient->connected())
		{
			Serial.println("cloud IoT reconnect");
			mqtt->mqttConnect();
		}
		Serial.print("publish: ");
		Serial.println(getDefaultSensor());
		mqtt->publishTelemetry(getDefaultSensor());
	}
}

void setup()
{
	uint8_t mac[6];

	M5.begin();
	Serial.begin(115200);
	Serial.println("Starting M5StickC 2JCE-BU1 GCP App...");
	M5.Axp.ScreenBreath(8);
	M5.Lcd.setRotation(3);
	M5.Lcd.setCursor(0, 0, 2);

	preferences.begin("Network", true);
	preferences.getString("ssid", wifi_ssid, sizeof(wifi_ssid));
	preferences.getString("key", wifi_key, sizeof(wifi_key));
	preferences.getString("senaddr", omronSensorAddress, sizeof(omronSensorAddress));
	preferences.getString("privatekey", private_key_str, sizeof(private_key_str));
	preferences.end();

	M5.Lcd.printf("Connecting to %s ", wifi_ssid);
	WiFi.begin(wifi_ssid, wifi_key);
	WiFi.macAddress(mac);
	Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
				  mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
	Serial.println("");
	sprintf(device_id, "m%02X%02X%02X%02X%02X%02X",
			mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		M5.Lcd.print(".");
	}
	M5.Lcd.println(" CONNECTED");

	configTime(9 * 3600, 0, "ntp.nict.jp"); // Set ntp time to local
	delay(5000);
	M5.Lcd.println("Setup CloudIoT");
	setupCloudIoT();
	delay(5000);

	M5.Lcd.println("Start BLEScan");
	BLEDevice::init("");
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setInterval(100);
	pBLEScan->setWindow(90);
	pBLEScan->setActiveScan(true);

	xTaskCreate(ledBlinkingTask, "ledBlinkingTask", configMINIMAL_STACK_SIZE, NULL, 3, &xhandle_ledblink);
	xTaskCreate(bleScanTask, "BLEScanTask", 2048, NULL, 3, &xhandle_blescan);
	xTaskCreate(cloudIoTTask, "cloudIoTTask", 4096, NULL, 3, &xhandle_cloudiot);
	M5.Lcd.fillScreen(BLACK);
} // End of setup.

// This is the Arduino main loop function.
void loop()
{
	static uint8_t displayoffcount = 100;

	M5.update();
	if (M5.BtnA.wasReleased())
	{
		if (displayoffcount == 0)
		{
			// wake up display and turn on back light
			M5.Lcd.writecommand(ST7735_SLPOUT);
			delay(150);
			M5.Axp.SetLDO2(true);
		}
		displayoffcount = 100;
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
		M5.Lcd.printf("temp: %2.1f ", temp);
		M5.Lcd.printf("hum: %2.1f", hum);
		M5.Lcd.println("");
		M5.Lcd.printf("lx: %4d    ", light);
		M5.Lcd.printf("press: %4.1f", pressure);
		M5.Lcd.println("");
		M5.Lcd.printf("eTVOC: %4d ", etvoc);
		M5.Lcd.printf("eCO2: %4d", eco2);
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
