#include "MqttManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

MqttManager* MqttManager::instance_ = nullptr;

MqttManager::MqttManager(const char* broker, int port, const char* zone, const char* clientId)
    : mqttClient_(wifiClient_), broker_(broker), port_(port), zone_(zone), clientId_(clientId) {
  snprintf(topicHotspots_, sizeof(topicHotspots_), "site/%s/thermal/hotspots", zone_);
  snprintf(topicStatus_, sizeof(topicStatus_), "site/%s/status", zone_);
  snprintf(topicSetpoint_, sizeof(topicSetpoint_), "site/%s/control/setpoint", zone_);
  instance_ = this;
}

bool MqttManager::begin() {
  mqttClient_.setServer(broker_, port_);
  mqttClient_.setCallback(staticCallback);
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
  if (!mqttClient_.connected()) reconnect();
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
