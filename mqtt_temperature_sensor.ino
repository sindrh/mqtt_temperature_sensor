#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <array>
#include <vector>
#include <utility>
#include <sstream>

class Timer
{
private:
  uint32_t counter = 0;
  uint32_t target = 0;

public:
  Timer(uint32_t target) : target(target), counter(target)
  {
  }

  void countDown()
  {
    if (counter > 0)
    {
      counter--;
    }
  }

  void reset()
  {
    counter = target;
  }

  auto triggered() -> bool
  {
    return counter == 0;
  }
};

class ApplicationConfig
{
public:
  const char *ssid = "SECRET";
  const char *password = "SECRET";
  const char *mqtt_server = "SECRET";

  WiFiClient espClient{};
  PubSubClient client;

  Timer reconnectTimer{5000};
  // Warning: Reading too often cause trouble for Wifi connection!
  Timer sensorReadTimer{10};
  Timer publishMessageTimer{5000};
  const char *out_topic = "sensors/beer_temperature";
  uint32_t sensorValues = 0;
  uint16_t noSensorValues = 0;
  static constexpr uint16_t temperaturePin = 0;
  std::vector<std::pair<float, float>> ADCToTemperature =
      {
          {525, 11.7},
          {543, 12.8},
          {550, 13.0},
          {568, 14.0},
          {584, 15.1},
          {601, 16.1},
          {607, 16.5},
          {614, 16.9},
          {618, 17.1},
          {621, 17.3},
          {628, 17.8},
          {633, 18.1},
          {643, 18.6},
          {654, 19.1},
          {667.3, 19.9},
          {678.2, 20.6},
          {802, 28.0},
          {805, 28.2}};

  ApplicationConfig() : client(espClient)
  {
  }
};

ApplicationConfig config{};

static auto getTemperatureFromADC(const std::vector<std::pair<float, float>> &ADCToTemperature, float adc) -> float
{
  auto adc_i = 0;
  for (const auto tempADC : ADCToTemperature)
  {
    if (adc <= tempADC.first)
    {
      break;
    }
    adc_i++;
  }

  if (adc_i == 0)
  {
    return ADCToTemperature.front().second;
  }

  float diff_temp = ADCToTemperature[adc_i].second - ADCToTemperature[adc_i - 1].second;
  float diff_adc = ADCToTemperature[adc_i].first - ADCToTemperature[adc_i - 1].first;
  float a = diff_temp / diff_adc;
  float adc_between = adc - ADCToTemperature[adc_i - 1].first;

  if (adc_i == ADCToTemperature.size())
  {
    return ADCToTemperature.back().second;
  }

  float value = ADCToTemperature[adc_i - 1].second + a * adc_between;

  return value;
}

static void arrivalCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }

  Serial.println();
}

void setup_wifi()
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(config.ssid);

  WiFi.mode(WIFI_STA);
  // WiFi.setOutputPower(17.5);
  // WiFi.setPhyMode(WIFI_PHY_MODE_11B);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  //WiFi.setOutputPower(10);
  WiFi.begin(config.ssid, config.password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect()
{
  while (!config.client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    // Attempt to connect
    if (config.client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      config.client.publish("outTopic", "hello world");
      // ... and resubscribe
      config.client.subscribe("inTopic");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(config.client.state());
      Serial.println(" try again in 5 seconds");
      Serial.printf("Connection status: %d\n", WiFi.status());
      delay(5000);
    }
  }
}

void setup()
{
  pinMode(BUILTIN_LED, OUTPUT); // Initialize the BUILTIN_LED pin as an output
  Serial.begin(19200);
  delay(500);
  setup_wifi();
  config.client.setServer(config.mqtt_server, 1883);
  config.client.setCallback(arrivalCallback);
}

void loop()
{
  static uint32_t lastTime = millis();
  uint32_t nowTime = millis();

  if (nowTime - lastTime > 0)
  {
    lastTime = nowTime;
    config.reconnectTimer.countDown();
    config.sensorReadTimer.countDown();
    config.publishMessageTimer.countDown();
  }

  if (!(config.client.connected()) && config.reconnectTimer.triggered())
  {
    config.reconnectTimer.reset();
    reconnect();
  }

  if (config.client.connected())
  {
    config.client.loop();
  }

  if (config.sensorReadTimer.triggered())
  {
    config.sensorReadTimer.reset();
    auto sensorValue = analogRead(config.temperaturePin);
    config.sensorValues += sensorValue;
    config.noSensorValues += 1;
  }

  if (config.publishMessageTimer.triggered())
  {
    config.publishMessageTimer.reset();

    float sensorValueAverage = static_cast<float>(config.sensorValues) / static_cast<float>(config.noSensorValues);
    auto temperature = getTemperatureFromADC(config.ADCToTemperature, sensorValueAverage);

    Serial.println("Average ADC:");
    Serial.println(sensorValueAverage);

    Serial.println("Calculated temperature:");
    Serial.println(temperature);
    std::stringstream temperature_ss;
    temperature_ss << temperature;

    if (config.client.connected())
    {
      config.client.publish(config.out_topic, temperature_ss.str().c_str());
    }

    config.sensorValues = 0;
    config.noSensorValues = 0;
  }
}
