#include <Arduino.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "secrets.h"

// Some code is referenced from randomnerdtutorials.com
// https://randomnerdtutorials.com/esp32-web-server-arduino-ide/
// https://randomnerdtutorials.com/esp32-send-email-smtp-server-arduino-ide/
// https://randomnerdtutorials.com/esp32-hc-sr04-ultrasonic-arduino/

// -------- ULTRASONIC CONFIG -------- //
// Define ultrasonic pins
const int trigPin = 26;
const int echoPin = 25;

// Define sound velocity in cm/uS
#define SOUND_VELOCITY 0.034

// Set up variables for ultrasonic distances
long duration;
float distanceCm;
int distanceInt;

// -------- WIFI CONFIG -------- //
// Load Wi-Fi library
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

WiFiServer server(80);

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

// Create a task handle for the web server
TaskHandle_t web;

// Variable to store the HTTP request
String header;

void webserver(void * pvParameters);

// -------- EMAIL CONFIG -------- //
// Email cooldown variable
bool email_sent = false;
int sent_day = 0;

// SMTP variables
// moved to secrets.h

// Create session
SMTPSession smtp;

// Callback prototype
void smtpCallback(SMTP_Status status);

// -------- WATER TANK CONFIG -------- //

// Water tank empty height and alert level
#define distanceEmpty 198
#define alertHeight 51
#define criticalHeight 30

// -------- NTP CONFIG -------- //

#define utcOffsetSeconds -25200
#define ntpServer "pool.ntp.org"



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200); // Starts the serial communication
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input

  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();

  xTaskCreatePinnedToCore(
    webserver,
    "WebServer",
    10000,
    NULL,
    tskIDLE_PRIORITY,
    &web,
    0
  );

  // NTP
  configTime(utcOffsetSeconds, 3600, ntpServer);
}



void loop() {
  /*
    Loop runs on core 1 and continuously queries the ultrasonic sensor
  */

  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance
  distanceCm = duration * SOUND_VELOCITY/2;
  distanceInt = (int) distanceCm;
  
  // Prints the distance on the Serial Monitor
  Serial.print("Distance (cm): ");
  Serial.println(distanceCm);

  delay(5000);
}

void webserver(void * pvParameters) {
  /*
    Webserver runs on core 0 and runs the web server and email
  */

   // Enable SMTP debug output
  smtp.debug(1);

  // Set the callback function
  smtp.callback(smtpCallback);

  // Create the session
  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = FROM_EMAIL;
  session.login.password = PASSWORD;
  session.login.user_domain = "";

  // Create the message headers
  SMTP_Message message;
  message.sender.name = "VE6HM";
  message.sender.email = "esp32@homelab.system32.ca";
  message.subject = "VE6HM Water Level Alert";
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  // Time printing delays
  unsigned long lastPrint = millis();

  delay(5000);

  while (true) {
    WiFiClient client = server.available();   // Listen for incoming clients

      if (client) {                             // If a new client connects,
        currentTime = millis();
        previousTime = currentTime;
        Serial.println("New Client.");          // print a message out in the serial port
        String currentLine = "";                // make a String to hold incoming data from the client
        while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
          currentTime = millis();
          if (client.available()) {             // if there's bytes to read from the client,
            char c = client.read();             // read a byte, then
            Serial.write(c);                    // print it out the serial monitor
            header += c;
            if (c == '\n') {                    // if the byte is a newline character
              // if the current line is blank, you got two newline characters in a row.
              // that's the end of the client HTTP request, so send a response:
              if (currentLine.length() == 0) {
                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                // and a content-type so the client knows what's coming, then a blank line:
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/html");
                client.println("Connection: close");
                client.println();
                
                // Display the HTML web page
                client.println("<!DOCTYPE html><html>");
                client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                client.println("<link rel=\"icon\" href=\"data:,\">");
                // CSS to style the on/off buttons 
                // Feel free to change the background-color and font-size attributes to fit your preferences
                client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}</style>");
                
                // Web Page Heading
                client.println("<body><h1>ESP32 Web Server</h1>");
                
                // Display current state  
                client.println("<p>Water Level: " + String(distanceEmpty - distanceInt) + " cm</p>");

                client.println("</body></html>");
                
                // The HTTP response ends with another blank line
                client.println();
                // Break out of the while loop
                break;
              } else { // if you got a newline, then clear currentLine
                currentLine = "";
              }
            } else if (c != '\r') {  // if you got anything else but a carriage return character,
              currentLine += c;      // add it to the end of the currentLine
            }
          }
        }
        // Clear the header variable
        header = "";
        // Close the connection
        client.stop();
        Serial.println("Client disconnected.");
        Serial.println("");
      }

    // Check time to reset email latch
    struct tm time;
   
    if(!getLocalTime(&time)){
      Serial.println("Could not obtain time info");
      return;
    }

    // Check if water level is below alert height and fire email
  
    if ((distanceEmpty - distanceInt) < alertHeight && (!email_sent)) {
      String textMsg = "Water level at HM site is " + (String) (distanceEmpty - distanceCm) + " cm";
      message.text.content = textMsg.c_str();

      message.clearRecipients();

      if ((distanceEmpty - distanceInt) < criticalHeight) {
        message.addRecipient("Exec", CRITICAL_EMAIL);
      } else message.addRecipient("VE6HM SIG", TO_EMAIL);

      /* Connect to server with the session config */
      if (!smtp.connect(&session))
        return;

      /* Start sending Email and close the session */
      if (!MailClient.sendMail(&smtp, &message))
        Serial.println("Error sending Email, " + smtp.errorReason());

      email_sent = true;
      sent_day = time.tm_mday;
    }

    // Reset email latch after midnight
    if (sent_day != time.tm_mday) {
      email_sent = false;
    }
    
    // Print time every minute
    if (lastPrint + 60000 < millis() || millis() - lastPrint < 0) {
      Serial.println(asctime(&time));
      lastPrint = millis();
    }

  }
}
void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");
  }
}