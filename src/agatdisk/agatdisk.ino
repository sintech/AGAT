/*------------------------------------------------------------
	Agat disk Emulator

	Copyright 2016 Konstantin Fedorov
	email: sintechs@gmail.com
------------------------------------------------------------*/
 
#include <SdFat.h>
#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include "dwt_timer.h"
static const dwt_timer usec_timer;

LiquidCrystal_I2C  lcd(0x3f, 2, 1, 0, 4, 5, 6, 7);

#define SD_SECTOR_SIZE 4096
// buttons
#define BTN_LEFT 7
#define BTN_CENTER 8
#define BTN_RIGHT 9
// disk drive input
#define PIN_DRIVE0 14
#define PIN_STEP 0
#define PIN_DIR 1
#define PIN_SIDE 2
// disk drive output
#define PIN_INDEX 3
#define PIN_TRACK0 4
#define PIN_READ 5
#define PIN_READY 6
// Disk II 140k
#define PIN_ENABLE140 17
#define PIN_READ140 16
const int ph_pins[] = {20, 21, 22, 23};
// 10,11,12,13 - SD CARD
// 18-19 - LCD
const int SDchipSelect = 10;


volatile int track = 1;
volatile int track140 = 0;
volatile int side = 0;
int real_track = 0;
int real_track140 = 0;
int real_track_old = -1;
int real_track_old140 = -1;

volatile int active_drive = 0;
int active_drive_old = -1;
volatile int last_act_ph = 0;
volatile int cur_act_ph = 0;

volatile bool file_select_mode = false;
volatile int filelist_pos = 0;
volatile unsigned long last_micros;
byte filelist_pos_old = -1;
byte file_index = 0;
bool file_select_mode_disp = false;

//byte image_data[2][12928];
byte mfm_data[2][13100];
byte gcr_data[16][512];
int track_size = 6250 * 2;

SdFat SD;
SdFile sdFile140;
SdFile sdFile840;
struct fileInfo {
  String fname;
  String ext;
  int fsize;
  int type;
};
fileInfo file140;
fileInfo file840;
fileInfo files[100];
byte file_pos140;
byte file_pos840;

unsigned long curtime = micros();
byte bitflip[] = {0b00, 0b10, 0b01, 0b11};
byte table62[] = {0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
                  0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
                  0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
                  0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
                  0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
                  0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
                  0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
                  0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
                 };
const byte sectorMap[] = {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};
int markers[2][4]={};



void setup() {
  usec_timer.init();
  dir_t dir;
  char fname[13];

  lcd.begin (16, 2);
  lcd.setBacklightPin(3, POSITIVE);
  lcd.setBacklight(HIGH);
  lcd.home ();                   // go home
  lcd.print("Hello, Agat!");

  // pin setup
  // out
  pinMode(PIN_INDEX, OUTPUT);
  pinMode(PIN_TRACK0, OUTPUT);
  digitalWriteFast(PIN_TRACK0, LOW);
  pinMode(PIN_READ, OUTPUT);
  pinMode(PIN_READY, OUTPUT);
  digitalWriteFast(PIN_READY, HIGH);
  // input
  pinMode(PIN_DRIVE0, INPUT_PULLUP);
  pinMode(PIN_STEP, INPUT_PULLUP);
  pinMode(PIN_DIR, INPUT_PULLUP);
  pinMode(PIN_SIDE, INPUT_PULLUP);
  attachInterrupt(PIN_STEP, set_track, FALLING);
  attachInterrupt(PIN_SIDE, set_side, CHANGE);
  attachInterrupt(PIN_DRIVE0, set_enable, CHANGE);

  // 140k
  pinMode(PIN_ENABLE140, INPUT_PULLUP);
  pinMode(PIN_READ140, OUTPUT);
  for (byte i = 0; i < 4; i++) {
    pinMode(ph_pins[i], INPUT);
    attachInterrupt(ph_pins[i], set_track140, RISING);
  }
  attachInterrupt(PIN_ENABLE140, set_enable, CHANGE);

  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  attachInterrupt(BTN_LEFT, select_file, FALLING);
  attachInterrupt(BTN_CENTER, select_file, FALLING);
  attachInterrupt(BTN_RIGHT, select_file, FALLING);


  randomSeed(analogRead(0));
  Serial.begin(115200);
  delay(2000);
  /*
    while (!Serial) {
    // wait for serial port to connect. Needed for Leonardo only
    }
  */
  Serial.print("Initializing SD card...");

  if (!SD.begin(SDchipSelect, SPI_FULL_SPEED)) {
    lcd.home ();
    lcd.print("SD INIT failed!");
    Serial.println("initialization failed!");
    SD.initErrorHalt();
  }
  Serial.println("initialization done.");
  //SD.ls("/", LS_R);
  // start at beginning of root directory
  SD.vwd()->rewind();

  // find files
  while (SD.vwd()->readDir(&dir) == sizeof(dir)) {
    if (strncmp((char*)&dir.name[8], "NIM", 3) && strncmp((char*)&dir.name[8], "AIM", 3) && strncmp((char*)&dir.name[8], "DSK", 3)) continue;
    SdFile::dirName(&dir, fname);
    files[file_index].fname = fname;
    files[file_index].ext = String(fname).substring(String(fname).length() - 3).toUpperCase();
    files[file_index].fsize = dir.fileSize;

    if (dir.fileSize == 143360) files[file_index].type = 140; // DSK 140kB with NO header or footer.
    else if (dir.fileSize == 143360 + 4)  files[file_index].type = 140; // DSK 140kB with 4 byte footer.
    else if (dir.fileSize == 143360 + 128)  files[file_index].type = 140; // DSK 140kB with 128 byte footer.
    else {
      files[file_index].type = 840;
    }
    file_index++;
  }
  Serial.println("file list:");
  for (int i = 0; i < file_index; i++) {
    Serial.print(i);
    fileInfo info = files[i];
    Serial.print(" " + info.fname);
    Serial.print(", " + info.ext);
    Serial.print(", type: " + info.type);
    Serial.println(", size: " + String(info.fsize));
  }
  Serial.println();
  Serial.println(micros() - curtime);

  // preload last files
  // EEPROM
  filelist_pos=EEPROM.read(0);
  file_pos140=EEPROM.read(1);
  file_pos840=EEPROM.read(2);
  Serial.println("file pos: " + String(filelist_pos)+", 140: "+String(file_pos140)+", 840: "+String(file_pos840));

 set_side();
}

void loop() {
  // put your main code here, to run repeatedly:
  if (track == 0) real_track = 0;
  else real_track = track * 2;

  if (track140 == 0) real_track140 = 0;
  else real_track140 = track140 / 2;
// file select 
  if (file_select_mode) {
    // endless select
    if (filelist_pos < 0) filelist_pos = file_index-1;
    else if (filelist_pos >= file_index) filelist_pos = 0;

    if (filelist_pos != filelist_pos_old || !file_select_mode_disp) {
      // file select mode
      Serial.print("Enter file select mode - pos: ");   Serial.println(filelist_pos);
      lcd.clear();
      lcd.print("Select file:");
      lcd.setCursor(0, 1);
      lcd.print(filelist_pos);
      lcd.print(": ");
      lcd.print(files[filelist_pos].fname);
      lcd.setCursor(13, 0);
      lcd.print(files[filelist_pos].type);
      
      filelist_pos_old = filelist_pos;
      file_select_mode_disp = true;
    }

  } else {
    if (file_select_mode_disp) {
      EEPROM.update(0, (byte)filelist_pos);
      display_filename();
      display_track_num();
      Serial.println("Status 840: " + String(digitalRead(PIN_DRIVE0)) + "Status 140: " + String(digitalRead(PIN_ENABLE140)));
    }
    file_select_mode_disp = false;
    if (files[filelist_pos].type == 140 && file_pos140 != filelist_pos) {
      file_pos140 = filelist_pos;
      real_track_old140 = -1;
      EEPROM.update(1, file_pos140);
    }
    else if (files[filelist_pos].type == 840  && file_pos840 != filelist_pos) {
      file_pos840 = filelist_pos;
      real_track_old = -1;
      EEPROM.update(2, file_pos840);
    }


    //Serial.println("NEW: "+files[filelist_pos].fname + " - old 840: "+ file840.fname + ", 140: "+file140.fname);
    // file change
    if (file840.fname != files[file_pos840].fname) {
      file840 = files[file_pos840];
      Serial.println("Loading 840: " + file840.fname);
      sdFile840.close();
      char cfilename[13];
      file840.fname.toCharArray(cfilename, file840.fname.length() + 1);
      sdFile840.open(cfilename, O_READ);
    }
    if (file140.fname != files[file_pos140].fname) {
      file140 = files[file_pos140];
      Serial.println("Loading 140: " + file140.fname);
      sdFile140.close();
      char cfilename[13];
      file140.fname.toCharArray(cfilename, file140.fname.length() + 1);
      sdFile140.open(cfilename, O_READ);
    }

    // track change 840
    if (real_track != real_track_old) { // && active_drive==840
      real_track_old = real_track;
      // track0 output
      if (real_track == 0) digitalWriteFast(PIN_TRACK0, LOW);
      else digitalWriteFast(PIN_TRACK0, HIGH);
      readTrack(sdFile840, file840, real_track);
      // ready state
      digitalWriteFast(PIN_READY, LOW);
    }
    // track change 140
    if (real_track140 != real_track_old140) { // && active_drive==140
      real_track_old140 = real_track140;
      readTrack(sdFile140, file140, real_track140);
    }
    
    // active drive change
    if (active_drive != active_drive_old) {
      Serial.println("Active drive changed to: " + String(active_drive));
      display_track_num();
      active_drive_old = active_drive;
    }

    //Serial.println("Sending track: "+ String(real_track));
    // data stream
    if (active_drive == 140) {
      sendTrack140();
    } else if (active_drive == 840) {
      if (file840.ext == "AIM") {
        sendTrackAIM(&markers[0]);
      } else sendTrack();
    }
    
  }
}


void readTrack(SdFile imageFile, fileInfo file, byte track_num) {

  byte side_num = 2;
  curtime = micros();
  Serial.println("reading file: " + file.fname + " - ext: " + file.ext);
  display_filename();

  if (file.ext == "DSK" && file.type == 140) {
    track_size = 256 * 16;
    side_num = 1;
  }
  else {
    if (file.ext == "NIM") track_size = 6250 * 2;
    else if (file.ext == "AIM") track_size = 6464 * 2;
    else if (file.ext == "DSK") track_size = 256 * 21; // header|footer
  }
  byte track_buff[track_size];
  
  // 840k + header 256 
  int pos_offset=0;
  if (file.ext == "DSK" && file.fsize == 860160 + 256) pos_offset=256;
  
  imageFile.seekSet(pos_offset + track_num * track_size);

  for (byte track_side = 0; track_side < side_num; track_side++) {
    Serial.print("Reading track: " + String(track) + "/" + String(real_track) + ", Side: " + String(track_side) + ", size: " + String(track_size)+" - ");  Serial.println(micros() - curtime);
    // change to while loop
    int read_amount = track_size;
    int data_size = SD_SECTOR_SIZE;
    while (read_amount > 0) {
      if (read_amount < data_size) data_size = read_amount;
      //      Serial.print("after side: "+String(track_side)+", amount: "+String(read_amount)+", read size: "+String(data_size)+" - ");  Serial.println(micros() - curtime);
      byte sd_sector_buff[data_size];
      imageFile.read((byte*)sd_sector_buff, data_size);
      // raw data array filling
      memcpy(track_buff + (track_size - read_amount), sd_sector_buff, data_size); //+track_size*(1-track_side)
      read_amount -= data_size;
    }
    Serial.print("  after read - ");    Serial.println(micros() - curtime);

    // encoding
    if (file.type == 140 && file.ext == "DSK") {
      encode_track_gcr(mfm_data[track_side], track_buff, track_num);
    } else {
      if (file.ext == "AIM") {
        encode_track_aim(mfm_data[track_side], track_buff, markers[track_side]);
        Serial.println("  AIM data size: "+String(markers[side][0]));
      } else if (file.ext == "DSK") {
        encode_track_dsk(mfm_data[track_side], track_buff, track_num+track_side);
      } else if (file.ext == "NIM") {
//        Serial.print(" "+String(sizeof(track_buff))+", "+String(track_size)+", "+String(sizeof(mfm_data[track_side]))+" - ");  Serial.println(micros() - curtime);
        memcpy(mfm_data[track_side], track_buff, sizeof(track_buff));
      }
    }

    Serial.print("  after encode - ");    Serial.println(micros() - curtime);

  }
  if (file.type == 840 &&file.ext == "DSK") track_size = 6250 * 2;

  Serial.print("after image_data array fill - ");    Serial.println(micros() - curtime);

  display_track_num();
  Serial.print("after display - ");    Serial.println(micros() - curtime);
  Serial.println();
}


void select_file () {
  if ((long)(micros() - last_micros) >= 200 * 1000) {
    if (digitalRead(BTN_CENTER) == LOW) {
      file_select_mode = ! file_select_mode;
    }
    if (digitalRead(BTN_LEFT) == LOW && file_select_mode) {
      filelist_pos--;
    }
    else if (digitalRead(BTN_RIGHT) == LOW && file_select_mode) {
      filelist_pos++;
    }
    last_micros = micros();
  }
}

void set_track() {
  if (active_drive!=840) return;
  int step_dir = digitalRead(PIN_DIR);
  if (step_dir == HIGH) track--;
  else track++;
  if (track < 0) track = 0;
}

void set_side() {
  side = ! digitalRead(PIN_SIDE);
}

void set_enable() {
  bool enable840 = digitalRead(PIN_DRIVE0);
  bool enable140 = digitalRead(PIN_ENABLE140);
  if (enable140 == LOW) {
    pinMode(PIN_READ140, OUTPUT);
  } else {
    pinMode(PIN_READ140, INPUT);
  }
  
  if (enable840 == LOW) {
    pinMode(PIN_INDEX, OUTPUT);
    pinMode(PIN_TRACK0, OUTPUT);
    pinMode(PIN_READ, OUTPUT);
    pinMode(PIN_READY, OUTPUT);
  } else {
    pinMode(PIN_INDEX, INPUT);
    pinMode(PIN_TRACK0, INPUT);
    pinMode(PIN_READ, INPUT);
    pinMode(PIN_READY, INPUT);
  }
  
  if (enable840 == LOW && enable140 == HIGH) {
    active_drive = 840;
  }
  else if (enable840 == HIGH && enable140 == LOW) {
    active_drive = 140;
  }
  else if (enable840 == LOW && enable140 == LOW) {
    active_drive = 840;
  }
  else active_drive = 0;
}

void set_track140() {
  if (active_drive!=140) return;
  for (int i = 0; i <= 3; i++) {
    if (last_act_ph == i) continue;
    if (digitalRead(ph_pins[i]) == HIGH) {
      cur_act_ph = i;
      break;
    }
  }
  if (cur_act_ph != last_act_ph) {
    if (last_act_ph == 3 && cur_act_ph == 0) track140++;
    else if (last_act_ph == 0 && cur_act_ph == 3) track140--;
    else if (cur_act_ph > last_act_ph) track140++;
    else if (cur_act_ph < last_act_ph) track140--;
    if (track140 < 0) track140 = 0;
    last_act_ph = cur_act_ph;
  }
}

void display_track_num() {
  if (active_drive == 840) {
    lcd.setCursor(12, 1);
    lcd.print("> ");
  } else if (active_drive == 140) {
    lcd.setCursor(12, 0);
    lcd.print("> ");
  } else {
    lcd.setCursor(12, 1);
    lcd.print("  ");
    lcd.setCursor(12, 0);
    lcd.print("  ");
  }
// 840
  if (real_track >= 100) lcd.setCursor(13, 1);
  else lcd.setCursor(14, 1);
  lcd.print(real_track + side);
// 140
  lcd.setCursor(14, 0);
  lcd.print(real_track140);
}

void display_filename() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("" + file140.fname);
  lcd.setCursor(0, 1);
  lcd.print("" + file840.fname);
}
