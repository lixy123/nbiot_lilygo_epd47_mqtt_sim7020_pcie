手机远程记事留言到nb-iot联网的墨水屏<br/>

<b>一.功能：</b><br/>
本系统设计的目标是随时随地用手机录入文字，远程传到联网的墨水屏进行记事留言.<br/>
<b>特点：</b><br/>
1.网络选用nb-iot，没用wifi。因为有些地方无wifi(例如车里) /场地更换重新配置wifi账号繁琐 /有些wifi验证复杂，单片机无法连接使用<br/>
2.墨水屏系统分成2套设备. <br/>
3.第1套设备: nbiot版本的MQTT转蓝牙透传器<br/>
4.第2套设备: 墨水屏<br/>
5.第1套设备通过nbiot技术连接到mqtt服务器,收到文字信息后蓝牙透传给墨水屏显示.<br/>
6.设计成2套设备是为了让墨水屏减少充电频率，使用这种设计，如果墨水屏记事留言频率不高的话，18650电池为墨水屏供电，1次充满电能使用2-6个月。MQTT转蓝牙透传器因为需要nbiot实时联网，不能休眠省电，用电相对较高。网上查到SIM7020可做到最大化优化省电，据传能做到用普通电池供电几年的能力，用于水表，科学测量仪上，我目前没掌握此技术,代码中没用到节能技术.<br/>

系统运行原理：<br/>
 <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/yuanli.JPG?raw=true' /> <br/>
  
<b>二.硬件:</b><br/>
<b>A.第1套设备：MQTT转蓝牙透传器</b><br/>
    >组成: <br/>
    esp32+sim7020<br/>
    >硬件资料:<br/>
    https://github.com/Xinyuan-LilyGO/LilyGo-T-PCIE<br/>
    >功能：<br/>
    通过NBIOT技术连接mqtt服务器，可随时待命接收MQTT客户端发来的的文字。当收到文字后，通过蓝牙将文字发给墨水屏<br/>
    >样图:<br/>
    <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/sim7020-2.jpg?raw=true' /> <br/>
    <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/sim7020-1.jpg?raw=true' /> <br/>
    
<b>B.第2套设备：墨水屏</b><br/>
    >组成:<br/>
    lilygo-epd47 墨水屏+hc08蓝牙硬件<br/>
    >硬件资料: <br/>
    https://github.com/Xinyuan-LilyGO/LilyGo-EPD47<br/>
    https://github.com/Xinyuan-LilyGO/EPD47-HC08<br/>
    >功能：<br/>
    平时深度休眠，收到第1套设备的蓝牙信号后唤醒，同时接收文字信息,刷屏显示文字，进入休眠<br/>
    >样图:<br/>
    <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/ink-1.jpg?raw=true' /> <br/>
     
    <b> hc08 </b>   <br/> 
     hc-08是一块 BLE4.0蓝牙模块 (购买时要告诉卖方要双晶振版本，否则不支持一级节能模式)<br/>
     hc-08需要配置成客户模式，一级节能模式,蓝牙名称用AT指令修改为edp47_ink，能防止被别的设备误连<br/>    
     引脚连接:<br/>
     lilygo-epd47 ==> hc-08<br/>
       VCC         VCC<br/>
       14          TX<br/>
       15          RX<br/>
       GND         GND<br/>

hc08 AT命令预处理:<br/>
AT+MODE=1 //设置成一级节能模式(必须)<br/>
AT+NAME=INK_047 //修改蓝牙名称，用于客户端查找蓝牙用<br/>
AT+LED=0 //关闭led灯，省电<br/>
注: 也可以通过连接到lilygo-epd47后,自编程序用lilygo-epd47虚拟串口传入AT命令<br/>

 注：可以一步到位，直接买lilygo 公司销售的专用于 lilygo-epd47 的hc-08模块，以上步骤可跳过。该模块将hc08模块套上了适合lilygo-epd47模块的外壳，集成度好, 已进行AT命令预处理.上手即用 <br/>
 如图, 蓝色小模块部分: <br/>
  <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_weather/blob/main/hc08.JPG?raw=true' /> <br/>
  
3.手机端可使用MQTT客户端软件IotMTQQPanel. 在此软件输入文字，直至文字显示到墨水屏，平均需要5-10秒左右.

 
<b>三.代码说明:</b> <br/>
  <b>1.nbiot_lilygo_epd47_mqtt_sim7020_pcie nbiot信息转蓝牙代码 </b>  <br/>
  烧录到1套设备的ESP32芯片上<br/> 
   1.1 软件: arduino 1.8.13<br/>
   1.2 用到的库文件:<br/>
   arduino-esp32 版本 1.0.6<br/>
   1.3开发板选择：ESP32 DEV Module <br/>
   1.4选择端口，点击烧录<br/>
   注：<br/>
nbiot_lilygo_epd47_mqtt_sim7020_pcie.ino 文件变量，共3处需要修改:<br/>
防止其它mqtt客户端用到同样名字互相干扰影响<br/>
另外，指向的MQTT服务器是网上免费的，很可能某天会失效，到时再换个免费或自建的mqtt服务器即可。<br/>
String mqtt_clientid = "client_you_7020";<br/>
String mqtt_topic = "/you_lily_mqtt";<br/>
String mqtt_topic_resp = "/you_lily_mqtt/resp";<br/>
    代码里面还有一些未描述的功能，例如语音TTS等，可查看代码了解。

<br/>
   <b>2.epd47_blue_waker_show_weather 墨水屏显示文字的代码 </b>   <br/>
   代码位置在 https://github.com/lixy123/nbiot_lilygo_epd47_weather/tree/main/epd47_blue_waker_show_weather<br/>
  烧录到LilyGo-EPD47墨水屏<br/>  
2.1 软件: arduino 1.8.13<br/>
2.2 用到的库文件:<br/>
https://github.com/espressif/arduino-esp32 版本:1.0.6<br/>
https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library 最新版本, 仅用到它的开发板定义<br/>
https://github.com/Xinyuan-LilyGO/LilyGo-EPD47 最新版本<br/>
https://github.com/bblanchon/ArduinoJson 版本: 6<br/>
https://github.com/ivanseidel/LinkedList 最新版本<br/>
2.3开发板选择：TTGO-T-WATCH / PSRAM ENABLED<br/>
2.4选择端口，点击烧录<br/>
注：代码内置显示天气，语音TTS的代码功能，是个增强的墨水屏套餐。用不着的代码可自行精简去掉.<br/>
  <br/>
<b>四.技术指标：</b><br/>
第1套设备电流约40ma,  普通18650电池2000mah, 估算支撑约2000/40/24约为2天。不建议电池供电<br/>
第2套设备在休眠时电流约0.2-0.8ma. 刷新显示文字是小概率情况，大部分时间是休眠状态，如果按0.5ma平均值估算，普通18650电池能待机 2000/0.5/24/30 约5个月，适合电池供电<br/>

