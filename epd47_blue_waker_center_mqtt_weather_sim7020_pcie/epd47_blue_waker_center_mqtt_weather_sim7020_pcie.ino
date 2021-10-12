#include <HardwareSerial.h>
#include "soul_word.h"
#include "ble_to_hc08.h"

#include "weather_multiday_7020.h"

//本代码所用硬件为ESP32 + LilyGo-T-PCIE SIM7020
//参考代码：
//https://github.com/Xinyuan-LilyGO/LilyGo-T-PCIE/blob/master/examples/SIM7020/SIM7020.ino

//注意：
//虚拟串口：波特率用9600 ，否则天气数据传输会出现不稳定！！！
//波特率11500 一次只传输少量数据没问题，但容易丢数据！


//本版本解决了蓝牙发送重启的bug!
//bug:获取天气原始信息时会有乱码!

//编译文件大小 1.2M

#define PIN_TX                  27
#define PIN_RX                  26
#define POWER_PIN               25
#define PWR_PIN                 4

#define LED_PIN                 12
#define IND_PIN                 36


Manager_blue_to_hc08* objManager_blue_to_hc08;
HardwareSerial mySerial(1);

String weather_data_table = ""; //json格式的文本串天气,华丽表格天气显示用
String g_ink_showtxt = "";
Weather_multidayManager * objWeather_multidayManager;

int loop_num = 0;

bool net_connect_succ = false;
bool mqtt_connect_succ = false;
int mqtt_connect_error_num = 0;

String mqtt_server = "test.ranye-iot.net";
// 国内的，更稳定
String mqtt_clientid = "client_you_7020";
String mqtt_topic = "/you_lily_mqtt";
String mqtt_topic_resp = "/you_lily_mqtt/resp";

uint32_t boot_time = 0;       //12小时复位一次ESP32
uint32_t check_down_time = 0; //每小时AT命令检查一次，如果sim7020关机，启动它

String buff_split[20];

//#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
hw_timer_t *timer = NULL;

/*

  代码功能:
  1.开机自动连接MQTT
  2.当收到MQTT时，能进行相应的处理
  3.如果检测到MQTT服务器中断时，60秒后自动重新连接MQTT
  4.每1小时检查一次网络连接，如果中断，自动连接网络，重建MQTT
  5.每天自动重启esp32 2次
  6.收发的MQTT的数据是16进制，进行转换

  整体电流:
  40-50ma

  mosquitto_sub -v -t "/you_lily_mqtt" -h test.ranye-iot.net
  mosquitto_sub -v -t "/you_lily_mqtt/resp" -h test.ranye-iot.net
  mosquitto_pub -t  "/you_lily_mqtt"  -h test.ranye-iot.net -m "hi"

*/

void IRAM_ATTR resetModule() {
  ets_printf("resetModule reboot\n");
  delay(100);
  //esp_restart_noos(); 旧api
  esp_restart();
}


void rebootESP() {
  Serial.print("Rebooting ESP32: ");
  delay(100);
  //ESP.restart();  左边的方法重启后连接不上esp32
  esp_restart();
}

//sec秒内不接收串口数据，并清缓存
void clear_uart(int ms_time)
{
  //唤醒完成后就可以正常接收串口数据了
  uint32_t starttime = 0;
  char ch;
  //5秒内有输入则输出
  starttime = millis();
  //临时接收缓存，防止无限等待
  while (true)
  {
    if  (millis()  - starttime > ms_time)
      break;
    while (mySerial.available())
    {
      ch = (char) mySerial.read();
      Serial.print(ch);
    }
    yield();
    delay(20);
  }
}



//readStringUntil 有阻塞，不好用
String send_at2(String p_char, String break_str, String break_str2, int delay_sec) {

  String ret_str = "";
  String tmp_str = "";
  if (p_char.length() > 0)
  {
    Serial.println(String("cmd=") + p_char);
    mySerial.println(p_char);
  }

  //发完命令立即退出
  //if (break_str=="") return "";

  mySerial.setTimeout(1000);

  uint32_t start_time = millis() / 1000;
  while (millis() / 1000 - start_time < delay_sec)
  {
    if (mySerial.available() > 0)
    {
      //此句容易被阻塞
      tmp_str = mySerial.readStringUntil('\n');
      tmp_str.replace("\r", "");
      //tmp_str.trim()  ;
      Serial.println(">" + tmp_str);
      //如果字符中有特殊字符，用 ret_str=ret_str+tmp_str会出现古怪问题，最好用concat函数
      ret_str.concat(tmp_str);
      if (break_str.length() > 0 && tmp_str.indexOf(break_str) > -1 )
        break;
      if (break_str2.length() > 0 &&  tmp_str.indexOf(break_str2) > -1 )
        break;
    }
    delay(10);
  }
  return ret_str;
}


//readStringUntil 有阻塞，不好用
String send_at(String p_char, String break_str, int delay_sec) {

  String ret_str = "";
  String tmp_str = "";
  if (p_char.length() > 0)
  {
    Serial.println(String("cmd=") + p_char);
    mySerial.println(p_char);
  }

  //发完命令立即退出
  //if (break_str=="") return "";

  mySerial.setTimeout(1000);

  uint32_t start_time = millis() / 1000;
  while (millis() / 1000 - start_time < delay_sec)
  {
    if (mySerial.available() > 0)
    {
      //此句容易被阻塞
      tmp_str = mySerial.readStringUntil('\n');
      //tmp_str.replace("\r","");
      //tmp_str.trim()  ;
      Serial.println(">" + tmp_str);
      //如果字符中有特殊字符，用 ret_str=ret_str+tmp_str会出现古怪问题，最好用concat函数
      ret_str.concat(tmp_str);
      if (break_str.length() > 0 && tmp_str.indexOf(break_str) > -1)
        break;
    }
    delay(10);
  }
  return ret_str;
}


bool connect_mqtt()
{
  bool succ_flag = false;
  String ret;

  //假定上一次还在连接中，强制中断,否则下面均无法进行
  ret = send_at("AT+CMQDISCON=0", "OK", 5);
  Serial.println("ret=" + ret);
  delay(5000);

  int error_cnt = 0;
  while (true)
  {
    //正常情况会收到：+CMQNEW: 0/n OK/n
    ret = send_at("AT+CMQNEW=\"" + mqtt_server + "\",\"1883\", 12000,1024", "OK", 20);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+CMQNEW: 0") > -1)
      break;
    delay(5000);

    error_cnt++;
    if (error_cnt >= 5)
      return false;
  }
  Serial.println(">>> 创建TCP连接 ok ...");
  delay(2000);
  error_cnt = 0;
  while (true)
  {
    //正常情况只会收到ok
    ret = send_at("AT+CMQCON=0,3,\"" + mqtt_clientid + "\",600,1,0", "OK", 20);
    Serial.println("ret=" + ret);
    if (ret.indexOf("OK") > -1)
      break;
    delay(5000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> MQTT 连接 ok ...");

  delay(5000);
  error_cnt = 0;
  while (true)
  {
    ret = send_at("AT+CMQSUB=0,\"" + mqtt_topic + "\",1", "OK", 10);
    Serial.println("ret=" + ret);
    if (ret.indexOf("OK") > -1)
    {
      succ_flag = true;
      break;
    }
    delay(5000);
    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> 订阅主题 ok ...");

  //mqtt 发送上线信息
  delay(2000);
  error_cnt = 0;
  while (true)
  {
    String out = Strhex_convert("online");
    ret = send_at("AT+CMQPUB=0,\"" + mqtt_topic_resp + "\",1,0,0," + String(out.length()) + ",\"" + out + "\"", "", 5);
    Serial.println("ret=" + ret);
    if (ret.indexOf("OK") > -1)
      break;
    delay(5000);
    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println("mqtt 发送上线信息 ok ...");

  Serial.println("mqtt 连接服务器成功 ...");
  return succ_flag;
}

bool check_net()
{
  bool  ret_bool = false;
  String ret = "";
  //查询网络状态
  // ret = send_at("AT+CGCONTRDP", "\"cmnbiot\"", 1);
  ret = send_at2("AT+CGCONTRDP", "cmnbiot", "CMIOT", 1);
  //分配到IP
  if (ret.indexOf("cmnbiot") > -1 || ret.indexOf("CMIOT") > -1)
  {
    ret_bool = true;
  }

  return ret_bool;
}

//仅检查是否关机状态
bool check_waker_7020()
{
  String ret = "";
  delay(1000);
  int cnt = 0;
  bool check_ok = false;
  //通过AT命令检查是否关机，共检查3次
  while (true)
  {
    cnt++;
    ret = send_at("AT", "", 2);
    Serial.println("ret=" + ret);
    if (ret.length() > 0)
    {
      check_ok = true;
      break;
    }
    if (cnt >= 3) break;
    delay(1000);
  }
  return check_ok;
}

//重启7020
void reset_7020()
{
  net_connect_succ = false;
  mqtt_connect_succ = false;

  //断电5秒
  digitalWrite(POWER_PIN, LOW);
  delay(5000);

  digitalWrite(POWER_PIN, HIGH);
  delay(1000);
  // PWR_PIN ： This Pin is the PWR-KEY of the Modem
  // The time of active low level impulse of PWRKEY pin to power on module , type 500 ms
  digitalWrite(PWR_PIN, HIGH);
  delay(500);
  digitalWrite(PWR_PIN, LOW);
  clear_uart(30000);

  //at预处理
  check_waker_7020();
}


bool connect_nb()
{
  bool  ret_bool = false;

  int error_cnt = 0;
  String ret;


  error_cnt = 0;
  //网络信号质量查询，返回信号值
  while (true)
  {
    ret = send_at("AT+CPIN?", "+CPIN: READY", 1);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+CPIN: READY") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> SIM 卡状态 ok ...");


  error_cnt = 0;
  //查询网络注册状态
  while (true)
  {
    ret = send_at("AT+CGREG?", "+CGREG: 0,1", 1);
    Serial.println("ret=" + ret);

    if (ret.indexOf("+CGREG: 0,1") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> PS 业务附着 ok ...");
  error_cnt = 0;
  //查询PDP状态
  while (true)
  {
    ret = send_at("AT+CGACT?", "+CGACT: 1,1", 1);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+CGACT: 1,1") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> PDN 激活 OK ...");
  error_cnt = 0;
  //查询网络信息
  while (true)
  {
    ret = send_at("AT+COPS?", "+COPS: 0,2,\"46000\",9", 1);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+COPS: 0,2,\"46000\",9") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> 网络信息，运营商及网络制式 OK...");
  error_cnt = 0;
  while (true)
  {
    //查询网络状态
    //ret = send_at("AT+CGCONTRDP", "cmnbiot", 1);
    ret = send_at2("AT+CGCONTRDP", "cmnbiot", "CMIOT", 1);
    Serial.println("ret=" + ret);

    //分配到IP
    if (ret.indexOf("cmnbiot") > -1 || ret.indexOf("CMIOT") > -1)
    {
      ret_bool = true;
      break;
    }
    delay(2000);

    error_cnt++;
    if (error_cnt >= 5)
      return false;
  }

  ret = send_at("AT+CDNSGIP=www.baidu.com", "+CDNSGIP: 1,", 10);
  Serial.println("ret=" + ret);

  Serial.println(">>> 获取IP OK...");
  return ret_bool;
}
String Strhex_char(char ch) {

  return String(ch, HEX);;
}

String Strhex_convert(String data_str) {
  String tmpstr = "";

  for (int loop1 = 0; loop1 < data_str.length() ; loop1++)
  {
    tmpstr = tmpstr + Strhex_char(data_str[loop1]);
  }
  return tmpstr;
}

char hexStr_char(String data_str) {
  //int tmpint = data_str.toInt();
  //Serial.println("tmpint="+String(tmpint));
  //String(data[i], HEX);

  char ch;
  sscanf(data_str.c_str(), "%x", &ch);
  return ch;
}

String hexStr_convert(String data_str) {
  char ch;
  String  tmpstr = "";
  for (int loop1 = 0; loop1 < data_str.length() / 2; loop1++)
  {
    ch = hexStr_char(data_str.substring(loop1 * 2, loop1 * 2 + 2));
    //Serial.print("ch="+String(ch));
    tmpstr = tmpstr + ch;
  }
  return tmpstr;
}


void splitString(String message, String dot, String outmsg[], int len)
{
  int commaPosition, outindex = 0;
  for (int loop1 = 0; loop1 < len; loop1++)
    outmsg[loop1] = "";
  do {
    commaPosition = message.indexOf(dot);
    if (commaPosition != -1)
    {
      outmsg[outindex] = message.substring(0, commaPosition);
      outindex = outindex + 1;
      message = message.substring(commaPosition + 1, message.length());
    }
    if (outindex >= len) break;
  }
  while (commaPosition >= 0);

  if (outindex < len)
    outmsg[outindex] = message;
}



String parse_CHTTPNMIC(String in_str)
{
  //+CHTTPNMIC: 0,0,462,462,7b22726573756c7473223a5b7b226c6f636174696f6e223a7b226964223a2257583446425858464b453446222c226e616d65223a22e58c97e4baac222c22636f756e747279223a22434e222c2270617468223a22e58c97e4baac2ce58c97e4baac2ce4b8ade59bbd222c2274696d657a6f6e65223a22417369612f5368616e67686169222c2274696d657a6f6e655f6f6666736574223a222b30383a3030227d2c226461696c79223a5b7b2264617465223a22323032312d30332d3133222c22746578745f646179223a22e99cbe222c22636f64655f646179223a223331222c22746578745f6e69676874223a22e998b4222c22636f64655f6e69676874223a2239222c2268696768223a223136222c226c6f77223a2238222c227261696e66616c6c223a22302e30222c22707265636970223a22222c2277696e645f646972656374696f6e223a22e58d97222c2277696e645f646972656374696f6e5f646567726565223a22313830222c2277696e645f7370656564223a22382e34222c2277696e645f7363616c65223a2232222c2268756d6964697479223a223539227d5d2c226c6173745f757064617465223a22323032312d30332d31335430383a30303a30302b30383a3030227d5d7d

  String out_str = "";
  int cnt = 0;
  splitString(in_str, ",", buff_split, 5);
  //注意： 要乘2
  cnt = buff_split[3].toInt() * 2;
  out_str = buff_split[4].substring(0, cnt);
  //Serial.println("out_str1=" + out_str);
  out_str = hexStr_convert(out_str);
  //Serial.println("out_str2=" + out_str);
  return out_str;
}


String send_at_httpget(String p_char, int delay_sec) {
  String ret_str = "";
  String tmp_str = "";
  if (p_char.length() > 0)
  {
    Serial.println(String("cmd=") + p_char);
    mySerial.println(p_char);
  }
  ret_str = "";
  mySerial.setTimeout(5000);
  uint32_t start_time = millis() / 1000;
  while (millis() / 1000 - start_time < delay_sec)
  {

    if (mySerial.available() > 0)
    {
      tmp_str = mySerial.readStringUntil('\n');
      Serial.println(tmp_str);

      //结束标志，退出
      if (tmp_str.indexOf("+CHTTPERR: 0,-2") > -1) break;

      if (tmp_str.indexOf("+CHTTPNMIC: 0,0,") > -1)
      {
        //Serial.println("&");
        ret_str = ret_str + parse_CHTTPNMIC(tmp_str);
      }
      else if (tmp_str.indexOf("+CHTTPNMIC: 0,1,") > -1)
      {
        //Serial.println("&");
        ret_str = ret_str + parse_CHTTPNMIC(tmp_str);
      }

    }
    delay(10);
  }

  return ret_str;
}


//获得天气，表格版
bool get_weather_table()
{
  bool succ_flag = false;

  //如果setup时网络连接失败，重新再试
  if (net_connect_succ == false)
  {
    delay(1000);
    Serial.println(">>> 检查网络连接 ...");
    net_connect_succ = connect_nb();
  }

  //如果连接失败
  if (net_connect_succ == false)
    return false;

  String ret;

  ret = send_at("AT+CHTTPCREATE=\"" + objWeather_multidayManager->req_host + "\"", "+CHTTPCREATE: 0", 30);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("+CHTTPCREATE: 0") > -1))
    return false;

  Serial.println(">>> 创建HTTP Host ok ...");
  delay(200);

  ret = send_at("AT+CHTTPCON=0", "OK", 30);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("OK") > -1))
    return false;

  Serial.println(">>> 连接 http  ok ...");

  delay(200);
  //最长120秒内获得数据
  ret = send_at_httpget("AT+CHTTPSEND=0,0,\"" + objWeather_multidayManager->req_url + "\"", 120);
  //Serial.println("ret=" + ret);
  if  (ret.length() > 100)
  {
    succ_flag = true;
    weather_data_table = ret;

    Serial.println("weather_data_table=" + weather_data_table);
  }

  return succ_flag;
}


void free_http()
{
  String ret;
  ret = send_at("AT+CHTTPDISCON=0", "OK", 5);
  Serial.println("ret=" + ret);
  Serial.println(">>> 断开http连接  ok ...");

  ret = send_at("AT+CHTTPDESTROY=0", "OK", 5);
  Serial.println("ret=" + ret);


  Serial.println(">>> 释放 HTTP ok ...");
}

//获取天气，并发送信息给墨水屏
void send_weather_ink()
{
  timerWrite(timer, 0); //reset timer (feed watchdog)

  //sim7020复位
  // reset_7020();

  Serial.println("send_weather_ink begin...");

  //不能保证每次都能获取到天气数据！ 试3次？
  bool weatherok = get_weather_table();

  free_http();

  if (weatherok)
  {
    int http_code = objWeather_multidayManager->getnow_weather_7020(weather_data_table);
    if (http_code == 0)
    {
      g_ink_showtxt = objWeather_multidayManager->resp_new;

      if (g_ink_showtxt.length() > 0)
      {
        timerWrite(timer, 0); //reset timer (feed watchdog)
        //正常300字节内5秒发送完成
        //1KB字节数据发送需要更长时间，发送中间需要定时delay，约35-40秒左右
        bool ret_bool = objManager_blue_to_hc08->blue_connect_sendmsg(g_ink_showtxt, true);
        if (ret_bool)
          g_ink_showtxt = "";
      }
    }
  }
  Serial.println("send_weather_ink end...");

  //sim7020复位
  // reset_7020();
}

void setup() {
  Serial.begin(115200);
  //                               RX, TX
  mySerial.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);

  Serial.println("setup ...");

  //蓝牙连接时，很容易出现阻塞，所以增加看门狗处理.
  //为防意外，n秒后强制复位重启，一般用不到。。。
  //n秒如果任务处理不完，看门狗会让esp32自动重启,防止程序跑死...
  int wdtTimeout = 10 * 60 * 1000; //设置10分钟 watchdog

  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000 , false); //set time in us
  timerAlarmEnable(timer);                          //enable interrupt

  // POWER_PIN : This pin controls the power supply of the Modem
  pinMode(POWER_PIN, OUTPUT);

  digitalWrite(POWER_PIN, LOW);



  //打开蓝牙
  objManager_blue_to_hc08 = new Manager_blue_to_hc08();


  loop_num = 0;


  //前一步关闭nbiot至少5秒
  delay(5000);

  digitalWrite(POWER_PIN, HIGH);

  delay(1000);

  // PWR_PIN ： This Pin is the PWR-KEY of the Modem
  // The time of active low level impulse of PWRKEY pin to power on module , type 500 ms
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(500);
  digitalWrite(PWR_PIN, LOW);


  // Onboard LED light, it can be used freely
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); //关闭主板上的绿LED

  /*
    // IND_PIN: It is connected to the Modem status Pin,
    // through which you can know whether the module starts normally.
    pinMode(IND_PIN, INPUT);

    //如果sim7020与基站是连接的，活的，会闪灯
    attachInterrupt(IND_PIN, []() {
      detachInterrupt(IND_PIN);
      // If Modem starts normally, then set the onboard LED to flash once every 1 second
      tick.attach_ms(1000, []() {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      });
    }, CHANGE);
  */


  boot_time = millis() / 1000;
  check_down_time = millis() / 1000;

  Serial.println(">>> 开启 nb-iot ...");

  //等待sim7020上电，开机，确保网络连接上
  delay(30000); //此句不要省掉

  //at预处理
  check_waker_7020();

  Serial.println(">>> 检查网络连接 ...");
  net_connect_succ = false;
  mqtt_connect_succ = false;

  net_connect_succ = connect_nb();

  if (net_connect_succ)
  {
    Serial.println(">>> MQTT连接 ...");
    mqtt_connect_succ = connect_mqtt();
  }
  Serial.println("MQTT连接 end");

  mqtt_connect_error_num = 0;
  Serial.println("net_connect_succ:" + String(net_connect_succ) + ",mqtt_connect_succ:" + String(mqtt_connect_succ) );

  delay(1000);

  objWeather_multidayManager = new Weather_multidayManager;

  g_ink_showtxt = "";
}


void loop() {
  //英文资料说millis（) 在大约50天后清零，
  if ( millis() / 1000 < check_down_time )
    check_down_time = millis() / 1000;

  if ( millis() / 1000 < boot_time )
    boot_time = millis() / 1000;

  //每24小时自动重启
  if ( millis() / 1000 - boot_time > 24 * 3600)
    //if ( millis() / 1000 - boot_time > 1800)
  {
    Serial.println("24小时重启1次 ...");
    rebootESP();
  }


  //如果setup时网络连接失败，重新再试
  if (net_connect_succ == false)
  {
    delay(5000);
    mqtt_connect_succ = false;
    Serial.println(">>> 检查网络连接 ...");
    net_connect_succ = connect_nb();
    if (net_connect_succ)
    {
      Serial.println(">>> 连接 MQTT...");
      mqtt_connect_succ = connect_mqtt();
    }
    return;
  }


  //每6小时检查sim7020是否alive, 如果关闭则重启sim7020
  //调试用，实测没什么意义！
  //if ( millis() / 1000 - check_down_time > 3600)
  if ( millis() / 1000 - check_down_time > 6 * 3600)
  {
    if (check_waker_7020() == false)
    {
      Serial.println("AT 命令不响应，reset sim7020");
      reset_7020();

    }
    else
    {
      //调试效果用，实际意义有限
      String ret = send_at("AT+CDNSGIP=www.baidu.com", "+CDNSGIP: 1,", 10);
      Serial.println("ret=" + ret);
    }

    check_down_time = millis() / 1000 ;
    return ;
  }



  if (net_connect_succ )
  {
    //注意：mqtt接收信息慢，可能1-2分钟！需耐心！
    if ( mqtt_connect_succ)
    {
      if (mySerial.available() > 0)
      {
        String mqtt_receive = "";
        char ch;
        while (mySerial.available() > 0)
        {
          ch = mySerial.read();
          mqtt_receive = mqtt_receive + ch;
          delay(10);
        }
        Serial.println("get mqtt msg:" + mqtt_receive );


        if (mqtt_receive.length() == 0)
        {
          Serial.println("收到空串，跳过");
        }
        else if (mqtt_receive.indexOf("OK") > -1)
        {
          Serial.println("收到ok串，跳过");
        }
        //延迟收到 AT+CDNSGIP=www.baidu.com 的返回信息，忽略
        else if (mqtt_receive.indexOf("+CDNSGIP:") > -1)
        {
          Serial.println("收到+CDNSGIP,跳过");
        }
        //+CMQDISCON: 0 标志表示MQTT中断！且不会自动重连
        else if (mqtt_receive.indexOf("+CMQDISCON: 0") > -1)
        {
          Serial.println("收到mqtt中断信号，重新连接mqtt");
          Serial.println("disconnect mqtt 1");
          clear_uart(20000);
          mqtt_connect_succ = false;
        }
        else if (mqtt_receive.indexOf("+CMQPUB: 0,") > -1)
        {
          //AT+CMQPUB=0,"/7020_mqtt/resp",1,0,0,12,"6f6e6c696e65"
          //分解出接收的数据
          splitString(mqtt_receive, ",", buff_split, 7);
          buff_split[0].trim();
          if (buff_split[0] == "+CMQPUB: 0")
          {
            mqtt_receive = buff_split[6];
            mqtt_receive.trim();
            mqtt_receive = mqtt_receive.substring(1, mqtt_receive.length() - 1);
            Serial.println("get mqtt msg1:" + mqtt_receive );
            mqtt_receive = hexStr_convert(mqtt_receive);
            Serial.println("get mqtt msg2:" + mqtt_receive  );

            //如果有待发送数据，进行发送
            if (mqtt_receive.length() > 0)
            {
              if (mqtt_receive == "weather")
              {
                send_weather_ink();
              }
              else
              {
                String  g_ink_showtxt_out = "";
                g_ink_showtxt_out = mqtt_receive;
                if (mqtt_receive == "soul")
                {
                  int soul_index = random(ToxicSoulCount);
                  g_ink_showtxt_out = String(ToxicSoul[soul_index]);
                }
                else if (mqtt_receive == "sp:soul")
                {
                  int soul_index = random(ToxicSoulCount);
                  g_ink_showtxt_out = "sp:" + String(ToxicSoul[soul_index]);
                }

                //正常300字节内5秒发送完成

                bool ret_bool = objManager_blue_to_hc08->blue_connect_sendmsg(g_ink_showtxt_out, false);
                if (ret_bool)
                  Serial.println("blue_connect_sendmsg ok" );
              }
              //调试用
              //delay(10000);

            }
          }
          Serial.println("send mqtt resp" );
          //注：31323334, 代表1234
          String out = Strhex_convert("ok");
          //Serial.println("out=" + out);
          String ret = send_at("AT+CMQPUB=0,\"" + mqtt_topic_resp + "\",1,0,0," + String(out.length()) + ",\"" + out + "\"", "", 5);
          Serial.println("ret=" + ret);
        }
        //收到不是以上字串的数据，多半是网络中断
        else
        {
          Serial.println("收到mqtt之外的数据，重新连接mqtt");
          Serial.println("disconnect mqtt 2");
          //无条件断开mqtt
          clear_uart(20000);
          net_connect_succ = false;
          mqtt_connect_succ = false;
        }
        delay(2000);
      }
    }
    else
    {
      //每分钟尝试一次,连接mqtt
      Serial.println(">>> 60秒后重新连接 mqtt");
      delay(60000);
      Serial.println(">>> 连接 mqtt");

      String ret = send_at("AT+CDNSGIP=www.baidu.com", "+CDNSGIP: 1,", 10);
      Serial.println("ret=" + ret);

      mqtt_connect_succ = connect_mqtt();

      if (mqtt_connect_succ)
        mqtt_connect_error_num = 0;
      else
      {
        mqtt_connect_error_num = mqtt_connect_error_num + 1;
        Serial.println("mqtt_connect_error_num=" + String(mqtt_connect_error_num));
      }

      //连续2次MQTT连接失败，重启SIM7020,直至连接成功
      if (mqtt_connect_error_num >= 2)
      {
        Serial.println("sim7020 2次未成功连接MQTT reset sim7020...");
        reset_7020();
      }
      //连续5次MQTT连接失败，重启ESP32
      if (mqtt_connect_error_num >= 5)
      {
        Serial.println("sim7020 5次未成功连接MQTT reset esp32...");
        rebootESP();
      }
    }
  }
  delay(1000);
  //每30秒 feed dog 一次,
  //蓝牙连接时如果阻塞，让esp32有机会重启
  loop_num = loop_num + 1;
  if (loop_num > 30)
  {
    // Serial.println("feed watchdog...");
    loop_num = 0;
    timerWrite(timer, 0); //reset timer (feed watchdog)
  }
}
