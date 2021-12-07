#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>
#include "OTA.h"

const char *ssid = "Your SSID";
const char *password = "Your Password";

WiFiServer server(80);
String header;
String ledState = "off";
String motor_relay_state = "off";
String high_level_switch_status = "off";
String low_level_switch_status = "off";
String lastLine1 = "";
String lastLine2 = "";
String lcd_backlight_status = "off";
String is_auto_str = "Auto";

IPAddress local_IP(192, 168, 0, 5);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

#define led LED_BUILTIN
#define motor_relay D7
#define low_level_reed D5
#define high_level_reed D6

bool motor_act_auto = false;
bool motor_act_man = false;
bool short_cycling_err = false;
bool lcd_backlight = false;
bool is_auto = true;

unsigned long lastDisplayRefreshTime = 0;
unsigned long currentTime = millis();
unsigned long pump_on_time = 0;
unsigned long pump_off_time = 0;
unsigned long stop_frequent_activation = 3600000;
unsigned long stop_manual_frequent_activation = 300000;
unsigned long previousTime = 0;
unsigned int last_run_duration = 0;
const long timeoutTime = 6000;
LiquidCrystal_I2C lcd(0x27, 16, 2);

void displayStuff(String line1, String line2 = "Low " + low_level_switch_status + " High " + high_level_switch_status)
{
  if (lastDisplayRefreshTime == 0)
  {
    lastDisplayRefreshTime = millis();
  }
  // only update if things have changed and at least 1 second has passed
  if (millis() - lastDisplayRefreshTime > 1000 && ((lastLine1 == "" || lastLine1 != line1) || (lastLine2 == "" || lastLine2 != line2)))
  {
    lastLine1 = line1;
    lastLine2 = line2;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
    lastDisplayRefreshTime = millis();
  }
}

void setup()
{
  // Setup OTA
  setupOTA("PumpCtrlOTA", ssid, password);

  // initialize LCD Display
  lcd.init();
  lcd.noBacklight();
  Serial.begin(115200);
  pinMode(led, OUTPUT);
  pinMode(motor_relay, OUTPUT);
  pinMode(low_level_reed, INPUT_PULLUP);
  pinMode(high_level_reed, INPUT_PULLUP);
  digitalWrite(motor_relay, LOW);
  digitalWrite(led, HIGH);

  Serial.println("Configuring static IP...");
  if (!WiFi.config(local_IP, gateway, subnet))
  {
    Serial.println("STA Failed to configure");
  }
  displayStuff("Connecting WiFi", ssid);
  Serial.print("Connecting to WiFi router");

  delay(1000);
  // Connect to Wi-Fi network with SSID and password
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  // Print local IP address and start web server
  Serial.println();
  Serial.print("Connected IP: ");
  Serial.println(WiFi.localIP().toString());
  displayStuff("Connected to", ssid);
  delay(1000);
  server.begin();
}

void switch_status_update()
{
  if (digitalRead(low_level_reed) == 0)
  {
    low_level_switch_status = "up";
  }
  else
  {
    low_level_switch_status = "down";
  }
  if (digitalRead(high_level_reed) == 0)
  {
    high_level_switch_status = "up";
  }
  else
  {
    high_level_switch_status = "down";
  }
}

void led_blink(uint8_t times = 5, uint8_t delayTime = 80)
{
  for (int i = 0; i < times; i++)
  {
    ArduinoOTA.handle();
    digitalWrite(led, LOW);
    delay(delayTime);
    digitalWrite(led, HIGH);
    delay(delayTime);
  }
}

void loop()
{
  ArduinoOTA.handle();
  switch_status_update();
  displayStuff("IP " + WiFi.localIP().toString());
  if (short_cycling_err != true)
  {

    if (low_level_switch_status == "down")
    {
      if ((pump_off_time == 0 || millis() - pump_off_time >= stop_frequent_activation) && is_auto == true)
      {
        motor_relay_state = "on";
        motor_act_auto = true;
        motor_act_man = false;
        digitalWrite(motor_relay, HIGH);
        digitalWrite(led, LOW);
        pump_on_time = millis();
        displayStuff("Pump auto start");
        delay(2000);
      }
      else if (millis() - pump_off_time <= stop_frequent_activation)
      {
        // Short cycling error! manual only from this point
        short_cycling_err = true;
        Serial.println("Short cycling error activated.");
        displayStuff("     ERROR", "Short Cycling!");
        led_blink(30, 100);
      }
    }
  }
  else
  {
    displayStuff("Short Cycling!", "Reboot Manually!");
    led_blink(50, 50);
    Serial.println("Short cycling error active - Unable to start motor in auto");
  }

  WiFiClient client = server.available(); // Listen for incoming clients

  if (high_level_switch_status == "up" && motor_relay_state == "on" && is_auto == true && (motor_act_auto == true || !client))
  {
    motor_relay_state = "off";
    motor_act_auto = false;
    digitalWrite(motor_relay, LOW);
    digitalWrite(led, HIGH);
    pump_off_time = millis();
    displayStuff("Pump auto stop");
    last_run_duration = pump_on_time - pump_off_time;
    delay(2000);
  }

  if (client)
  {                          // If a new client connects,
    String currentLine = ""; // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime)
    { // loop while the client's connected
      switch_status_update();
      displayStuff("Client Connected", "Low " + low_level_switch_status + " High " + high_level_switch_status);
      currentTime = millis();
      if (client.available())
      {                         // if there's bytes to read from the client,
        char c = client.read(); // read a byte, then
        header += c;
        if (c == '\n')
        { // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0)
          {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // turns the Pump on and off
            if (header.indexOf("GET /pump/on") >= 0 && (pump_on_time == 0 || millis() - pump_on_time > stop_manual_frequent_activation))
            {
              motor_relay_state = "on";
              digitalWrite(motor_relay, HIGH);
              digitalWrite(led, LOW);
              pump_on_time = millis();
              motor_act_auto = false;
              motor_act_man = true;
              displayStuff("Manual Start");
            }
            else if (header.indexOf("GET /pump/on") >= 0 && motor_relay_state == "off" && millis() - pump_on_time < stop_manual_frequent_activation)
            {
              displayStuff("Frequent Start", "wait 5 min");
              delay(2000);
            }
            else if (header.indexOf("GET /pump/off") >= 0 && motor_relay_state == "on")
            {
              motor_relay_state = "off";
              digitalWrite(motor_relay, LOW);
              digitalWrite(led, HIGH);
              pump_off_time = millis();
              motor_act_auto = false;
              motor_act_man = false;
              displayStuff("Manual Stop");
            }
            else if (header.indexOf("GET /reset") >= 0)
            {
              motor_relay_state = "off";
              displayStuff("!Reboot request!", "!Close browser!");
              client.stop();
              delay(5000);
              ESP.restart();
            }
            else if (header.indexOf("GET /backlight/off") >= 0)
            {
              lcd_backlight = false;
              String lcd_backlight_status = "off";
              lcd.noBacklight();
            }
            else if (header.indexOf("GET /backlight/on") >= 0)
            {
              lcd_backlight = true;
              String lcd_backlight_status = "on";
              lcd.backlight();
            }
            else if (header.indexOf("GET /auto/off") >= 0)
            {
              is_auto = false;
              is_auto_str = "Manual";
            }
            else if (header.indexOf("GET /auto/on") >= 0)
            {
              is_auto = true;
              is_auto_str = "Auto";
            }

            else
            {
              led_blink(5, 40);
              if (motor_relay_state == "on")
              {
                displayStuff("Pump running!");
                digitalWrite(led, LOW);
              }
            }

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<meta http-equiv=\"refresh\" content=\"5\">");
            client.println("<title>Pump control and monitoring</title>");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica;font-size: 18px; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; border-radius: 10px; color: white; padding: 16px 100px;");
            client.println("text-decoration: none; font-size: 30px; margin: 20px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}");
            client.println(".button3 {background-color: #009418;}");
            client.println(".button4 {background-color: #00CF22;}");
            client.println(".button5 {background-color: #FF0000;}</style></head>");
            // Web Page Heading
            client.println("<body><h1>Pump Control</h1>");

            // Display current state, and ON/OFF buttons for LED

            client.println("<p>Low level switch is " + low_level_switch_status + "</p>");
            client.println("<p>High level switch is  " + high_level_switch_status + "</p>");
            client.println("<p>Motor Relay - State " + motor_relay_state + "</p>");
            client.println("<p>Motor actuation mode - " + is_auto_str + "</p>");
            if (pump_on_time != 0 && pump_off_time != 0 && pump_off_time > pump_on_time)
            {
              last_run_duration = (pump_off_time - pump_on_time) / 1000;
              client.println("<p>Last run time - " +String(int(last_run_duration / 60))+ " Minutes and "+String(last_run_duration % 60)+ " Seconds</p>");
            }

            // If the motor_relay_state is off, it displays the ON button
            if (motor_relay_state == "off")
            {
              client.println("<p><a href=\"/pump/on\"><button class=\"button\">PUMP ON</button></a></p>");
            }
            else
            {
              client.println("<p><a href=\"/pump/off\"><button class=\"button button2\">PUMP OFF</button></a></p>");
            }
            if (lcd_backlight == true)
            {
              client.println("<p><a href=\"/backlight/off\"><button class=\"button button3\">BACKLIGHT OFF</button></a></p>");
            }
            else
            {
              client.println("<p><a href=\"/backlight/on\"><button class=\"button button4\">BACKLIGHT ON</button></a></p>");
            }
            if (is_auto == true)
            {
              client.println("<p><a href=\"/auto/off\"><button class=\"button button3\">MANUAL CONTROL</button></a></p>");
            }
            else
            {
              client.println("<p><a href=\"/auto/on\"><button class=\"button button4\">AUTO CONTROL</button></a></p>");
            }

            client.println("<p><a href=\"/reset\"><button class=\"button button5\">RESET</button></a></p>");
            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          }
          else
          { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        }
        else if (c != '\r')
        {                   // if you got anything else but a carriage return character,
          currentLine += c; // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
  }
}