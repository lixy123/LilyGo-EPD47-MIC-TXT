//memo缓存管理
#include "memo_historyManager.h"
#include "hz3500_36.h"

#include "CloudSpeechClient.h"
#include "I2S.h"
#include "time.h"
#include "esp_system.h"
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiAP.h>  //必须加上,否则AP模式 配置参数会有问题

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

esp_sleep_wakeup_cause_t wakeup_reason;
//30分钟一次
int TIME_TO_SLEEP = 30 * 60; //下次唤醒间隔时间(秒）

//墨水屏缓存区
uint8_t *framebuffer;

memo_historyManager* objmemo_historyManager;

String last_voice = "";
const IPAddress apIP(192, 168, 4, 1);
const char* apSSID = "ESP32SETUP";
boolean settingMode = false;
String ssidList1;

bool SPIFFS_ok = false;

//Preferences 的参数重烧固件会仍会存在！
//配置参数：

String report_address = "";  //如果配置有值,识别到的文字传给树莓派...
String report_url = "";      //如果配置有值,识别到的文字传给树莓派...

String dog_delay;     //分钟数， 定时狗健康状态监控，多少秒如果程序不在主循环上 esp32自动重启

String baidu_key;     //百度语音账号key
String baidu_secert;  //百度语音账号秘密码 注意：名称过长会有问题

String volume_low; //静音音量标准 (暂未用)
String volume_high ; //噪音音量标准(暂未用)
String volume_double ; //音量乘以倍数

String define_max1 ;  //音量波峰（最高的一个值)
String define_avg1 ;  //音量平均值

String define_max2 ;  //和上面的2个值的含义一样，区别是唤醒的音量标准会更大一些，在录音时静音的标准略低
String define_avg2;


String pre_sound;  //预缓冲秒数 2秒足够
String skip_baidu; //声音长度过短跳过识别 1==yes 0==no
String loopsleep;  //每次调用百度文字识别后的休息时间,防止过度频繁调用百度服务

String wifi_ssid1 ;
String wifi_password1  ;

uint32_t sleep_start_time = 0;

WebServer webServer(80);
Preferences preferences;

//一.程序功能： 
//     识别声音中的文字，文字显示至墨水屏
//     每段录音最长10秒(可调长),平均一段录音的文字识别时间约2-5秒，取决于网络速度
//二.硬件： 
//   1.lilygo-epd47 墨水屏
//       资料位置:  https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
//   2.MSM261S4030H0,INMP441 I2S麦克风模块均可用,约10-15块钱
//         lilygo-epd47  I2S麦克风
//             VCC3.3    VCC
//             GND       GND
//             15        WS
//             12        DA
//             13        CK
//             GND       LR
//
//三.编译环境:
//    1.Arduino 1.8.13
//    2.引用地址配置成: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json
//    3.安装： 安装esp32的官方开发包 esp32 by Espressif Systems 版本 1.05-rc6
//    4.安装此处的开发板，因为字库文件导致编译的固件太大，仅仅为了用到开发板定义。 https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library
//    5.开发板选择: TTGO T-WATCH, PSRAM选择Enabled
//    6.选择ESP32连接的端口号，编译，上传烧写固件
//    编译大小约 3.3M
//
//四.使用说明：
//  1.配置: ESP32首次运行时会自动初始化内置参数,自动进入路由器模式,创建一个ESP32SETUP的路由器，电脑连接此路由输入http://192.168.4.1进行配置
//    配置:
//    A.esp32连接的路由器和密码
//    B.百度语音的账号校验码
//      baidu_key: 一个账号字串       (必须注册获得)
//      baidu_secert: 一个账号校验码  (必须注册获得)
//      这两个参数需要注册百度语音服务,在如下网址获取 https://ai.baidu.com/tech/speech//     
//      开通中文普通话短语音识别，单次语音最长识别60秒。新用户可免费用一段时间，再用必须开通收费，1000次3.4元左右价位，如果不限制使用，最多每天可调用5000-10000次，需要增加休眠功能或控制调用条件。
//    C.其它音量监测参数: 默认是在家里安静环境下,如果周围较吵,需要将值调高
//      define_max1 每0.5秒声音峰值（声音开始判断）
//      define_avg1 每0.5秒声音均值（声音开始判断）
//      define_max2 每0.5秒声音峰值（录音中静音判断）
//      define_avg3 每0.5秒声音均值（录音中静音判断）
//  2.运行：
//    A.上电或按RESET按钮
//    B.对着墨水屏说话
//    C.墨水屏将语音识别出来的文字显示
//    D.20分钟测试有识别到新文字自动进往休眠模式节能，重新唤醒按RESET按钮
//
//五.软件代码原理:
//  1.esp32上电后实时读取I2S声音信号，检测到周围声强是否达到指定音量，达到后立即进入录音模式
//  2.如发现3秒内静音,录音停止，否则一直录音，直到10秒后停止录音，
//  3.将i2s采集到的wav原始声音数据传给百度云语音转文字服务
//  4.如果识别出文字，将文字显示至墨水屏
//  声源在1-4米内识别效果都不错，再远了识别率会低.
//
//六.其它技巧
//  1.wav采集的数字声音有点像水波振动，以数字0不基线上下跳动. 静音时采集到的数值为0.
//  2.程序会预存2秒的声音，这2秒不仅用于检测声强，也会用于文字识别。这样对于监听二个字的短语不会丢失声音数据.
//  3.声音数据: 16khz 16位 wav数据，经测试，此格式下百度文字识别效果最合适  8khz 8位wav 格式识别效果很差
//
//七.工作用电:
//  5v 70-100ma电流


hw_timer_t *timer = NULL;
const char* ntpServer = "ntp1.aliyun.com";
const long  gmtOffset_sec = 3600 * 8;
const int   daylightOffset_sec = 0;
long  last_check = 0;

const int record_time = 10;  // 录音秒数
const int waveDataSize = record_time * 32000 ;  //每秒声音文件占用32000字节
const int numCommunicationData = 8000;
//数组：8000字节缓冲区，4000次声音采集数据，1/4秒声音数据
char communicationData[numCommunicationData];   //1char=8bits 1byte=8bits 1int=8*2 1long=8*4
long writenum = 0;

struct tm timeinfo;

CloudSpeechClient* cloudSpeechClient;



int16_t max(int16_t a, int16_t b)
{
  if (a > b) return a;
  else return b;
}

int16_t min(int16_t a, int16_t b)
{
  if (b > a) return a;
  else return b;
}


void writeparams()
{
  Serial.println("Writing params to EEPROM...");

  printparams();


  preferences.putString("report_address", report_address);
  preferences.putString("report_url", report_url);


  preferences.putString("baidu_key", baidu_key);
  preferences.putString("baidu_secert", baidu_secert);



  preferences.putString("dog_delay", dog_delay);

  preferences.putString("volume_low", volume_low);
  preferences.putString("volume_high", volume_high);
  preferences.putString("volume_double", volume_double);


  preferences.putString("define_max1", define_max1);
  preferences.putString("define_avg1", define_avg1);

  preferences.putString("define_max2", define_max2);
  preferences.putString("define_avg2", define_avg2);


  preferences.putString("pre_sound", pre_sound);
  preferences.putString("skip_baidu", skip_baidu);
  preferences.putString("loopsleep", loopsleep);

  preferences.putString("wifi_ssid1", wifi_ssid1);
  preferences.putString("wifi_password1", wifi_password1);

  Serial.println("Writing params done!");
}

bool readparams()
{
  report_address = preferences.getString("report_address");

  //如果这个值还没有，说明本机器没有配置默认
  if (report_address == "")
  {
    Serial.println("首次运行，配置默认参数");

    report_address = "192.168.1.20";

    report_url = "/chat";

    baidu_key = "";
    baidu_secert =  "";

    volume_low = "15"; //静音音量值
    volume_high = "5000"; //噪音音量值
    volume_double = "40"; //音量乘以倍数

    define_max1 = "150";
    define_avg1 = "10";


    define_max2 = "120";
    define_avg2 =  "6";

    dog_delay = "10";

    pre_sound = "2";
    skip_baidu = "1";
    loopsleep = "2";  //每识别完文字后等待时间

    wifi_ssid1 = "CMCC-r3Ff";
    wifi_password1 = "9999900000";
    writeparams();
    printparams();
    return false;
  }


  report_address = preferences.getString("report_address");
  report_url =  preferences.getString("report_url");
  baidu_key = preferences.getString("baidu_key");
  baidu_secert = preferences.getString("baidu_secert");
  dog_delay = preferences.getString("dog_delay");

  volume_low = preferences.getString("volume_low");
  volume_high = preferences.getString("volume_high");
  volume_double = preferences.getString("volume_double");

  define_max1 = preferences.getString("define_max1");
  define_avg1 = preferences.getString("define_avg1");

  define_max2 = preferences.getString("define_max2");
  define_avg2 = preferences.getString("define_avg2");


  pre_sound = preferences.getString("pre_sound");
  skip_baidu = preferences.getString("skip_baidu");
  loopsleep = preferences.getString("loopsleep");

  wifi_ssid1 = preferences.getString("wifi_ssid1");
  wifi_password1 = preferences.getString("wifi_password1");

  printparams();
  return true;
}

void printparams()
{
  // return;
  Serial.println(" report_address: " + report_address);
  Serial.println(" report_url: " + report_url);
  Serial.println(" baidu_key: " + baidu_key);
  Serial.println(" baidu_secert: " + baidu_secert);

  Serial.println(" dog_delay: " + dog_delay);

  Serial.println(" volume_low: " + volume_low);
  Serial.println(" volume_high: " + volume_high);
  Serial.println(" volume_double: " + volume_double);

  Serial.println(" define_max1: " + define_max1);
  Serial.println(" define_avg1: " + define_avg1);


  Serial.println(" define_max2: " + define_max2);
  Serial.println(" define_avg2: " + define_avg2);

  Serial.println(" pre_sound: " + pre_sound);
  Serial.println(" skip_baidu: " + skip_baidu);
  Serial.println(" loopsleep: " + loopsleep);
  Serial.println(" wifi_ssid1: " + wifi_ssid1);
  Serial.println(" wifi_password1: " + wifi_password1);
}



void IRAM_ATTR resetModule() {
  ets_printf("reboot\n");
  //esp_restart_noos(); 旧api
  esp_restart();
}

String GetLocalTime()
{
  String timestr = "";
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return (timestr);
  }
  timestr = String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + " " +
            String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec) ;
  return (timestr);
}


int16_t max_int(int16_t a, int16_t b)
{
  if (a > b)
    return a;
  else
    return b;
}

int16_t min_int(int16_t a, int16_t b)
{
  if (b > a)
    return a;
  else
    return b;
}

//每半秒一次检测噪音
bool wait_loud()
{

  String timelong_str = "";
  float val_avg = 0;
  int16_t val_max = 0;

  int32_t tmpval = 0;
  int16_t val16 = 0;
  uint8_t val1, val2;
  bool aloud = false;
  Serial.println(">");
  int32_t loop_sound_rec = 0;
  while (true)
  {
    loop_sound_rec = loop_sound_rec + 1;
    //每25秒处理一次即可
    if (loop_sound_rec % 100 == 0)
      timerWrite(timer, 0); //reset timer (feed watchdog)

    //读满缓冲区8000字节
    //此函数会自动调节时间，只要后续的操作不要让缓冲区占满即可
    //1/4秒 8000字节 4000次
    I2S_Read(communicationData, numCommunicationData);

    for (int loop1 = 0; loop1 < numCommunicationData / 2 ; loop1++)
    {

      val1 = communicationData[loop1 * 2];
      val2 = communicationData[loop1 * 2 + 1] ;
      val16 = val1 + val2 *  256;
      if (val16 > 0)
      {
        val_avg = val_avg + val16;

      }

      //正负数都比较，靠谱
      val_max = max( val_max, abs(val16));

      //经验数据：乘以40 ：音量提升20db
      tmpval = val16 * volume_double.toInt();
      if (abs(tmpval) > 32767 )
      {
        if (val16 > 0)
          tmpval = 32767;
        else
          tmpval = -32767;
      }
      //Serial.println(String(val1) + " " + String(val2) + " " + String(val16) + " " + String(tmpval));
      communicationData[loop1 * 2] =  (byte)(tmpval & 0xFF);
      communicationData[loop1 * 2 + 1] = (byte)((tmpval >> 8) & 0xFF);

    }

    //填充声音信息到缓存区 (配置:2秒或n秒)
    //每个缓存环存满了换下一个缓存环
    cloudSpeechClient->pre_push_sound_buff((byte *)communicationData, numCommunicationData);


    //半秒检查一次  16000字节 8000次数据记录
    if (loop_sound_rec % 2 == 0 )
    {
      //乘以2,是正负对半原理
      val_avg = val_avg * 2 / numCommunicationData;

      //判断是否噪音指标： 峰值， 平均值
      if ( val_max > define_max1.toInt() && val_avg > define_avg1.toInt())
        aloud = true;
      else
        aloud = false;

      if (aloud)
      {
#ifdef SHOW_DEBUG

        timelong_str = ">>>>> high_max:" + String(val_max) +  " high_avg:" + String(val_avg) ;
        Serial.println(timelong_str);
#endif
        last_voice =  String(val_max) +  "|" + String(val_avg)  ;
        break;
      }
      val_avg = 0;
      val_max = 0;


      //防止溢出
      if (loop_sound_rec >= 100000)
        loop_sound_rec = 0;
    }
  }
  last_check = millis() ;
  return (true);
}

int record_sound()
{
  uint32_t all_starttime;
  uint32_t all_endtime;
  uint32_t timelong = 0;
  String timelong_str = "";
  uint32_t last_starttime = 0;

  float val_avg = 0;
  int16_t val_max = 0;
  //int32_t all_val_zero = 0;
  int16_t val16 = 0;
  uint8_t val1, val2;
  bool aloud = false;
  int32_t tmpval = 0;
  int all_alound;
  writenum = 0;

  //int loop1=0;
  //初始化0
  cloudSpeechClient->sound_bodybuff_p = 0;

  //用双声道，32位并没什么关系，因为拷数据时间很快！完全不占用多少时间！
  //Serial.println("record start 16k,16位,单声道");
  Serial.println( ">" + GetLocalTime() + " 开始录音 声音格式:16khz 16位 单声道， 静音检测，最长10秒结束...");

  //Serial.println(GetLocalTime() + "> " + "record... 反应时间:" + String(millis() - last_check) + "毫秒");

  //Serial.println("");
  // last_press = millis() / 1000;
  all_starttime = millis() / 1000;
  last_starttime = millis() / 1000;

  timerWrite(timer, 0); //reset timer (feed watchdog)


  //反复循环最长时间I2S录音
  for (uint32_t j = 0; j < waveDataSize / numCommunicationData; ++j) {
    //timelong_str = "";
    // Serial.println("loop");
    //读满缓冲区8000字节
    //此函数会自动调节时间，只要后续的操作不要让缓冲区占满即可
    I2S_Read(communicationData, numCommunicationData);

    //timelong_str = timelong_str + "," + j;
    //Serial.println(timelong_str);

    //平均值，最大值记录，检测静音参数用
    for (int loop1 = 0; loop1 < numCommunicationData / 2 ; loop1++)
    {
      val1 = communicationData[loop1 * 2];
      val2 = communicationData[loop1 * 2 + 1] ;
      val16 = val1 + val2 *  256;
      if (val16 > 0)
      {
        val_avg = val_avg + val16;
      }

      //用正负绝对值比较，更靠谱
      val_max = max( val_max, abs(val16));

      //乘以40 ：音量提升20db
      tmpval = val16 * volume_double.toInt();
      if (abs(tmpval) > 32767 )
      {
        if (val16 > 0)
          tmpval = 32767;
        else
          tmpval = -32767;
      }
      communicationData[loop1 * 2] =  (byte)(tmpval & 0xFF);
      communicationData[loop1 * 2 + 1] = (byte)((tmpval >> 8) & 0xFF);
    }

    //声音信息保存至缓冲区
    cloudSpeechClient->push_bodybuff_buff((byte*)communicationData, numCommunicationData);

    writenum = writenum + numCommunicationData;
    //半秒检查一次静音  16000字节 8000次数据记录
    if (j % 2 == 1)
    {
      val_avg = val_avg * 2 / numCommunicationData ;

      if ( val_max > define_max2.toInt() && val_avg > define_avg2.toInt()  )
        aloud = true;
      else
        aloud = false;

      if (aloud)
      {
        all_alound = all_alound + 1;
        //录音过程中，调试输出不要轻易用，会影响识别率！
        //timelong_str = ">>>>> " + String( millis() / 1000 - all_starttime) + String("秒 ");
        //timelong_str = timelong_str + " high_max:" + String(val_max) +  " high_avg:" + String(val_avg) ;
        //Serial.println(timelong_str);
        last_starttime = millis() / 1000;
      }

      val_avg = 0;
      val_max = 0;

    }

    //3秒仍静音，中断退出
    if ( millis() / 1000 - last_starttime > 3)
    {
#ifdef SHOW_DEBUG
      Serial.println("检测到连续3秒静音，退出录音");
#endif
      break;
    }
  }
  all_endtime = millis() / 1000;

#ifdef SHOW_DEBUG
  Serial.println("文件字节数:" + String(writenum) + ",理论秒数:" + String(writenum / 32000) + "秒") ;
  Serial.println("录音结束,时长:" + String(all_endtime - all_starttime) + "秒" );
#endif
  return (all_alound);
}

//如果flag 1 必须连接才算over,  如果为0 只试30秒
bool connectwifi(int flag)
{
  if (WiFi.status() == WL_CONNECTED) return true;

  while (true)
  {
    if (WiFi.status() == WL_CONNECTED) break;

    int trynum = 0;
    Serial.print("Connecting to ");

    Serial.println(wifi_ssid1);

    //静态IP有时会无法被访问，原因不明！
    WiFi.disconnect(true); //关闭网络
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);

    WiFi.begin(wifi_ssid1.c_str(), wifi_password1.c_str());

    while (WiFi.status() != WL_CONNECTED) {
      delay(2000);
      Serial.print(".");
      trynum = trynum + 1;
      //30秒 退出
      if (trynum > 14) break;
    }
    if (flag == 0) break;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("");
    Serial.println("WiFi connected with IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());
    Serial.println(WiFi.dnsIP(0));
    Serial.println(WiFi.dnsIP(1));
    return true;
  }
  else
    return false;
}



void begin_recordsound()
{
  //Serial.println("press 最长录音10秒，检测静音会提前结束" );
  //调用录音函数，直到结束
  int rec_ok = record_sound();
  String retstr = "";

  //录入声音都是静音
  if (skip_baidu == "1" && rec_ok == 0)
  {
#ifdef SHOW_DEBUG
    Serial.println("无用声音信号...");
#endif
    return;
  }
  else
  {
#ifdef SHOW_DEBUG
    Serial.println("进行文字识别" );
#endif
    uint32_t all_starttime = millis() / 1000;
    String VoiceText = cloudSpeechClient->getVoiceText();
    Serial.println("识别用时: " + String ( millis() / 1000 - all_starttime) + "秒" );

    int error = 0;

    if (VoiceText.indexOf("speech quality error") > -1)
    {
      Serial.println("find speech quality error");
      error = 1;
    }

    VoiceText.replace("speech quality error", "");
    VoiceText.replace("。", "");

    //如果识别到文字，对文字进行处理
    if (VoiceText.length() > 0)
    {
      record_succ(VoiceText);
    }
  }
}

//成功识别文字后的处理(VoiceText 不会为空)
void record_succ(String VoiceText)
{
  if (VoiceText.length() == 0) return;

  String retstr = "";
  //每个汉字占3个长度
  Serial.println(String("识别结果:") + GetLocalTime() + "> " + VoiceText + " len=" + VoiceText.length());

  //文字显示至墨水屏
  Show_hz(VoiceText, false);
  objmemo_historyManager->save_list();
  sleep_start_time = millis() / 1000;

}


void setupMode() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  delay(100);
  Serial.println("scanNetworks");

  ssidList1 = "";

  for (int i = 0; i < n; ++i) {
    ssidList1 += "<option value=\"";
    ssidList1 += WiFi.SSID(i);
    ssidList1 += "\"";

    if (WiFi.SSID(i) == wifi_ssid1)
      ssidList1 += " selected ";
    ssidList1 += ">";
    ssidList1 += WiFi.SSID(i);
    ssidList1 += "</option>";
  }
  delay(100);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID);
  WiFi.mode(WIFI_MODE_AP);
  startWebServer();
  Serial.println("Starting Access Point at \"" + String(apSSID) + "\"");
  Show_hz("http://192.168.4.1 设置参数", false);
}



String makePage(String title, String contents) {
  String s = "<!DOCTYPE html><html><head>";
  s += "<meta name=\"viewport\" content=\"width=device-width,user-scalable=0\">";
  s += "<title>";
  s += title;
  s += "</title></head><body>";
  s += contents;
  s += "</body></html>";
  return s;
}


String new_urlDecode(String input) {
  String s = input;
  s.replace("%20", " ");
  s.replace("+", " ");
  s.replace("%21", "!");
  s.replace("%22", "\"");
  s.replace("%23", "#");
  s.replace("%24", "$");
  s.replace("%25", "%");
  s.replace("%26", "&");
  s.replace("%27", "\'");
  s.replace("%28", "(");
  s.replace("%29", ")");
  s.replace("%30", "*");
  s.replace("%31", "+");
  s.replace("%2C", ",");
  s.replace("%2E", ".");
  s.replace("%2F", "/");
  s.replace("%2C", ",");
  s.replace("%3A", ":");
  s.replace("%3A", ";");
  s.replace("%3C", "<");
  s.replace("%3D", "=");
  s.replace("%3E", ">");
  s.replace("%3F", "?");
  s.replace("%40", "@");
  s.replace("%5B", "[");
  s.replace("%5C", "\\");
  s.replace("%5D", "]");
  s.replace("%5E", "^");
  s.replace("%5F", "-");
  s.replace("%60", "`");
  return s;
}


//配置模式在笔记本电脑win10上 192.168.4.1 ping不通，网页打不开
//但是使用手机可以连接此ip,可能是win10安全机制问题？
void startWebServer() {

  //设置模式
  //if (settingMode) {
  //if (true)
  //{
  Serial.print("Starting Web Server at ");
  if (settingMode)
    Serial.println(WiFi.softAPIP());
  else
    Serial.println(WiFi.localIP());
  //设置主页

  // readparams();
  webServer.on("/settings", []() {
    String s = "<h1>Wi-Fi Settings</h1><p>Please enter your password by selecting the SSID.</p>";
    s += "<form method=\"get\" action=\"setap\">" ;


    s += "<br>report_address: <input name=\"report_address\" style=\"width:350px\" value='" + report_address + "'type=\"text\">";
    s += "<br>report_url: <input name=\"report_url\" style=\"width:350px\"  value='" + report_url + "'type=\"text\">";

    s += "<br>baidu_key: <input name=\"baidu_key\" style=\"width:350px\"  value='" + baidu_key + "'type=\"text\">";
    s += "<br>baidu_secert: <input name=\"baidu_secert\" style=\"width:350px\"  value='" + baidu_secert + "'type=\"text\">";


    s += " <hr>";
    s += "<br>dog_delay: <input name=\"dog_delay\" style=\"width:100px\"  value='" + dog_delay + "'type=\"text\">mins";

    s += "<br>volume_low: <input name=\"volume_low\" style=\"width:100px\"  value='" + volume_low + "'type=\"text\">";
    s += "volume_high: <input name=\"volume_high\" style=\"width:100px\"  value='" + volume_high + "'type=\"text\">";
    s += "volume_double: <input name=\"volume_double\" style=\"width:100px\"  value='" + volume_double + "'type=\"text\">";

    s += "<br>define_max1: <input name=\"define_max1\" style=\"width:100px\"  value='" + define_max1 + "'type=\"text\">";
    s += "define_avg1: <input name=\"define_avg1\" style=\"width:100px\"  value='" + define_avg1 + "'type=\"text\">";
    s += "<br>define_max2: <input name=\"define_max2\" style=\"width:100px\" value='" + define_max2 + "'type=\"text\">";
    s += "define_avg2: <input name=\"define_avg2\" style=\"width:100px\" value='" + define_avg2 + "'type=\"text\">";

    s += "<br>pre_sound:<input name=\"pre_sound\" style=\"width:100px\" value='" + pre_sound + "'type=\"text\">";
    if (skip_baidu == "")
      s += "skip_baidu: <select name=\"skip_baidu\" ><option  value=\"1\" selected>yes</option> <option  value=\"0\">no</option>  </select>";
    else if (skip_baidu == "1")
      s += "skip_baidu: <select name=\"skip_baidu\" ><option  value=\"1\" selected>yes</option> <option  value=\"0\">no</option>  </select>";
    else
      s += "skip_baidu: <select name=\"skip_baidu\" ><option  value=\"1\">yes</option> <option  value=\"0\" selected>no</option>  </select>";
    s += "loopsleep:<input name=\"loopsleep\" style=\"width:100px\" value='" + loopsleep + "'type=\"text\">";

    s += " <hr>";
    s += "<label>SSID1: </label><select style=\"width:200px\"  name=\"wifi_ssid1\" >" + ssidList1 +  "</select>";
    s += "Password1: <input name=\"wifi_password1\" style=\"width:100px\"  value='" + wifi_password1 + "' type=\"text\">";

    s += "<hr>";



    s += "<br><input type=\"submit\"></form>";
    webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
  });
  //设置写入页(后台)
  webServer.on("/setap", []() {

    report_address = new_urlDecode(webServer.arg("report_address"));
    report_url = new_urlDecode(webServer.arg("report_url"));

    baidu_key = new_urlDecode(webServer.arg("baidu_key"));
    baidu_secert = new_urlDecode(webServer.arg("baidu_secert"));

    dog_delay = new_urlDecode(webServer.arg("dog_delay"));

    volume_low = new_urlDecode(webServer.arg("volume_low"));
    volume_high = new_urlDecode(webServer.arg("volume_high"));

    volume_double = new_urlDecode(webServer.arg("volume_double"));

    define_max1 = new_urlDecode(webServer.arg("define_max1"));
    define_avg1 = new_urlDecode(webServer.arg("define_avg1"));

    define_max2 = new_urlDecode(webServer.arg("define_max2"));
    define_avg2 = new_urlDecode(webServer.arg("define_avg2"));


    pre_sound = new_urlDecode(webServer.arg("pre_sound"));
    skip_baidu = new_urlDecode(webServer.arg("skip_baidu"));
    loopsleep = new_urlDecode(webServer.arg("loopsleep"));

    wifi_ssid1 = new_urlDecode(webServer.arg("wifi_ssid1"));
    wifi_password1 = new_urlDecode(webServer.arg("wifi_password1"));

    //   Serial.print("baidu_secert: " + baidu_secert);

    //写入配置
    writeparams();
    String wifi_ssid = "";
    String wifi_password = "";

    wifi_ssid = wifi_ssid1;
    wifi_password = wifi_password1;

    String s = "<h1>Setup complete.</h1><p>device will be connected to \"";
    s += wifi_ssid;
    s += "\" after the restart.";
    webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
    delay(3000);
    ESP.restart();
  });
  webServer.onNotFound([]() {
    String s = "<h1>AP mode</h1><p><a href=\"/settings\">Wi-Fi Settings</a></p>";
    webServer.send(200, "text/html", makePage("AP mode", s));
  });

  webServer.begin();
}


//GetCharwidth函数本来应放在 memo_historyManager类内部
//但因为引用了 msyh24海量字库变量，会造成编译失败,所以使用了一些技巧
//将函数当指针供类memo_historyManager 使用
//计算墨水屏显示的单个字的长度
int GetCharwidth(String ch)
{
  //修正，空格计算的的宽度为0, 强制36 字体不一样可能需要修改！
  if (ch == " ") return 28;

  char buf[50];
  int x1 = 0, y1 = 0, w = 0, h = 0;
  int tmp_cur_x = 0;
  int tmp_cur_y = 0;
  FontProperties properties;
  get_text_bounds((GFXfont *)&msyh36, (char *) ch.c_str(), &tmp_cur_x, &tmp_cur_y, &x1, &y1, &w, &h, &properties);
  //sprintf(buf, "x1=%d,y1=%d,w=%d,h=%d", x1, y1, w, h);
  //Serial.println("ch="+ ch + ","+ buf);

  //负数说明没找到这个字,会不显示出来
  if (w <= 0)
    w = 0;
  return (w);
}

//文字显示
void Show_hz(String rec_text, bool loadbutton)
{
  //最长限制160字节，40汉字
  //6个字串，最长约在 960字节，小于1024, json字串最大不超过1024
  rec_text = rec_text.substring(0, 160);
  //Serial.println("begin Showhz:" + rec_text + ", txt_list_index=" + String(txt_list_index));

  epd_poweron();
  //uint32_t t1 = millis();
  //全局刷
  epd_clear();
  //局刷,一样闪屏
  //epd_clear_area(screen_area);
  //epd_full_screen()

  //此句不要缺少，否则显示会乱码
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  //uint32_t t2 = millis();
  //printf("EPD clear took %dms.\n", t2 - t1);
  int cursor_x = 10;
  int cursor_y = 80;

  //多行文本换行显示算法。
  if (!loadbutton)
    objmemo_historyManager->multi_append_txt_list(rec_text);

  String now_string = "";
  int i;
  //int now_index = objmemo_historyManager->txt_list_index + 1;
  //当前行不要，所以最终会少一行数据
  //write_string 能根据手工加的 "\n"换行，但不能自由控制行距，此处我自行控制了.
  for ( i = 0; i < objmemo_historyManager->memolist.size() ; i++)
  {
    now_string = objmemo_historyManager->memolist.get(i);
    //Serial.println("Show_hz line:" + String((now_index + i) % TXT_LIST_NUM) + " " + now_string);

    if (now_string.length() > 0)
    {
      //加">"字符，规避epd47的bug,当所有字库不在字库时，esp32会异常重启
      // “Guru Meditation Error: Core 1 panic'ed (LoadProhibited). Exception was unhandled."
      now_string = ">" + now_string;
      //墨水屏writeln不支持自动换行
      //delay(200);
      //一定要用framebuffer参数，否则当最后一行数据过长时，会导致代码在此句阻塞，无法休眠，原因不明！

      writeln((GFXfont *)&msyh36, (char *)now_string.c_str(), &cursor_x, &cursor_y, framebuffer);
      //writeln调用后，cursor_x会改变，需要重新赋值
      cursor_x = 10;
      cursor_y = cursor_y + 85;
    }
  }

  //前面不要用writeln，有一定机率阻塞，无法休眠
  epd_draw_grayscale_image(epd_full_screen(), framebuffer);

  //delay(500);
  epd_poweroff();

  //Serial.println("end Showhz:" + rec_text + ", txt_list_index=" + String(txt_list_index));
}

//初始化硬件及变量
//如果非首次上电,且蓝牙没获取到新信息，则跳过此步，提升效率
void init_hard()
{
  epd_init();
  // framebuffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
  if (!framebuffer) {
    Serial.println("alloc memory failed !!!");
    delay(1000);
    while (1);
  }
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  //初始化SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS init failed");
    Serial.println("SPIFFS format ...");
    if (SPIFFS.format())
    {
      Serial.println("SPIFFS format ok");
      Serial.println("SPIFFS re_init");
      if (SPIFFS.begin(true))
      {
      }
      else
      {
        Serial.println("SPIFFS re_init error");
        ets_printf("reboot\n");
        esp_restart();
        return;
      }
    }
    else
    {
      Serial.println("SPIFFS format failed");
      ets_printf("reboot\n");
      esp_restart();
      return;
    }
  }
  Serial.println("SPIFFS init ok");
  //objmemo_historyManager->init_txt_list();
  objmemo_historyManager->load_list();
  Serial.println("load_list init ok");
}


void setup()
{
  Serial.begin(115200);
  Serial.println("setup begin");

  wakeup_reason = esp_sleep_get_wakeup_cause();
  //首次上电,进行初始化
  //启动原因不是深度休眠
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER)
  {
    Serial.println("sleep...");
    delay(100);
    //不调用此句无法节能
    epd_poweroff_all();

    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

    // ESP进入deepSleep状态
    //最大时间间隔为 4,294,967,295 µs 约合71分钟
    //休眠后，GPIP的高，低状态将失效，无法用GPIO控制开关
    //深度休眠才可以节能，电流0-1ma
    esp_deep_sleep_start();
    //轻度休眠 epd47下无法定时唤醒?
    //esp_light_sleep_start();
  }

  sleep_start_time = millis() / 1000;
  objmemo_historyManager = new memo_historyManager();
  objmemo_historyManager->GetCharwidth = GetCharwidth;

  init_hard();
  //清屏，对存储在SPIFFS内的记忆数据进行显示
  Show_hz("", true);

  Serial.println("epd_init init ok");


  //初始化配置类
  preferences.begin("wifi-config");
  readparams();

  //如果进入配置模式，10分钟后看门狗会让esp32自动重启
  int wdtTimeout = dog_delay.toInt() * 60 * 1000; //设置分钟 watchdog

  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000 , false); //set time in us
  timerAlarmEnable(timer);                          //enable interrupt

  //有限模式进入连接，如果30秒连接不上，返回false
  bool ret_bol = connectwifi(0);

  //ret_bol = false;
  //wifi连接不上，进入配置模式
  if (ret_bol == false)
  {
    settingMode = true;
    Show_hz("首次使用，进入设置", false);
    setupMode();
    return;
  }

  //I2S_BITS_PER_SAMPLE_8BIT 配置的话，下句会报错，
  //最小必须配置成I2S_BITS_PER_SAMPLE_16BIT
  I2S_Init(I2S_MODE_RX, 16000, I2S_BITS_PER_SAMPLE_16BIT);

  cloudSpeechClient = new CloudSpeechClient(pre_sound.toInt());
  //此方法必须调用成功，否则语音识别会无法进行
  while (true)
  {
    String baidu_Token = cloudSpeechClient->getToken(baidu_key, baidu_secert);
    Serial.println("get baidu_Token:" + baidu_Token);
    if (baidu_Token.length() > 0 )
      break;
    delay(30000);
  }

  //NTP 时间
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //String retstr = "";
  Serial.println("start...");
}

void loop() {
  //如果是配置模式，不录音，识音
  if (settingMode)
  {
    //处理网页服务（必须有)
    webServer.handleClient();
    return;
  }

  //检测到wifi失联，自动连接
  connectwifi(1);

  //检测是噪音（声音识别开始)
  wait_loud();
  //进入录音及识别模式
  begin_recordsound();
#ifdef SHOW_DEBUG
  Serial.println("loop sleep:" + String(loopsleep) + "秒");
#endif
  delay(loopsleep.toInt() * 1000);

  if (millis() / 1000 < sleep_start_time)
    sleep_start_time = millis() / 1000;

  //20分钟没有识别到文字，进入睡眠
  if ((millis() / 1000 - sleep_start_time) > 20 * 60 * 1000)
  {
    Serial.println("time out, sleep...");
    delay(100);
    //不调用此句无法节能
    epd_poweroff_all();


    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

    // ESP进入deepSleep状态
    //最大时间间隔为 4,294,967,295 µs 约合71分钟
    //休眠后，GPIP的高，低状态将失效，无法用GPIO控制开关
    //深度休眠才可以节能，电流0-1ma
    esp_deep_sleep_start();
    //轻度休眠 epd47下无法定时唤醒?
    //esp_light_sleep_start();
  }

}
