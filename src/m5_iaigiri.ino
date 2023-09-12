#include "M5StickCPlus.h"
#include "Arduino.h"
#include "Audio.h"
#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

// Digital I/O used
#define I2S_DOUT 25
#define I2S_BCLK 26
#define I2S_LRC  0
#define FAILED_SCORE 10000// 合図の前の切ってしまった
const String MASTER_MAC = "4c:75:25:9e:9d:32";//1PとなるM5StickCPlusのMACアドレス

Audio audio;

// 加速度の閾値（適宜調整してください）
const float accelerationThreshold = 2.0; // 例：10.0 m/s^2
enum GameModeEnum {IDLING, WAITING, JUDGEMENT};
enum GameModeEnum gameMode;
enum BluetoothSerialDataEnum {START, SCORE, WIN, LOSE, NONE};
enum BluetoothSerialDataEnum bluetoothSerialData;
String receivedData = "";
boolean is1P = false;// 1Pかどうか
boolean isNpc = false;// NPC対戦かどうか
long enemyScore = 0;
long myScore = 0;
long npcScore = 0;
float ax, ay, az;
float accelerationMagnitude;
unsigned long firedMillis = 0;
unsigned long iaigiriedMillis = 0;
long fireTime = 3000;       // 待機するインターバル（ミリ秒）
boolean iaigiried = false; //抜刀したかのフラグ
boolean iaigiriPlay = false; //抜刀音声を再生するフラグ
boolean fired = false; //火蓋が落とされたかのフラグ
boolean firePlay = false; //火蓋が落とされた音声を再生するフラグ
long diffTime = 0;//1Pとの時刻のズレ
void taskTimer(void *pvParameters)
{
    M5.Lcd.print("fireTime:");
    M5.Lcd.println(fireTime);
    long waitingTime = fireTime - millis() + diffTime;
    M5.Lcd.print("waitingTime:");
    M5.Lcd.println(waitingTime);
    vTaskDelay(pdMS_TO_TICKS(waitingTime));
    M5.Lcd.print("millis() :");
    M5.Lcd.println(millis());
    M5.Lcd.println("FIRED!!");
    firedMillis = millis();
    fired = true;
    firePlay = true;
    vTaskDelete(NULL);
}

void taskIaigiri(void *pvParameters)
{
    float time = 0;
    while (true)
    {
        vTaskDelay(1);
        //IMUによる抜刀の検知
        M5.IMU.getAccelData(&ax, &ay, &az);
        accelerationMagnitude = sqrt(ax * ax + ay * ay + az * az);
        if (accelerationMagnitude > accelerationThreshold) {
          iaigiriedMillis = millis();
          iaigiried = true;
          iaigiriPlay = true;
          break;
        }
    }
    vTaskDelete(NULL);
}

void setup() {
    //初期化作業
    M5.begin();
    M5.Lcd.setRotation(3);
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("Initialize");
    M5.IMU.Init();
    Serial.begin(115200);
    //Audio周りの設定
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21);
    if (!SPIFFS.begin()) {
      M5.Lcd.println("SPIFFS Mount Failed");
      return;
    }

    //Bluetooth Serial周りの設定
    uint8_t macBT[6];
    esp_read_mac(macBT, ESP_MAC_BT);
    //比較用にMACアドレスを文字列に変換
    String macAddress;
    for (int i = 0; i < 6; i++) {
      macAddress += String(macBT[i], HEX);
      if (i < 5) macAddress += ":";
    }
    if(macAddress == MASTER_MAC){
      is1P = true;
      M5.Lcd.println("You are 1P");
      SerialBT.begin("Iaigiri_1P", true);
      boolean connected = SerialBT.connect("Iaigiri_2P");
      if(connected){
        M5.Lcd.println("CONNECTED!!");
        isNpc =false;
        //時刻同期用
        SerialBT.print(String(millis()) + "\r");
        while(true){
          if (SerialBT.available()){
            M5.Lcd.println("PRESS A TO START");
            break;
          }
        }
      }else{
        M5.Lcd.println("Cannot connected to 2P");
        isNpc =true;
      }
    }else{
      is1P = false;
      M5.Lcd.println("You are 2P");
      SerialBT.begin("Iaigiri_2P", false);
      //時刻同期
      while(true){
        if (SerialBT.available()){
          receivedData = SerialBT.readStringUntil('\r');
          M5.Lcd.print("1P millis():");
          M5.Lcd.println(receivedData);
          M5.Lcd.print("2P millis():");
          M5.Lcd.println(millis());
          diffTime = millis() - receivedData.toInt();
          SerialBT.print("OK\r");
          M5.Lcd.println("READY TO START");
          break;
        }
      }
    }
    
    //変数の初期化
    gameMode = IDLING;
    bluetoothSerialData = NONE;
}

void loop() {
    audio.loop();
    M5.update();

    if(M5.BtnB.wasPressed()){
      ESP.restart();
    }

    //Bluetooth serial
    if (SerialBT.available()){
      receivedData = SerialBT.readStringUntil('\r');
      M5.Lcd.print("received:");
      M5.Lcd.println(receivedData);
      if(receivedData == "win"){
        bluetoothSerialData = WIN;
        Serial.print("You Win !!");
        audio.connecttoFS(SPIFFS, "「やった！」.mp3");
      }else if(receivedData == "lose"){
        bluetoothSerialData = LOSE;
        Serial.print("You Lose !!");
        audio.connecttoFS(SPIFFS, "間抜け1.mp3");
      }else{// 数字データの場合
        M5.Lcd.print("GAME MODE: ");
        M5.Lcd.println(gameMode);
        if(gameMode == IDLING && !is1P){
          fireTime = receivedData.toInt();
          bluetoothSerialData = START;
        }else{
          bluetoothSerialData = SCORE;
          enemyScore = receivedData.toInt();
        }
      }
    }
    switch (gameMode){
      case IDLING:
        if ((M5.BtnA.wasPressed() && is1P) || bluetoothSerialData == START){// 1Pのみが開始することができる
            M5.Lcd.fillScreen(BLACK);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.println("GAME START");
            audio.connecttoFS(SPIFFS, "/尺八.mp3");

            //2Pに待機時間を通知
            if(is1P){
              fireTime = millis() + random(4000, 8000);//乱数で待ち時間作成
              SerialBT.print(String(fireTime) + "\r");
            }
            myScore = 0;
            enemyScore = 0;
            fired = false;
            M5.Lcd.print("now: ");
            M5.Lcd.println(millis() - diffTime);
            long playedTime = millis();
            while(true){
              audio.loop();
              if(playedTime + 4000 < millis()){//尺八音が鳴り終わるまで待機
                break;
              }
            }
            xTaskCreatePinnedToCore(taskTimer, "TaskTimer", 4096, NULL, 1, NULL, 1);
            iaigiried = false;
            xTaskCreatePinnedToCore(taskIaigiri, "TaskIaigiri", 4096, NULL, 0, NULL, 0);
            bluetoothSerialData = NONE;
            gameMode = WAITING;
        }
        break;
      case WAITING:
        if(iaigiriPlay){
          audio.stopSong();
          audio.connecttoFS(SPIFFS, "/剣で斬る3.mp3");
          long playedTime = millis();
          if(fired){// 既に合図が鳴っている場合は、音がなり終わるまで待つ（待たないと、音が成り切るまでに次の音が再生されてしまう。）
            while(true){
              audio.loop();
              if(playedTime + 1000 < millis()){
                break;
              }
            }
          }
          iaigiriPlay = false;
        }
        if(firePlay){
          audio.stopSong();
          audio.connecttoFS(SPIFFS, "/時代劇演出3.mp3");
          firePlay = false;
        }
        if(iaigiried && fired){
          myScore = iaigiriedMillis - firedMillis;
          if (myScore < 0){// 失敗時
            myScore = FAILED_SCORE;
          }
          // 1Pは判定モード、２Pは待機モードへ移行
          if(is1P){
            gameMode = JUDGEMENT;
          }else{
            SerialBT.print(String(myScore) + "\r");
            gameMode = IDLING;
          }
          npcScore = random(400, 1000);//乱数でNPCのスコアを作成;
        }
        break;
      case JUDGEMENT:
        if(myScore == 0 || enemyScore == 0){
          delay(100);
          return;//両者が居合斬りしていない場合は待機する。
        }
        bool isWin = false;
        bool myFailed = false;
        bool enemyFailed = false;
        if(isNpc){
          if(myScore < npcScore){
            isWin = true;
          }else{
            isWin = false;
          }
        }else{
          if(!is1P){//2Pの場合は判定せず、1Pの判定結果を待つ
            gameMode = IDLING;
            break;
            }
          M5.Lcd.print("myScore:");
          M5.Lcd.println(myScore);
          if(myScore < enemyScore){
            isWin = true;
          }else{
            isWin = false;
          }
        }
        if(myScore == FAILED_SCORE)myFailed = true;
        if(enemyScore == FAILED_SCORE)enemyFailed = true;
        if(myFailed){
          audio.connecttoFS(SPIFFS, "間抜け1.mp3");
          if(enemyFailed) SerialBT.print("lose\r");
          else SerialBT.print("win\r");
        }else if(isWin){
            audio.connecttoFS(SPIFFS, "「やった！」.mp3");
            delay(500);
            if(is1P) SerialBT.print("lose\r");
        }else{
            if(enemyFailed) SerialBT.print("lose\r");
            else SerialBT.print("win\r");
            delay(500);
            audio.connecttoFS(SPIFFS, "間抜け1.mp3");
        }
        gameMode = IDLING;
        break;
    }
}
