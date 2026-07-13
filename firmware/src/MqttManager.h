#pragma once

#include <PubSubClient.h>
#include <WiFiClient.h>

#include <cstdint>

// Wraps the MQTT topic design from PROJECT_PLAN.md section 8:
//   site/<zone>/thermal/hotspots  (published)
//   site/<zone>/status            (published, retained + LWT)
//   site/<zone>/control/setpoint   (subscribed)
class MqttManager {
public:
  using SetpointCallback = void (*)(float setpointC);

  MqttManager(const char* broker, int port, const char* zone, const char* clientId);

  bool begin();
  void loop();
  bool connected();

  void publishHotspotsJson(const char* json);
  void publishStatus(const char* status);

  void setSetpointCallback(SetpointCallback cb) { setpointCallback_ = cb; }

private:
  WiFiClient wifiClient_;
  PubSubClient mqttClient_;
  const char* broker_;
  int port_;
  const char* zone_;
  const char* clientId_;
  char topicHotspots_[64];
  char topicStatus_[64];
  char topicSetpoint_[64];
  SetpointCallback setpointCallback_ = nullptr;

  bool reconnect();
  void onMessage(char* topic, uint8_t* payload, unsigned int length);

  static MqttManager* instance_;
  static void staticCallback(char* topic, uint8_t* payload, unsigned int length);
};
