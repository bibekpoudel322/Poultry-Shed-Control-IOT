#define BLYNK_TEMPLATE_ID "TMPL6xnlNFNv2"
#define BLYNK_TEMPLATE_NAME "Poultry Coop"
char auth[] = "g2_a8vFGs78sK6sXbidkRXyim2FdgooC"; // Replace with your Blynk Auth Token
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);
#include <EMailSender.h>
#include <EEPROM.h>
#include <DHT.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <BlynkSimpleEsp32.h> // Include Blynk library
#define EEPROM_SIZE 20

// WiFi credentials dictionary
const char *ssid[] = {"bkpoudel_fctwn", "Baral@patihaniNet", "lamsal-wifi11_fctwn_2.4g"};
const char *password[] = {"CLB2710A59", "9845023546", "9845144415P"};
const int numNetworks = 3;
EMailSender emailSend("esp322376@gmail.com", "nejyqiqnkalzdtas"); // Replace with your email ID and password
unsigned long wifiReconnectPreviousMillis = 0;
const long wifiReconnectInterval = 600000; // 10 minutes in milliseconds
// Pin configuration
const int DHT_PIN = 6;         // DHT11 data pin connected to GPIO 6
const int RELAY_LIGHT_PIN = 4; // Relay for poultry light connected to GPIO 4 (G4)
const int RELAY_FOG_PIN = 7;   // Relay for fogging system connected to GPIO 7 (G7)
const int RELAY_HEAT_PIN = 8;  // Relay for heating system connected to GPIO 8 (G8)
const int EXTRA_RELAY = 5;     // Additional relay pin (example)

// Define EEPROM address
#define EEPROM_ADDR_TARGET_TEMP 0
#define EEPROM_ADDR_TURN_ON_HOUR 4
#define EEPROM_ADDR_NUMBER_OF_HOURS 8
// DHT sensor configuration
#define DHTTYPE DHT22 // DHT type
DHT dht(DHT_PIN, DHTTYPE);

// WiFi and NTP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 20700, 60000); // NTP server and time settings for Nepal (UTC +5:45)

// Timing variables
unsigned long previousMillisFog = 0;
const long fogInterval = 60 * 60 * 1000; // 1 hour interval for fogging control (in milliseconds)

// Hysteresis parameters for heating control
float upperThreshold = 29.0;                 // Upper threshold for turning off heating (in °C)
float lowerThreshold = upperThreshold - 4.0; // Lower threshold for turning on heating (in °C)
bool heatingActive = false;                  // Flag to track heating system state
bool foggingActive = false;

// Hysteresis parameters for cooling (fogging) control
float foggingThreshold = upperThreshold + 4.0; // Temperature threshold for fogging (in °C)

// Temperature and humidity variables
float temperature = 0.0;
float humidity = 0.0;
String tempHumStr = " ";

// Manual control flags
bool manualLightControl = false;

// Set light on and off hours (example: 7 PM to 5 AM)
int lightOnHour = 18; // 18:00 (6 PM)
int numberofHours = 8;
int lightOffHour = (lightOnHour + numberofHours) % 24; // Give additional 7 hours of light

bool email_condition_met = false; // condition to check if email is need to be sent
// Email sending related variables
unsigned long lastEmailTime = 0;
unsigned long emailTimer = 0;
const unsigned long emailInterval = 10 * 60 * 1000; // 10 minutes in milliseconds

void setup()
{
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);

    Wire.begin(0, 1);
    lcd.begin(16, 2); // Initialize the LCD with 16x2 characters
    lcd.backlight();  // Turn on the backlight (if supported)
    // Connect to Wi-Fi
    connectWiFi();

    // Initialize DHT sensor
    dht.begin();
    // Get EEROM Values
    EEPROM.get(EEPROM_ADDR_TARGET_TEMP, upperThreshold);
    EEPROM.get(EEPROM_ADDR_TURN_ON_HOUR, lightOnHour);
    EEPROM.get(EEPROM_ADDR_NUMBER_OF_HOURS, numberofHours);
    lightOffHour = (lightOnHour + numberofHours) % 24; // Give additional 7 hours of light
    lowerThreshold = upperThreshold - 4.0;
    foggingThreshold = upperThreshold + 4.0;

    // Initialize relay pins as outputs
    pinMode(RELAY_LIGHT_PIN, OUTPUT);
    pinMode(RELAY_FOG_PIN, OUTPUT);
    pinMode(RELAY_HEAT_PIN, OUTPUT);
    pinMode(EXTRA_RELAY, OUTPUT);
    // Initialize relay pins as OFF
    digitalWrite(EXTRA_RELAY, HIGH);
    digitalWrite(RELAY_LIGHT_PIN, HIGH);
    digitalWrite(RELAY_FOG_PIN, HIGH);
    digitalWrite(RELAY_HEAT_PIN, HIGH);

    // Initialize NTP client
    timeClient.begin();

    Blynk.syncVirtual(V4); // Sync manualLightControl
}

void loop()
{
    unsigned long currentMillis = millis();
    if (!WiFi.isConnected() && (currentMillis - wifiReconnectPreviousMillis >= wifiReconnectInterval))
    {
        wifiReconnectPreviousMillis = currentMillis;
        connectWiFi();
    }
    // Update NTP client and get time
    timeClient.update();
    unsigned long currentEpochTime = timeClient.getEpochTime();

    // Calculate current local time in seconds since midnight
    int currentHour = (currentEpochTime % 86400L) / 3600;
    if (currentHour >= 24)
    {
        currentHour -= 24;
    }

    // Maintain Blynk connection
    Blynk.run();

    // Call functions for each control block
    readSensors();
    if (!manualLightControl)
        controlLight(currentHour);
    controlHeating();
    controlFogging(millis());

    // Add additional logic or delays as needed
    if (email_condition_met)
    {
        if (currentMillis - emailTimer > emailInterval)
        {
            sendEmail("Optimal Condition Not Met for More Than 10 Minutes.");
            emailTimer = currentMillis; // Reset the timer
        }
    }
    delay(100); // Example: Delay for 0.1 seconds
}

// Function to read temperature and humidity
void readSensors()
{
    // Reading temperature and humidity
    temperature = dht.readTemperature(); // Read temperature as Celsius
    humidity = dht.readHumidity();       // Read humidity

    // Check if any reads failed and exit early (to try again).
    if (isnan(temperature) || isnan(humidity))
    {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }

    // Print sensor readings to Serial Monitor
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" °C\t");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %\n");
    Serial.print("TargetTemp: ");
    Serial.print(upperThreshold);
    Serial.print(" °C\t");
    Serial.print("Lower: ");
    Serial.print(lowerThreshold);
    Serial.println(" C ");
    Serial.println(" Fogging Threshold:");
    Serial.print(foggingThreshold);
    Serial.println(" C ");
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperature);
    lcd.print("C ");
    lcd.print("H:");
    lcd.print(humidity);
    lcd.print("%");
    // Create a string for temperature and Humidity
    String tempHumStr = "T: " + String(temperature) + "C H: " + String(humidity) + "%";
    // Publish sensor data to Blynk widgets
    Blynk.virtualWrite(V1, tempHumStr); // Send temperature to Blynk app (Virtual Pin V1)
}

// Function to control poultry light based on local time and Wi-Fi connection status
void controlLight(int currentHour)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        digitalWrite(RELAY_LIGHT_PIN, HIGH); // Turn off the light if Wi-Fi is not connected
        Serial.println("Poultry light turned off due to Wi-Fi disconnection.");
        return;
    }

    // Check if current time is within light on/off time
    if (currentHour >= lightOnHour || currentHour < lightOffHour)
    {
        digitalWrite(RELAY_LIGHT_PIN, LOW); // ON when LOW (inverted logic)
        Serial.println("Poultry light turned on.");
    }
    else
    {
        digitalWrite(RELAY_LIGHT_PIN, HIGH); // OFF when HIGH (inverted logic)
        Serial.println("Poultry light turned off.");
    }
}

// Function to control heating system with hysteresis
void controlHeating()
{
    temperature = dht.readTemperature(); // Read temperature as Celsius
    // Hysteresis control for heating system
    if (temperature <= lowerThreshold)
    {
        // Turn on heating system
        digitalWrite(RELAY_HEAT_PIN, LOW); // ON when LOW (inverted logic)
        digitalWrite(RELAY_FOG_PIN, HIGH);
        heatingActive = true; // Set heating system active
        email_condition_met = true;
        Serial.println("Heating system turned on.");
    }
    else if (temperature >= upperThreshold && heatingActive)
    {
        // Turn off heating system
        digitalWrite(RELAY_HEAT_PIN, HIGH); // OFF when HIGH (inverted logic)
        heatingActive = false;              // Set heating system inactive
        email_condition_met = false;
        Serial.println("Heating system turned off.");
    }
}

// Function to control fogging system based on temperature
void controlFogging(unsigned long currentMillis)
{
    temperature = dht.readTemperature();
    // Fogging system control based on time interval and temperature
    if (temperature >= foggingThreshold && currentMillis - previousMillisFog >= fogInterval)
    {
        previousMillisFog = currentMillis;
        foggingActive = true;
        email_condition_met = true;
        // Turn on fogging system
        digitalWrite(RELAY_FOG_PIN, LOW); // ON when LOW (inverted logic)
        Serial.println("Fogging system turned on.");
        delay(15000); // Keep fogging system on for 15 seconds
        // Turn off fogging system
        digitalWrite(RELAY_FOG_PIN, HIGH); // OFF when HIGH (inverted logic)
        Serial.println("Fogging system turned off.");
    }
    else if (temperature < foggingThreshold && foggingActive)
    {
        digitalWrite(RELAY_FOG_PIN, HIGH);
        foggingActive = false;
        email_condition_met = false;
    }
}

// Function to connect to Wi-Fi
void connectWiFi()
{
    Serial.println("Connecting to Wi-Fi...");
    for (int i = 0; i < numNetworks; i++)
    {
        WiFi.begin(ssid[i], password[i]);
        unsigned long startAttemptTime = millis();

        // Try to connect to WiFi for 10 seconds
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
        {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("");
            Serial.println("WiFi connected");
            Serial.print("IP address: ");
            Blynk.begin(auth, ssid[i], password[i]); // Connect to Blynk server
            Serial.println(WiFi.localIP());
            return;
        }
    }
    Serial.println("");
    Serial.println("Failed to connect to any Wi-Fi network");
}
// Blynk functions to handle manual control switches
BLYNK_WRITE(V6)
{
    int switchState = param.asInt();                   // Get switch state
    manualLightControl = ((switchState == 0) ? 0 : 1); // Set manual control flag for light relay

    if (manualLightControl)
    {
        if (switchState == 1)
        {
            digitalWrite(RELAY_LIGHT_PIN, LOW);
        }
        else if (switchState == 2)
        {
            digitalWrite(RELAY_LIGHT_PIN, HIGH);
        }
        Serial.println("Manual control: Light relay turned on.");
        manualLightControl = true;
    }
    else
    {
        Serial.println("Manual control: Light relay deactivated. Returning to normal control.");
        digitalWrite(RELAY_LIGHT_PIN, HIGH); // Turn off light relay
        manualLightControl = false;
    }
}
BLYNK_WRITE(V0)
{ // Target temperature slider (Virtual Pin V0)
    upperThreshold = param.asFloat();
    lowerThreshold = upperThreshold - 4.0;
    foggingThreshold = upperThreshold + 4.0;
    EEPROM.put(EEPROM_ADDR_TARGET_TEMP, upperThreshold);
    EEPROM.commit();
}

BLYNK_WRITE(V3)
{ // Light Turn ON Hour.V3
    lightOnHour = param.asFloat();
    lightOffHour = (lightOnHour + numberofHours) % 24;
    EEPROM.put(EEPROM_ADDR_TURN_ON_HOUR, lightOnHour);
    EEPROM.commit();
}
BLYNK_WRITE(V2)
{ // Number of hours. V2
    numberofHours = param.asFloat();
    lightOffHour = (lightOnHour + numberofHours) % 24;
    EEPROM.put(EEPROM_ADDR_NUMBER_OF_HOURS, numberofHours);
    EEPROM.commit();
}
void sendEmail(const char *mess)
{
    EMailSender::EMailMessage message;
    message.subject = "Incubator Alert";
    message.message = mess;

    EMailSender::Response resp = emailSend.send("bkpoudel44@gmail.com", message);
    delay(4000); // delay 4s.
}