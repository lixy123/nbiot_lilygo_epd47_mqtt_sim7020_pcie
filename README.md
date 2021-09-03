手机远程记事留言到nb-iot联网的墨水屏<br/>

本系统设计的目标是随时随地用手机录入文字，远程传到联网的墨水屏进行记事留言.<br/>
特点：<br/>
1.网络选用nb-iot，而不是wifi。因为有些地方无wifi(例如车里), 换场地后重新配置wifi账号繁琐，有些wifi要手机短信验证，用单片机连接不了。<br/>
2.墨水屏系统分成2套设备. <br/>
3.第1套设备: nbiot转发器，这一块因为要求实时联网，用电较大.如果仔细设计，应能进一步减少电流消耗.<br/>
4.第2套设备: 墨水屏显示，平时休眠，可以尽量少的减少电池充电频率.<br/>

<b>一.功能：</b><br/>
1.本系统由两套设备组成<br/>
2.第1套设备: esp32+sim7020<br/>
3.第2套设备: lilygo-epd47 墨水屏+hc08蓝牙硬件<br/>
4.设计成2套设备是为了让墨水屏减少充电频率，使用这种设计，如果墨水屏记事留言频率不高的话，电池充满电能支持2-6个月<br/>
5.技术原理:<br/>
A.第1套设备通过NBIOT技术连接mqtt服务器，可随时待命接收MQTT客户端的文字任务。当收到文字，sim7020将文字传给ESP32主控设备。主控设备用蓝牙将天气数据传给墨水屏<br/>
B.第2套设备平时休眠，可被第1套设备的蓝牙信号唤醒，同时接收文字信息显示，显示完毕立即进入休眠<br/>
<br/>
<b>技术指标：</b><br/>
第1套设备电流约40ma,  普通18650电池2000mah, 估算支撑约2000/40/24约为2天。不建议电池供电<br/>
第2套设备在休眠时电流约0.2-0.8ma. 刷新显示文字是小概率情况，大部分时间是休眠状态，如果按0.5ma平均值估算，普通18650电池能待机 2000/0.5/24/30 约5个月，适合电池供电<br/>


<b>二.硬件需求：</b><br/>
1.第一套设备 ESP32 +sim7020<br/>
  <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/sim7020-1.jpg?raw=true' /> <br/>
  <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/sim7020-2?raw=true' /> <br/>
  功能：mqtt连接云服务器. 收到手机(不限于手机)传过来的mqtt文字信号，文字信号通过蓝牙传给墨水屏显示<br/>
  硬件清单：<br/>
  A.esp32<br/>
  最好用带psram的esp32,且编译时打开param:enabled,否则json解析天气会不稳定<br/>
  
  
2.第2套设备 lilygo-epd47 + hc08<br/>
  <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_mqtt_sim7020_pcie/blob/main/ink-1.jpg?raw=true' /> <br/>
  功能：显示天气
  A.lilygo-epd47
     主控芯片为esp32，驱动墨水屏显示
     
  B.hc08     
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

 注：直接买lilygo 公司销售的专用于 lilygo-epd47 的hc-08模块又省事又简单。<br/>
 上面的hc-08的步骤都不用操作。此模块就是hc08模块套上了适合lilygo-epd47模块的外壳，引脚,集成度好, 已进行AT命令预处理.上手即用 <br/>
 如图, 蓝色小模块部分: <br/>
  <img src= 'https://github.com/lixy123/nbiot_lilygo_epd47_weather/blob/main/hc08.JPG?raw=true' /> <br/>
  
<b>三.代码说明:</b> <br/>
  <b>1.nbiot_lilygo_epd47_mqtt_sim7020_pcie nbiot信息转蓝牙代码 </b>  <br/>
  烧录到ESP32开发板<br/> 
   1.1 软件: arduino 1.8.13<br/>
   1.2 用到的库文件:<br/>
   arduino-esp32 版本 1.0.6<br/>
   1.3开发板选择：ESP32 DEV Module <br/>
   1.4选择端口，点击烧录<br/>
   注：<br/>
主项目文字的这3处需要修改，MQTT的特性是每个设备要用唯一号，如果其它人正好用到同样的名字，容易影响功能.<br/>
这个MQTT服务器是免费的，很可能哪天就失效了，到时再换个免费或自建的mqtt服务器即可。<br/>
String mqtt_clientid = "client_you_7020";<br/>
String mqtt_topic = "/you_lily_mqtt";<br/>
String mqtt_topic_resp = "/you_lily_mqtt/resp";<br/>
    代码里面有很多功能没描述到，例如语音TTS等，请看代码自己了解。

<br/>
   <b>2.epd47_blue_waker_show_weather 墨水屏支持蓝牙唤醒并显示文字的代码 </b>   <br/>
  烧录到LilyGo-EPD47墨水屏<br/>
2.1 软件: arduino 1.8.13<br/>
2.2 用到的库文件:<br/>
https://github.com/espressif/arduino-esp32 版本:1.0.6<br/>
https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library 最新版本, 仅为了用到它定义的开发板<br/>
https://github.com/Xinyuan-LilyGO/LilyGo-EPD47 最新版本<br/>
https://github.com/bblanchon/ArduinoJson 版本: 6<br/>
https://github.com/ivanseidel/LinkedList 最新版本<br/>
2.3开发板选择：TTGO-T-WATCH / PSRAM ENABLED<br/>
2.4选择端口，点击烧录<br/>
注：<br/>
电池供电，能自动休眠.<br/>
  
  
