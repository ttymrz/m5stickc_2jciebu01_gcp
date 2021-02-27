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

static char wifi_ssid[33];
static char wifi_key[65];
static char omronSensorAddress[18];
// const auto pubMinutes = {0, 5, 10, 15, 25, 30, 35, 40, 45, 50, 55};
const auto pubMinutes = {0, 15, 30, 45};

static boolean bleDetect = true;
static boolean networkerr = false;

static time_t timestamp;
static float temp;
static float hum;
static uint16_t light;
static float pressure;
static uint16_t etvoc;
static uint16_t eco2;

SemaphoreHandle_t xMutexData = NULL;
TaskHandle_t xhandle_blescan = NULL;
TaskHandle_t xhandle_ledblink = NULL;
TaskHandle_t xhandle_cloudiot = NULL;

const int wdtTimeout = 120000; //time in ms to trigger the watchdog
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule()
{
	ets_printf("reboot\n");
	esp_restart();
}

// The MQTT callback function for commands and configuration updates
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
	mqttClient = new MQTTClient(512);
	mqttClient->setOptions(30, true, 10000); // keepAlive 30s, cleanSession, timeout 10s
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
				const TickType_t xTicksToWait = 5000UL;
				BaseType_t xStatus = xSemaphoreTake(xMutexData, xTicksToWait);
				if (xStatus == pdTRUE)
				{
					timestamp = time(NULL);
					temp = (float)((payload[10] << 8) | payload[9]) / 100.0;
					hum = (float)((payload[12] << 8) | payload[11]) / 100.0;
					light = (uint16_t)((payload[14] << 8) | payload[13]);
					pressure = (float)((payload[18] << 24) | (payload[17] << 16) | (payload[16] << 8) | payload[15]) / 1000.0;
					etvoc = (uint16_t)((payload[22] << 8) | payload[21]);
					eco2 = (uint16_t)((payload[24] << 8) | payload[23]);
				}
				xSemaphoreGive(xMutexData);
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
		if (networkerr)
		{
			for (int i = 0; i < 2; i++)
			{
				digitalWrite(M5_LED, LOW);
				delay(1);
				digitalWrite(M5_LED, HIGH);
				delay(249);
			}
			delay(3500);
		}
		else
		{
			digitalWrite(M5_LED, LOW);
			delay(1);
			digitalWrite(M5_LED, HIGH);
			delay(4999);
		}
	}
}

void bleScanTask(void *arg)
{
	while (true)
	{
		bleDetect = false;
		BLEDevice::getScan()->start(5, false);
		delay(30000);
	}
}

void cloudIoTTask(void *arg)
{
	BaseType_t xStatus;
	const TickType_t xTicksToWait = 5000UL;
	static auto prevPubMin = 99;

	while (true)
	{
		struct tm timeInfo;
		bool doPublish = false;

		delay(30000);
		timerWrite(timer, 0); //reset timer (feed watchdog)
		networkerr = false;

		// mqtt->loop();
		// delay(10);
		while (WiFi.status() != WL_CONNECTED)
		{
			networkerr = true;
			Serial.println("WiFi disconnection");
			// Serial.print("WiFi reconnect: ");
			// Serial.println(WiFi.status());
			// WiFi.disconnect();
			// WiFi.begin(wifi_ssid, wifi_key);
			// WiFi.reconnect();
			delay(3000);
		}

		getLocalTime(&timeInfo);
		for (const auto &e : pubMinutes)
		{
			if (e == timeInfo.tm_min && e != prevPubMin)
			{
				doPublish = true;
			}
		}
		if (doPublish)
		{
			char message[256];

			prevPubMin = timeInfo.tm_min;
			if (!mqttClient->connected())
			{
				networkerr = true;
				Serial.println("cloud IoT reconnect");
				mqtt->mqttConnect();
			}
			xStatus = xSemaphoreTake(xMutexData, xTicksToWait);
			int len = 0;
			if (xStatus == pdTRUE)
			{
				len = snprintf(message, sizeof(message),
							   "{\"timestamp\":\"%lu\",\"light\":\"%u\",\"temp\":\"%.2f\","
							   "\"humidity\":\"%.2f\",\"pressure\":\"%.2f\","
							   "\"eTVOC\":\"%d\",\"eCO2\":\"%d\"}",
							   timestamp, light, temp, hum, pressure, etvoc, eco2);
			}
			xSemaphoreGive(xMutexData);
			Serial.print("publish: ");
			Serial.println(message);
			if (0 < len)
			{
				if (!(mqtt->publishTelemetry(message, len)))
				{
					Serial.println("Publish error");
					networkerr = true;
				}
			}
			// mqttClient->disconnect();
		}
	}
}

void setup()
{
	Preferences preferences;
	uint8_t mac[6];

	M5.begin();
	// setCpuFrequencyMhz(80);
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

	timer = timerBegin(0, 80, true);				  //timer 0, div 80
	timerAttachInterrupt(timer, &resetModule, true);  //attach callback
	timerAlarmWrite(timer, wdtTimeout * 1000, false); //set time in us
	timerAlarmEnable(timer);						  //enable interrupt

	M5.Lcd.printf("Connecting to %s ", wifi_ssid);
	WiFi.begin(wifi_ssid, wifi_key);
	WiFi.macAddress(mac);
	Serial.printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x",
				  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	Serial.println("");
	sprintf(device_id, "m%02x%02x%02x%02x%02x%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		M5.Lcd.print(".");
	}
	M5.Lcd.println(" CONNECTED");

	configTzTime("JST-9", "ntp.nict.jp"); // Set ntp time to local
	delay(1000);
	M5.Lcd.println("Setup CloudIoT");
	setupCloudIoT();
	delay(1000);

	M5.Lcd.println("Start BLEScan");
	BLEDevice::init("");
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setInterval(100);
	pBLEScan->setWindow(90);
	pBLEScan->setActiveScan(false);

	xMutexData = xSemaphoreCreateMutex();
	xTaskCreatePinnedToCore(ledBlinkingTask, "ledBlinkingTask", configMINIMAL_STACK_SIZE, NULL, 3, &xhandle_ledblink, 0);
	xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 2048, NULL, 3, &xhandle_blescan, 1);
	xTaskCreatePinnedToCore(cloudIoTTask, "cloudIoTTask", 4096, NULL, 3, &xhandle_cloudiot, 1);

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
	if (M5.BtnB.wasPressed())
	{
		esp_restart();
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
		M5.Lcd.println("");
		M5.Lcd.print("RSSI: ");
		M5.Lcd.println(WiFi.RSSI());
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
