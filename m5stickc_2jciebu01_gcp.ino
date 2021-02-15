
#include <Preferences.h>
#include "BLEDevice.h"
//#include "BLEScan.h"

Preferences preferences;
static char omronSensorAddress[18];

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

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
			BLEDevice::getScan()->stop();
			//doConnect = true;
			doScan = true;
		} // Found our server
	}	  // onResult
};		  // MyAdvertisedDeviceCallbacks

void setup()
{
	Serial.begin(115200);
	Serial.println("Starting Arduino BLE Client application...");
	
	preferences.begin("Network", true);
	preferences.getString("senaddr", omronSensorAddress, sizeof(omronSensorAddress));
	preferences.end();
	BLEDevice::init("");

	// Retrieve a Scanner and set the callback we want to use to be informed when we
	// have detected a new device.  Specify that we want active scanning and start the
	// scan to run for 5 seconds.
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setInterval(1349);
	pBLEScan->setWindow(449);
	pBLEScan->setActiveScan(true);
	pBLEScan->start(5, false);
} // End of setup.

// This is the Arduino main loop function.
void loop()
{

	// If we are connected to a peer BLE Server, update the characteristic each time we are reached
	// with the current time since boot.
	if (doScan)
	{
		BLEDevice::getScan()->start(0); // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
	}

	delay(5000); // Delay a second between loops.
} // End of loop
