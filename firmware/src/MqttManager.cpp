#include "MqttManager.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"

MqttManager* MqttManager::instance_ = nullptr;

MqttManager::MqttManager(const char* broker, int port, const char* zone, const char* clientId)
    : mqttClient_(wifiClient_), broker_(broker), port_(port), zone_(zone), clientId_(clientId) {
  snprintf(topicHotspots_, sizeof(topicHotspots_), "site/%s/thermal/hotspots", zone_);
  snprintf(topicStatus_, sizeof(topicStatus_), "site/%s/status", zone_);
  snprintf(topicSetpoint_, sizeof(topicSetpoint_), "site/%s/control/setpoint", zone_);
  instance_ = this;
}

// Configures the client unconditionally, then attempts a first connect only
// if there's actually a link. Safe to call with WiFi down -- loop() will
// pick up the connection later without needing begin() again.
bool MqttManager::begin() {
  mqttClient_.setServer(broker_, port_);
  mqttClient_.setCallback(staticCallback);
  // PubSubClient defaults to a 15 s socket timeout; a failed connect would
  // stall the whole main loop (and with it the servos and the TFT) for that
  // long on every retry. 2 s is plenty on a LAN.
  mqttClient_.setSocketTimeout(2);

  if (WiFi.status() != WL_CONNECTED) return false;
  lastReconnectAttemptMs_ = millis();
  return reconnect();
}

bool MqttManager::reconnect() {
  if (mqttClient_.connected()) return true;
  // Last-will: topicStatus_ retains "offline" if the connection drops.
  if (mqttClient_.connect(clientId_, topicStatus_, 0, true, "offline")) {
    mqttClient_.subscribe(topicSetpoint_);
    mqttClient_.publish(topicStatus_, "online", true);
    return true;
  }
  return false;
}

void MqttManager::loop() {
  // No link, nothing to do. Without this guard PubSubClient would attempt a
  // TCP connect on every pass and block the main loop.
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqttClient_.connected()) {
    // Rate-limit reconnects. Retrying every frame turns an absent broker
    // into a visibly stuttering servo/display.
    const unsigned long now = millis();
    if (now - lastReconnectAttemptMs_ < kMqttReconnectIntervalMs) return;
    lastReconnectAttemptMs_ = now;
    reconnect();
    return;
  }

  mqttClient_.loop();
}

bool MqttManager::connected() { return mqttClient_.connected(); }

void MqttManager::publishHotspotsJson(const char* json) {
  mqttClient_.publish(topicHotspots_, json);
}

void MqttManager::publishStatus(const char* status) {
  mqttClient_.publish(topicStatus_, status, true);
}

void MqttManager::onMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (strcmp(topic, topicSetpoint_) != 0) return;
  char buf[32];
  const unsigned int n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';
  if (setpointCallback_) setpointCallback_(static_cast<float>(atof(buf)));
}

void MqttManager::staticCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (instance_) instance_->onMessage(topic, payload, length);
}
