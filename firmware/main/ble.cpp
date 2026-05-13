#include "ble.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

static const char *TAG = "ble";

#define DEVICE_NAME "Claude Controller"

#define SERVICE_UUID  "4c41555a-4465-7669-6365-000000000001"
#define RX_CHAR_UUID  "4c41555a-4465-7669-6365-000000000002"
#define TX_CHAR_UUID  "4c41555a-4465-7669-6365-000000000003"
#define REQ_CHAR_UUID "4c41555a-4465-7669-6365-000000000004"

#define HID_SERVICE_UUID 0x1812
#define HID_KEYBOARD_APPEARANCE 0x03C1

#define BLE_BUF_SIZE 512

static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
};

static NimBLEServer*         server     = nullptr;
static NimBLEHIDDevice*      hid_dev    = nullptr;
static NimBLECharacteristic* input_kbd  = nullptr;
static NimBLECharacteristic* tx_char    = nullptr;
static NimBLECharacteristic* rx_char    = nullptr;
static NimBLECharacteristic* req_char   = nullptr;

static volatile ble_state_t  state              = BLE_STATE_INIT;
static volatile bool         need_advertise     = false;
static char                  rx_buf[BLE_BUF_SIZE];
static char                  rx_buf_safe[BLE_BUF_SIZE];
static volatile bool         data_ready         = false;
static volatile bool         has_received_data  = false;
static char                  mac_str[18];
static portMUX_TYPE          rx_mux = portMUX_INITIALIZER_UNLOCKED;

static void start_advertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->reset();
    adv->addServiceUUID(SERVICE_UUID);
    adv->addServiceUUID(NimBLEUUID((uint16_t)HID_SERVICE_UUID));
    adv->setAppearance(HID_KEYBOARD_APPEARANCE);
    adv->enableScanResponse(true);
    adv->setName(DEVICE_NAME);
    bool ok = adv->start();
    state = BLE_STATE_ADVERTISING;
    ESP_LOGI(TAG, "advertising start=%s", ok ? "OK" : "FAILED");
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        (void)s;
        (void)info;
        state = BLE_STATE_CONNECTED;
        ESP_LOGI(TAG, "client connected");
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        (void)s;
        (void)info;
        ESP_LOGI(TAG, "client disconnected, reason=%d", reason);
        state = BLE_STATE_DISCONNECTED;
        need_advertise = true;
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
        (void)info;
        NimBLEAttValue val = chr->getValue();
        size_t len = val.length();
        if (len >= BLE_BUF_SIZE) len = BLE_BUF_SIZE - 1;
        taskENTER_CRITICAL(&rx_mux);
        memcpy(rx_buf, val.c_str(), len);
        rx_buf[len] = '\0';
        data_ready = true;
        has_received_data = true;
        taskEXIT_CRITICAL(&rx_mux);
    }
};

class ReqCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& info, uint16_t subValue) override {
        (void)chr;
        (void)info;
        if (subValue != 0 && !has_received_data) {
            ble_request_refresh();
        }
    }
};

void ble_init(void) {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setSecurityAuth(true, false, true);

    NimBLEAddress addr = NimBLEDevice::getAddress();
    snprintf(mac_str, sizeof(mac_str), "%s", addr.toString().c_str());
    for (int i = 0; mac_str[i]; i++) {
        if (mac_str[i] >= 'a' && mac_str[i] <= 'f') mac_str[i] -= 32;
    }
    ESP_LOGI(TAG, "device name='%s' mac=%s", DEVICE_NAME, mac_str);

    server = NimBLEDevice::createServer();
    static ServerCallbacks serverCb;
    server->setCallbacks(&serverCb);

    hid_dev = new NimBLEHIDDevice(server);
    hid_dev->setReportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    hid_dev->setManufacturer("Anthropic");
    hid_dev->setPnp(0x02, 0x05AC, 0x820A, 0x0210);
    hid_dev->setHidInfo(0x00, 0x02);
    hid_dev->setBatteryLevel(100);
    input_kbd = hid_dev->getInputReport(1);

    NimBLEService* svc = server->createService(SERVICE_UUID);

    rx_char = svc->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    static RxCallbacks rxCb;
    rx_char->setCallbacks(&rxCb);

    tx_char = svc->createCharacteristic(
        TX_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    req_char = svc->createCharacteristic(
        REQ_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY);
    static ReqCallbacks reqCb;
    req_char->setCallbacks(&reqCb);

    svc->start();
    hid_dev->startServices();
    server->start();
    start_advertising();
}

void ble_tick(void) {
    if (need_advertise) {
        need_advertise = false;
        start_advertising();
    }
}

ble_state_t ble_get_state(void)         { return state; }
const char* ble_get_device_name(void)   { return DEVICE_NAME; }
const char* ble_get_mac_address(void)   { return mac_str; }

void ble_clear_bonds(void) {
    NimBLEDevice::deleteAllBonds();
    if (state == BLE_STATE_CONNECTED && server) {
        server->disconnect(server->getPeerInfo(0).getConnHandle());
    }
    need_advertise = true;
}

bool ble_has_data(void) { return data_ready; }

const char* ble_get_data(void) {
    taskENTER_CRITICAL(&rx_mux);
    memcpy(rx_buf_safe, rx_buf, BLE_BUF_SIZE);
    data_ready = false;
    taskEXIT_CRITICAL(&rx_mux);
    return rx_buf_safe;
}

void ble_send_ack(void) {
    if (state == BLE_STATE_CONNECTED && tx_char) {
        tx_char->setValue("{\"ack\":true}");
        tx_char->notify();
    }
}

void ble_send_nack(void) {
    if (state == BLE_STATE_CONNECTED && tx_char) {
        tx_char->setValue("{\"err\":true}");
        tx_char->notify();
    }
}

void ble_request_refresh(void) {
    if (state == BLE_STATE_CONNECTED && req_char) {
        uint8_t v = 0x01;
        req_char->setValue(&v, 1);
        req_char->notify();
    }
}

void ble_keyboard_press(uint8_t key, uint8_t modifier) {
    if (state != BLE_STATE_CONNECTED || !input_kbd) return;
    uint8_t report[8] = { modifier, 0, key, 0, 0, 0, 0, 0 };
    input_kbd->setValue(report, sizeof(report));
    input_kbd->notify();
}

void ble_keyboard_release(void) {
    if (state != BLE_STATE_CONNECTED || !input_kbd) return;
    uint8_t report[8] = { 0 };
    input_kbd->setValue(report, sizeof(report));
    input_kbd->notify();
}

bool ble_parse_usage(const char *json, UsageData *out) {
    if (!json || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "parse_usage: JSON parse failed");
        return false;
    }

    bool success = false;

    cJSON *s   = cJSON_GetObjectItemCaseSensitive(root, "s");
    cJSON *sr  = cJSON_GetObjectItemCaseSensitive(root, "sr");
    cJSON *w   = cJSON_GetObjectItemCaseSensitive(root, "w");
    cJSON *wr  = cJSON_GetObjectItemCaseSensitive(root, "wr");
    cJSON *st  = cJSON_GetObjectItemCaseSensitive(root, "st");
    cJSON *ok  = cJSON_GetObjectItemCaseSensitive(root, "ok");

    if (cJSON_IsNumber(s)  &&
        cJSON_IsNumber(sr) &&
        cJSON_IsNumber(w)  &&
        cJSON_IsNumber(wr) &&
        cJSON_IsString(st) && st->valuestring != nullptr &&
        cJSON_IsBool(ok)) {

        out->session_pct        = (float)s->valuedouble;
        out->session_reset_mins = sr->valueint;
        out->weekly_pct         = (float)w->valuedouble;
        out->weekly_reset_mins  = wr->valueint;
        snprintf(out->status, sizeof(out->status), "%s", st->valuestring);
        out->ok    = cJSON_IsTrue(ok);
        out->valid = true;
        success    = true;
    } else {
        ESP_LOGW(TAG, "parse_usage: missing/typed field");
    }

    cJSON_Delete(root);
    return success;
}
