# LilyGo-EPD47-MIC-TXT
LilyGo-EPD47 麦克风识别并显示文字

<b>一.程序功能：</b> <br/>
     识别声音中的文字，文字显示至墨水屏<br/>
     每段录音最长10秒(可调长),平均一段录音的文字识别时间约2-5秒，取决于网络速度<br/>
<br/>
<b>二.硬件：</b> <br/>
   1.lilygo-epd47 墨水屏<br/>
       资料位置:  https://github.com/Xinyuan-LilyGO/LilyGo-EPD47<br/>
   2.MSM261S4030H0,INMP441 I2S麦克风模块均可用,约10-15块钱<br/>
         lilygo-epd47  I2S麦克风<br/>
             VCC3.3    VCC<br/>
             GND       GND<br/>
             15        WS<br/>
             12        DA<br/>
             13        CK<br/>
             GND       LR<br/>
<br/>
效果图<br/>
<img src= 'https://raw.githubusercontent.com/lixy123/LilyGo-EPD47-MIC-TXT/main/epd47-1.jpg' /> <br/>
<img src= 'https://raw.githubusercontent.com/lixy123/LilyGo-EPD47-MIC-TXT/main/epd47-2.jpg' /> <br/>
<br/>
<b>三.编译环境:</b><br/>
    1.Arduino 1.8.13<br/>
    2.引用地址配置成: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json<br/>
    3.安装： 安装esp32的官方开发包 esp32 by Espressif Systems 版本 1.05-rc6<br/>
    4.安装此处的开发板，因为字库文件导致编译的固件太大，仅仅为了用到开发板定义。 https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library<br/>
    5.开发板选择: TTGO T-WATCH, PSRAM选择Enabled<br/>
    6.选择ESP32连接的端口号，编译，上传烧写固件<br/>
    编译大小约 3.3M<br/>
<br/>
<b>四.使用说明：</b><br/>
  1.配置: ESP32首次运行时会自动初始化内置参数,自动进入路由器模式,创建一个ESP32SETUP的路由器，电脑连接此路由输入http:192.168.4.1进行配置<br/>
    A.esp32连接的路由器和密码<br/>
    B.百度语音的账号校验码<br/>
      baidu_key: 一个账号字串       (必须注册获得)<br/>
      baidu_secert: 一个账号校验码  (必须注册获得)<br/>
      这两个参数需要注册百度语音服务,在如下网址获取 https://ai.baidu.com/tech/speech     <br/>
      开通中文普通话短语音识别，单次语音最长识别60秒。新用户可免费用一段时间，再用必须开通收费，1000次3.4元左右价位，如果不限制使用，最多每天可调用5000-10000次，需要增加休眠功能或控制调用条件。<br/>
    C.其它音量监测参数: 默认是在家里安静环境下,如果周围较吵,需要将值调高<br/>
      define_max1 每0.5秒声音峰值（声音开始判断）<br/>
      define_avg1 每0.5秒声音均值（声音开始判断）<br/>
      define_max2 每0.5秒声音峰值（录音中静音判断）<br/>
      define_avg3 每0.5秒声音均值（录音中静音判断）<br/>
  2.运行：<br/>
    A.上电或按RESET按钮<br/>
    B.对着墨水屏说话<br/>
    C.墨水屏将语音识别出来的文字显示<br/>
    D.20分钟测试有识别到新文字自动进往休眠模式节能，重新唤醒按RESET按钮<br/>
<br/>
<b>五.软件代码原理:</b><br/>
  1.esp32上电后实时读取I2S声音信号，检测到周围声强是否达到指定音量，达到后立即进入录音模式<br/>
  2.如发现3秒内静音,录音停止，否则一直录音，直到10秒后停止录音，<br/>
  3.将i2s采集到的wav原始声音数据传给百度云语音转文字服务<br/>
  4.如果识别出文字，将文字显示至墨水屏<br/>
  声源在1-4米内识别效果都不错，再远了识别率会低.<br/>
<br/>
<b>六.其它技巧</b><br/>
  1.wav采集的数字声音有点像水波振动，以数字0不基线上下跳动. 静音时采集到的数值为0.<br/>
  2.程序会预存2秒的声音，这2秒不仅用于检测声强，也会用于文字识别。这样对于监听二个字的短语不会丢失声音数据.<br/>
  3.声音数据: 16khz 16位 wav数据，经测试，此格式下百度文字识别效果最合适  8khz 8位wav 格式识别效果很差<br/>
<br/>
<b>七.工作用电:</b><br/>
  5v 70-100ma电流
