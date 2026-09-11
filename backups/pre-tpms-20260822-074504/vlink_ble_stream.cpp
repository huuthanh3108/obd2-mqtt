#include "vlink_ble_stream.h"

static constexpr size_t VLINK_WRITE_CHUNK_SIZE = 1;
static constexpr uint32_t VLINK_MIN_WRITE_GAP_MS = 80;
static constexpr uint32_t VLINK_INTER_BYTE_GAP_MS = 20;
static constexpr uint32_t VLINK_POST_COMMAND_GAP_MS = 120;
static constexpr size_t VLINK_MAX_RX_BUFFER = 512;

class VLinkBLEStream::ClientCallbacks final : public NimBLEClientCallbacks {
public:
    explicit ClientCallbacks(VLinkBLEStream *owner) : owner(owner) {}

    void onDisconnect(NimBLEClient *client, int reason) override {
        Serial.printf("VLinkBLEStream: disconnected, reason %d\n", reason);
        if (owner != nullptr && owner->disconnectCallback != nullptr) {
            owner->disconnectCallback();
        }
    }

private:
    VLinkBLEStream *owner;
};

bool VLinkBLEStream::begin(const String &localName,
                           const std::string &serviceUUID,
                           const std::string &rxUUID,
                           const std::string &txUUID) {
    this->localName = localName;
    this->serviceUUID = NimBLEUUID(serviceUUID);
    this->rxUUID = NimBLEUUID(rxUUID);
    this->txUUID = NimBLEUUID(txUUID);

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init(this->localName.c_str());
    }
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, true);

    init = true;
    return true;
}

bool VLinkBLEStream::connect(const NimBLEAddress &remoteAddress) {
    disconnect();

    const NimBLEAdvertisedDevice *advertisedDevice = nullptr;
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan != nullptr) {
        Serial.printf("VLinkBLEStream: scanning for %s\n", remoteAddress.toString().c_str());
        scan->stop();
        scan->clearResults();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(99);
        scan->setMaxResults(32);
        NimBLEScanResults results = scan->getResults(8 * 1000, false);
        Serial.printf("VLinkBLEStream: scan found %d devices\n", results.getCount());
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *device = results.getDevice(i);
            if (device == nullptr) {
                continue;
            }

            if (device->getAddress().toString() == remoteAddress.toString()) {
                advertisedDevice = device;
                Serial.printf("VLinkBLEStream: found %s (%s), RSSI %d, type %u\n",
                              device->getAddress().toString().c_str(),
                              device->getName().empty() ? "<no name>" : device->getName().c_str(),
                              device->getRSSI(),
                              device->getAddress().getType());
                break;
            }
        }
    }

    client = NimBLEDevice::createClient();
    if (client == nullptr) {
        Serial.println("VLinkBLEStream: failed to create NimBLE client");
        return false;
    }

    client->setClientCallbacks(new ClientCallbacks(this), true);
    client->setConnectionParams(24, 48, 0, 300);
    client->setConnectTimeout(15 * 1000);

    Serial.printf("VLinkBLEStream: connecting to %s\n", remoteAddress.toString().c_str());
    const bool connected = advertisedDevice != nullptr
                           ? client->connect(advertisedDevice, true, false, false)
                           : client->connect(remoteAddress, true, false, false);
    if (!connected) {
        Serial.println("VLinkBLEStream: GAP connect failed");
        NimBLEDevice::deleteClient(client);
        client = nullptr;
        return false;
    }

    Serial.printf("VLinkBLEStream: connected, RSSI %d\n", client->getRssi());
    client->updateConnParams(24, 48, 0, 300);

    NimBLERemoteService *service = client->getService(serviceUUID);
    if (service == nullptr) {
        Serial.printf("VLinkBLEStream: service not found %s\n", serviceUUID.toString().c_str());
        disconnect();
        return false;
    }

    rxCharacteristic = service->getCharacteristic(rxUUID);
    txCharacteristic = service->getCharacteristic(txUUID);
    if (rxCharacteristic == nullptr) {
        Serial.printf("VLinkBLEStream: RX characteristic not found %s\n", rxUUID.toString().c_str());
        disconnect();
        return false;
    }
    if (txCharacteristic == nullptr) {
        Serial.printf("VLinkBLEStream: TX characteristic not found %s\n", txUUID.toString().c_str());
        disconnect();
        return false;
    }

    if (rxCharacteristic->canNotify()) {
        if (!rxCharacteristic->subscribe(true, [this](NimBLERemoteCharacteristic *characteristic,
                                                     uint8_t *data,
                                                     size_t length,
                                                     bool isNotify) {
            handleNotify(characteristic, data, length, isNotify);
        })) {
            Serial.println("VLinkBLEStream: notify subscribe failed");
            disconnect();
            return false;
        }
        Serial.println("VLinkBLEStream: notify subscribed");
    } else if (rxCharacteristic->canIndicate()) {
        if (!rxCharacteristic->subscribe(false, [this](NimBLERemoteCharacteristic *characteristic,
                                                      uint8_t *data,
                                                      size_t length,
                                                      bool isNotify) {
            handleNotify(characteristic, data, length, isNotify);
        })) {
            Serial.println("VLinkBLEStream: indicate subscribe failed");
            disconnect();
            return false;
        }
        Serial.println("VLinkBLEStream: indicate subscribed");
    } else {
        Serial.printf("VLinkBLEStream: RX characteristic %s cannot notify or indicate\n",
                      rxUUID.toString().c_str());
        disconnect();
        return false;
    }

    rxBuffer.clear();
    delay(300);
    return true;
}

bool VLinkBLEStream::connected() const {
    return client != nullptr && client->isConnected();
}

bool VLinkBLEStream::disconnect() {
    rxCharacteristic = nullptr;
    txCharacteristic = nullptr;

    if (client != nullptr) {
        if (client->isConnected()) {
            client->disconnect();
        }
        NimBLEDevice::deleteClient(client);
        client = nullptr;
        return true;
    }

    return false;
}

bool VLinkBLEStream::isClosed() const {
    return !connected();
}

void VLinkBLEStream::end() {
    disconnect();
    init = false;
}

void VLinkBLEStream::onDisconnect(const DisconnectCallback &callback) {
    disconnectCallback = callback;
}

VLinkBLEStream::operator bool() const {
    return init;
}

int VLinkBLEStream::available() {
    std::lock_guard<std::mutex> lock(rxMutex);
    return static_cast<int>(rxBuffer.length());
}

int VLinkBLEStream::peek() {
    std::lock_guard<std::mutex> lock(rxMutex);
    if (!rxBuffer.empty()) {
        return static_cast<uint8_t>(rxBuffer[0]);
    }

    return -1;
}

int VLinkBLEStream::read() {
    std::lock_guard<std::mutex> lock(rxMutex);
    if (!rxBuffer.empty()) {
        uint8_t value = rxBuffer[0];
        rxBuffer.erase(0, 1);
        return value;
    }

    return -1;
}

size_t VLinkBLEStream::write(uint8_t c) {
    return write(&c, 1);
}

size_t VLinkBLEStream::write(const uint8_t *data, size_t size) {
    if (txCharacteristic == nullptr || data == nullptr || size == 0) {
        return 0;
    }

    if (!connected()) {
        return 0;
    }

    const unsigned long now = millis();
    if (lastWriteAt != 0 && now - lastWriteAt < VLINK_MIN_WRITE_GAP_MS) {
        delay(VLINK_MIN_WRITE_GAP_MS - (now - lastWriteAt));
    }

    const bool writeNoResponse = txCharacteristic->canWriteNoResponse();
    const bool writeWithResponse = txCharacteristic->canWrite();
    if (!writeNoResponse && !writeWithResponse) {
        Serial.println("VLinkBLEStream: TX characteristic is not writable");
        return 0;
    }

    size_t written = 0;
    while (written < size) {
        const size_t remaining = size - written;
        const size_t chunkSize = remaining < VLINK_WRITE_CHUNK_SIZE ? remaining : VLINK_WRITE_CHUNK_SIZE;
        if (!connected()) {
            break;
        }
        if (!txCharacteristic->writeValue(data + written, chunkSize, !writeNoResponse)) {
            Serial.println("VLinkBLEStream: write failed");
            break;
        }
        written += chunkSize;
        lastWriteAt = millis();
        delay(VLINK_INTER_BYTE_GAP_MS);
    }

    delay(VLINK_POST_COMMAND_GAP_MS);
    return size;
}

void VLinkBLEStream::flush() {
    std::lock_guard<std::mutex> lock(rxMutex);
    rxBuffer.clear();
}

void VLinkBLEStream::handleNotify(NimBLERemoteCharacteristic *characteristic,
                                  uint8_t *data,
                                  size_t length,
                                  bool isNotify) {
    if (data == nullptr || length == 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(rxMutex);
        rxBuffer.append(reinterpret_cast<char *>(data), length);
        if (rxBuffer.length() > VLINK_MAX_RX_BUFFER) {
            rxBuffer.erase(0, rxBuffer.length() - VLINK_MAX_RX_BUFFER);
        }
    }

#if 0
    Serial.print("VLinkBLEStream RX: ");
    for (size_t i = 0; i < length; i++) {
        char c = static_cast<char>(data[i]);
        if (c == '\r') {
            Serial.print("\\r");
        } else if (c == '\n') {
            Serial.print("\\n");
        } else if (isPrintable(c)) {
            Serial.print(c);
        } else {
            Serial.printf("\\x%02X", data[i]);
        }
    }
    Serial.println();
#endif
}
