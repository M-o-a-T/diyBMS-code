#include <WiFi.h>
#include <ESPmDNS.h>

#include "defines.h"
#include "SoftAP.h"
#include "EmbeddedFiles_AutoGenerated.h"

wifi_eeprom_settings DIYBMSSoftAP::_config;
const char *DIYBMSSoftAP::_configtag = "diybmswifi";

String DIYBMSSoftAP::networks;

AsyncWebServer *DIYBMSSoftAP::_myserver;

void DIYBMSSoftAP::handleRoot(AsyncWebServerRequest *request)
{
  String s;
  s += DIYBMSSoftAP::networks;
  request->send(200, "text/html", s);
}

void DIYBMSSoftAP::handleSave(AsyncWebServerRequest *request)
{
  String s;
  String ssid = request->arg("ssid");
  String password = request->arg("pass");

  if ((ssid.length() <= sizeof(_config.wifi_ssid)) && (password.length() <= sizeof(_config.wifi_passphrase)))
  {

    memset(&_config, 0, sizeof(_config));

    ssid.toCharArray(_config.wifi_ssid, sizeof(_config.wifi_ssid));
    password.toCharArray(_config.wifi_passphrase, sizeof(_config.wifi_passphrase));

    Settings::WriteConfig(_configtag, (char *)&_config, sizeof(_config));

    s = F("<p>WIFI settings saved, will reboot in 5 seconds.</p>");

    request->send(200, "text/html", s);

    //Delay 6 seconds
    for (size_t i = 0; i < 60; i++)
    {
      delay(100);
    }

    ESP.restart();
  }
  else
  {
    s = F("<p>WIFI settings too long.</p>");
    request->send(200, "text/html", s);
  }
}

bool DIYBMSSoftAP::LoadConfigFromEEPROM()
{
  return (Settings::ReadConfig(_configtag, (char *)&_config, sizeof(_config)));
}

void DIYBMSSoftAP::SetupAccessPoint(AsyncWebServer *webserver)
{
  _myserver = webserver;
  const char *ssid = "DIY_BMS_CONTROLLER";

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(100);
  int n = WiFi.scanNetworks();

  if (n == 0)
    DIYBMSSoftAP::networks = "no networks found";
  else
  {
    DIYBMSSoftAP::networks = "";
    for (int i = 0; i < n; ++i)
    {
      DIYBMSSoftAP::networks += "<option>" + WiFi.SSID(i) + "</option>";
      ESP_LOGI(TAG, "%s", WiFi.SSID(i).c_str());
      delay(2);
    }
  }

  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP(ssid);

  _myserver->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/softap.htm");
  });

  _myserver->on("/softap.htm", HTTP_GET,
                [](AsyncWebServerRequest *request) {
                  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (char *)file_softap_htm, DIYBMSSoftAP::TemplateProcessor);
                  request->send(response);
                });

  _myserver->on("/save", HTTP_POST, handleSave);
  //_myserver->onNotFound(handleNotFound);
  _myserver->begin();

  IPAddress IP = WiFi.softAPIP();

  // Set up mDNS responder, using consistent name of http://diybms.local
  if (!MDNS.begin("diybms"))
  {
    ESP_LOGE("Error setting up MDNS responder!");
  }
  else
  {
    ESP_LOGI("mDNS responder started");
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }

  ESP_LOGI(TAG, "Access point IP address: %s", IP.toString().c_str());
}

String DIYBMSSoftAP::TemplateProcessor(const String &var)
{
  if (var == "SSID")
    return DIYBMSSoftAP::networks;

  return String();
}
