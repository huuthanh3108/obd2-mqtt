#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Stream.h>
#include <functional>
#include <mutex>
#include <string>

class VLinkBLEStream : public Stream {
public:
    using DisconnectCallback = std::function<void()>;

    bool begin(const String &localName = String(),
               const std::string &serviceUUID = "E7810A71-73AE-499D-8C15-FAA9AEF0C3F2",
               const std::string &rxUUID = "BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F",
               const std::string &txUUID = "BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F");

    bool connect(const NimBLEAddress &remoteAddress);
    bool connected() const;
    bool disconnect();
    bool isClosed() const;
    void end();

    void onDisconnect(const DisconnectCallback &callback);

    explicit operator bool() const;

    int available() override;
    int peek() override;
    int read() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *data, size_t size) override;
    void flush() override;

private:
    class ClientCallbacks;

    bool init = false;
    String localName;
    std::string rxBuffer;
    std::mutex rxMutex;

    NimBLEUUID serviceUUID;
    NimBLEUUID rxUUID;
    NimBLEUUID txUUID;

    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *rxCharacteristic = nullptr;
    NimBLERemoteCharacteristic *txCharacteristic = nullptr;

    DisconnectCallback disconnectCallback = nullptr;
    unsigned long lastWriteAt = 0;

    void handleNotify(NimBLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify);
};
