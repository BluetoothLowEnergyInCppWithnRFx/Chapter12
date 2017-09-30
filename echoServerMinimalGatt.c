#include "mbed.h"
#include "ble/BLE.h"
#include "ble/services/BatteryService.h"
#include "ble/services/DeviceInformationService.h"


/** User interface I/O **/

// instantiate USB Serial
Serial serial(USBTX, USBRX);

// Status LED
DigitalOut statusLed(LED1, 0);

// Timer for blinking the statusLed
Ticker ticker;


/** Bluetooth Peripheral Properties **/

// Broadcast name
const static char BROADCAST_NAME[] = "EchoServer";

// Service UUID
static const uint16_t customServiceUuid  = 0x180C;

// array of all Service UUIDs
static const uint16_t uuid16_list[] = { customServiceUuid };

// Number of bytes in Characteristic
static const uint8_t characteristicLength = 20;

// Response Characteristic UUID
uint16_t readCharacteristicUuid = 0x2A56;

// Incoming Characteristic UUID
uint16_t writeCharacteristicUuid = 0x2A57;

// model and serial numbers
static char modelNumber[characteristicLength] = "1AB2";
static char serialNumber[characteristicLength] = "1234";
uint8_t batteryLevel = 100;

/** State **/

// flag when Central has written to a Characteristic
bool bleDataWritten = false; // true if data has been written to the characteristic

// Storage for written Characteristic  value
char bleCharacteristicValue[characteristicLength];
uint16_t bleCharacteristicValueLength = 0;



/** Functions **/

/**
 * visually signal that program has not crashed
 */
void blinkHeartbeat(void);

/**
 * Callback triggered when the ble initialization process has finished
 *
 * @param[in] params Information about the initialized Peripheral
 */
void onBluetoothInitialized(BLE::InitializationCompleteCallbackContext *params);

/**
 * This callback allows the LEDService to receive updates to the ledState Characteristic.
 *
 * @param[in] params
 *     Information about the characterisitc being updated.
 */
void onBleCharacteristicWritten(const GattWriteCallbackParams *params);

/**
 * Change the value of the read Characteristic and notify the connected client
 */
void sendBleMessage(char* value, uint16_t valueLength);

/**
 * Callback handler when a Central has disconnected
 * 
 * @param[i] params Information about the connection
 */
void onCentralDisconnected(const Gap::DisconnectionCallbackParams_t *params);


/** Build Service and Characteristic Relationships **/
// Set Up custom Characteristics
static char readValue[characteristicLength] = {0};
ReadOnlyArrayGattCharacteristic<uint8_t, sizeof(readValue)> readCharacteristic(
    readCharacteristicUuid, 
    (uint8_t *)readValue,
    GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY);
 
static char writeValue[characteristicLength] = {0};
WriteOnlyArrayGattCharacteristic<uint8_t, sizeof(writeValue)> writeCharacteristic(
    writeCharacteristicUuid, 
    (uint8_t *)writeValue,
    GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE);

// Set up custom service
GattCharacteristic *characteristics[] = {&readCharacteristic, &writeCharacteristic};
GattService        customService(customServiceUuid, characteristics, sizeof(characteristics) / sizeof(GattCharacteristic *));



/**
 * Main program and loop
 */
int main(void) {
    serial.baud(9600);
    serial.printf("Starting EchoServer\r\n");

    ticker.attach(blinkHeartbeat, 1); // Blink LED every 1 seconds 

    // initialized Bluetooth Radio
    BLE &ble = BLE::Instance(BLE::DEFAULT_INSTANCE);
    ble.init(onBluetoothInitialized);

    
    // wait for Bluetooth Radio to be initialized
    while (ble.hasInitialized()  == false);

    while (1) {
        // when a Central has written to a local Charecteristic, handle the change here
        if (bleDataWritten) {
            bleDataWritten = false; // ensure only happens once
            // do something with the bleCharacteristicValue
            
            serial.printf("Incoming message found: ");
            serial.printf(readValue);
            serial.printf("\r\n");
            
            sendBleMessage(readValue, characteristicLength);
        }
        // save power when possible
        ble.waitForEvent();
    }
}



void blinkHeartbeat(void) {
    statusLed = !statusLed; /* Do blinky on LED1 to indicate system aliveness. */
}

void onBluetoothInitialized(BLE::InitializationCompleteCallbackContext *params) {
    BLE&        ble   = params->ble;
    ble_error_t error = params->error;

    // quit if there's a problem
    if (error != BLE_ERROR_NONE) {
        return;
    }

    // Ensure that it is the default instance of BLE 
    if(ble.getInstanceID() != BLE::DEFAULT_INSTANCE) {
        return;
    }
    
    
    // Shortcut to setting up device information service
    DeviceInformationService *deviceInformationService = new DeviceInformationService(ble, BROADCAST_NAME, modelNumber, serialNumber);
    
    // shortcut to setting up battery level service
    BatteryService batteryService(ble);
    batteryService.updateBatteryLevel(batteryLevel);
    
    // attach Services
    ble.addService(customService);
    
    // if a Central writes tho a Characteristic, handle event with a callback
    ble.gattServer().onDataWritten(onBleCharacteristicWritten);
 
    // process disconnections with a callback
    ble.gap().onDisconnection(onCentralDisconnected);

    // advertising parametirs
    ble.gap().accumulateAdvertisingPayload(
        GapAdvertisingData::BREDR_NOT_SUPPORTED |   // Device is Peripheral only
        GapAdvertisingData::LE_GENERAL_DISCOVERABLE); // always discoverable
    // broadcast name
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LOCAL_NAME, (uint8_t *)BROADCAST_NAME, sizeof(BROADCAST_NAME));
    //  advertise services
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LIST_16BIT_SERVICE_IDS, (uint8_t *)uuid16_list, sizeof(uuid16_list));
    // allow connections
    ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    // advertise every 1000ms
    ble.gap().setAdvertisingInterval(1000); // 1000ms
    // begin advertising
    ble.gap().startAdvertising();
}

void onBleCharacteristicWritten(const GattWriteCallbackParams *params) {
    // clear the characteristic
    strncpy(readValue, '\0', characteristicLength * sizeof(uint8_t));
    if (params->handle == writeCharacteristic.getValueHandle()) {
        bleDataWritten = true;
        strncpy(readValue, (char*) params->data, params->len);
    }
}


void sendBleMessage(char* value, uint16_t valueLength) {
    serial.printf("Sending message: ");
    serial.printf(value);
    serial.printf("\r\n");
    BLE::Instance(BLE::DEFAULT_INSTANCE).gattServer().write(
        readCharacteristic.getValueHandle(), 
        (const uint8_t *)value,  
        valueLength);
}

void onCentralDisconnected(const Gap::DisconnectionCallbackParams_t *params) {
    BLE::Instance().gap().startAdvertising();
    serial.printf("Central disconnected\r\n");
}


