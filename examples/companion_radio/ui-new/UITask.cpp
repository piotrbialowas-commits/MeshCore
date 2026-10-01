#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#include <RTClib.h>

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 15000
#endif

#define BOOT_SCREEN_MILLIS 5000
#define LONG_PRESS_MILLIS 1000
#define UI_NODE_LIST_SIZE 8

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS 20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS 4000
#endif

static int batteryPercent(uint16_t mv) {
  const int minMv = 3000;
  const int maxMv = 4200;
  int p = ((int)mv - minMv) * 100 / (maxMv - minMv);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

static void drawHeader(DisplayDriver& d, uint16_t battMv) {
  d.setTextSize(1);
  d.setColor(UIColor::primary_txt);
  d.setCursor(3, 16);
  d.print("PB/DS/PL");

  char buf[12];
  snprintf(buf, sizeof(buf), "%d%%", batteryPercent(battMv));
  d.drawTextRightAlign(159, 16, buf);

  d.drawRect(164, 4, 29, 13);
  d.fillRect(193, 8, 4, 5);
  int fill = (batteryPercent(battMv) * 25) / 100;
  if (fill > 0) d.fillRect(166, 6, fill, 9);
  d.drawRect(0, 23, 200, 1);
}

static void drawFooter(DisplayDriver& d, const char* left, const char* right) {
  d.setColor(UIColor::primary_txt);
  d.drawRect(0, 181, 200, 1);
  d.setTextSize(1);
  d.setCursor(3, 198);
  d.print(left);
  if (right && right[0]) d.drawTextRightAlign(197, 198, right);
}

static void ageText(uint32_t now, uint32_t then, char* out, size_t outSz) {
  if (then == 0 || now < then) {
    snprintf(out, outSz, "--");
    return;
  }
  uint32_t s = now - then;
  if (s < 60) snprintf(out, outSz, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(out, outSz, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, outSz, "%luh", (unsigned long)(s / 3600));
  else snprintf(out, outSz, "%lud", (unsigned long)(s / 86400));
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
public:
  SplashScreen(UITask* task) : _task(task), dismiss_after(millis() + BOOT_SCREEN_MILLIS) {}

  int render(DisplayDriver& d) override {
    drawHeader(d, _task->getBattMilliVolts());

    d.setColor(UIColor::primary_txt);
    d.drawRect(59, 43, 82, 55);
    d.setTextSize(3);
    d.drawTextCentered(100, 82, ":)");

    d.setTextSize(2);
    d.drawTextCentered(100, 122, "WITAJ!");

    d.setTextSize(1);
    d.drawTextCentered(100, 145, "Uruchamiam radio...");
    d.drawTextCentered(100, 163, "MeshCore 1.17.1");
    d.drawTextCentered(100, 178, "PB & ChatGPT v1.0");
    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) _task->gotoHomeScreen();
  }
};

class HomeScreen : public UIScreen {
public:
  enum Page : uint8_t {
    HOME = 0, MESSAGES, NODES, RADIO, GPS, BLUETOOTH, SETTINGS, INFO, COUNT
  };

private:
  enum Overlay : uint8_t {
    NONE = 0,
    NODE_DETAIL,
    RADIO_MENU,
    ADVERT_CONFIRM,
    ADVERT_SENT,
    TX_MENU,
    RADIO_PARAMS,
    RADIO_RESTORE_CONFIRM,
    GPS_MENU,
    BT_MENU,
    SETTINGS_MENU,
    DISPLAY_MENU,
    SOUND_MENU,
    POWER_MENU,
    BATTERY_MENU,
    ADVANCED_MENU,
    HW_TEST,
    MSG_MEMORY,
    DIAGNOSTICS,
    RESTART_CONFIRM,
    RESTART_SPLASH,
    FACTORY_RESET_CONFIRM,
    FACTORY_RESET_PROGRESS,
    HIBERNATE_SPLASH
  };

  enum PendingAction : uint8_t {
    ACTION_NONE = 0,
    ACTION_RESTART,
    ACTION_FACTORY_RESET,
    ACTION_HIBERNATE
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _prefs;
  Page _page = HOME;
  Overlay _overlay = NONE;
  PendingAction _pending = ACTION_NONE;
  uint8_t _sel = 0;
  uint8_t _nodeSel = 0;
  uint8_t _txSel = 0;
  uint8_t _batteryWarnPct = 20;
  int8_t _previousTx = 0;
  bool _frontlight = true;
  unsigned long _actionAt = 0;
  AdvertPath _recent[UI_NODE_LIST_SIZE];

  int recentCount() {
    memset(_recent, 0, sizeof(_recent));
    return the_mesh.getRecentlyHeard(_recent, UI_NODE_LIST_SIZE);
  }

  void title(DisplayDriver& d, const char* t) {
    d.setTextSize(2);
    d.setColor(UIColor::primary_txt);
    d.setCursor(6, 48);
    d.print(t);
  }

  void row(DisplayDriver& d, int y, const char* label, const char* value, bool selected=false) {
    d.setTextSize(1);
    if (selected) {
      d.setColor(UIColor::primary_txt);
      d.fillRect(6, y - 15, 188, 20);
      d.setColor(UIColor::window_bkg);
    } else {
      d.setColor(UIColor::primary_txt);
    }
    d.setCursor(11, y);
    d.print(label);
    if (value && value[0]) d.drawTextRightAlign(190, y, value);
  }

  void centeredChoice(DisplayDriver& d, const char* left, const char* right) {
    d.setTextSize(2);
    if (_sel == 0) {
      d.setColor(UIColor::primary_txt);
      d.fillRect(18, 129, 74, 34);
      d.setColor(UIColor::window_bkg);
      d.drawTextCentered(55, 153, left);
      d.setColor(UIColor::primary_txt);
      d.drawRect(108, 129, 74, 34);
      d.drawTextCentered(145, 153, right);
    } else {
      d.setColor(UIColor::primary_txt);
      d.drawRect(18, 129, 74, 34);
      d.drawTextCentered(55, 153, left);
      d.fillRect(108, 129, 74, 34);
      d.setColor(UIColor::window_bkg);
      d.drawTextCentered(145, 153, right);
    }
  }

  void renderHome(DisplayDriver& d) {
    char buf[64];
    uint32_t now = _rtc->getCurrentTime();
    DateTime dt(now);

    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "RADIO RSSI %.0f  SNR %.1f", radio_driver.getLastRSSI(), radio_driver.getLastSNR());
    d.setCursor(6, 43);
    d.print(buf);

    if (now > 100000) {
      d.setTextSize(3);
      snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour(), dt.minute());
      d.drawTextCentered(100, 93, buf);
      d.setTextSize(1);
      snprintf(buf, sizeof(buf), "%02d.%02d.%04d", dt.day(), dt.month(), dt.year());
      d.drawTextCentered(100, 116, buf);
    } else {
      d.setTextSize(3);
      d.drawTextCentered(100, 93, "--:--");
      d.setTextSize(1);
      d.drawTextCentered(100, 116, "brak synchronizacji");
    }

    d.drawRect(5, 125, 190, 1);
    int nodes = recentCount();
    d.setTextSize(1);
    snprintf(buf, sizeof(buf), "WIAD.: %d", _task->getMsgCount());
    d.setCursor(8, 146);
    d.print(buf);
    snprintf(buf, sizeof(buf), "NODY: %d", nodes);
    d.drawTextRightAlign(192, 146, buf);

    const char* ble = _task->isBluetoothEnabled() ? (_task->hasConnection() ? "BLE: POL." : "BLE: WL.") : "BLE: WYL.";
    const char* gps = _task->getGPSState() ? "GPS: WL." : "GPS: WYL.";
    d.setCursor(8, 169);
    d.print(ble);
    d.drawTextRightAlign(192, 169, gps);
    drawFooter(d, "v DALEJ", "");
  }

  void renderMessages(DisplayDriver& d) {
    title(d, "WIADOMOŚCI");
    char buf[48];
    snprintf(buf, sizeof(buf), "Nieprzeczytane: %d", _task->getMsgCount());
    row(d, 79, "Status", buf);
    snprintf(buf, sizeof(buf), "%d / %d", the_mesh.getOfflineQueueLen(), OFFLINE_QUEUE_SIZE);
    row(d, 105, "Kolejka aplikacji", buf);
    if (_task->getMsgCount() > 0) {
      d.setTextSize(2);
      d.drawTextCentered(100, 145, "NOWA WIADOMOŚĆ");
    } else {
      d.setTextSize(1);
      d.drawTextCentered(100, 145, "Brak nowych wiadomości");
    }
    drawFooter(d, "v DALEJ", "o OTWORZ");
  }

  void renderNodes(DisplayDriver& d) {
    title(d, "NODY");
    int count = recentCount();
    uint32_t now = _rtc->getCurrentTime();
    if (count <= 0) {
      d.setTextSize(1);
      d.drawTextCentered(100, 110, "Brak słyszanych nodów");
    } else {
      int y = 72;
      for (int i = 0; i < count && i < 5; ++i, y += 23) {
        char age[12];
        ageText(now, _recent[i].recv_timestamp, age, sizeof(age));
        row(d, y, _recent[i].name, age, i == 0);
      }
    }
    drawFooter(d, "v DALEJ", "o SZCZEGOLY");
  }

  void renderNodeDetail(DisplayDriver& d) {
    int count = recentCount();
    if (count <= 0) { _overlay = NONE; return; }
    if (_nodeSel >= count) _nodeSel = 0;
    AdvertPath& n = _recent[_nodeSel];
    title(d, n.name);
    char buf[64], age[16];
    ageText(_rtc->getCurrentTime(), n.recv_timestamp, age, sizeof(age));
    row(d, 80, "Ostatnio słyszany", age);
    snprintf(buf, sizeof(buf), "%u", n.path_len == 0xFF ? 0 : n.path_len);
    row(d, 104, "Długość ścieżki", buf);
    row(d, 128, "RSSI", "brak danych");
    row(d, 152, "GPS", "brak danych");
    drawFooter(d, "v NAST.", "o WSTECZ");
  }

  void renderRadio(DisplayDriver& d) {
    title(d, "RADIO / SIEC");
    char buf[48];
    snprintf(buf, sizeof(buf), "%.3f MHz", _prefs->freq); row(d, 72, "FQ", buf);
    snprintf(buf, sizeof(buf), "%u", _prefs->sf); row(d, 93, "SF", buf);
    snprintf(buf, sizeof(buf), "%.2f kHz", _prefs->bw); row(d, 114, "BW", buf);
    snprintf(buf, sizeof(buf), "4/%u", _prefs->cr); row(d, 135, "CR", buf);
    snprintf(buf, sizeof(buf), "%d dBm", _prefs->tx_power_dbm); row(d, 156, "TX", buf);
    snprintf(buf, sizeof(buf), "%d dBm", radio_driver.getNoiseFloor()); row(d, 177, "Noise", buf);
    drawFooter(d, "v DALEJ", "o OPCJE");
  }

  void renderRadioMenu(DisplayDriver& d) {
    title(d, "RADIO / OPCJE");
    const char* items[] = {"WYŚLIJ ADVERT", "MOC TX", "PARAMETRY RADIA", "PRZYWRÓĆ OSTATNIE", "WSTECZ"};
    int y = 67;
    for (int i=0;i<5;i++,y+=23) row(d, y, items[i], ">", i == _sel);
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderAdvertConfirm(DisplayDriver& d) {
    title(d, "WYŚLIJ ADVERT");
    d.setTextSize(2);
    d.drawTextCentered(100, 92, "Wysłać komunikat");
    d.drawTextCentered(100, 116, "ADVERT w sieci?");
    centeredChoice(d, "TAK", "NIE");
    drawFooter(d, "v ZMIEN", "o POTWIERDZ");
  }

  void renderAdvertSent(DisplayDriver& d) {
    title(d, "WYŚLIJ ADVERT");
    d.setTextSize(2);
    d.drawTextCentered(100, 104, "ADVERT WYSŁANY!");
    d.setTextSize(1);
    d.drawTextCentered(100, 135, "Urządzenie ogłoszone");
    d.drawTextCentered(100, 155, "w sieci.");
    drawFooter(d, "", "o OK");
  }

  void renderTxMenu(DisplayDriver& d) {
    title(d, "MOC TX");
    static const int vals[] = {10,14,17,20,22};
    int y = 68;
    for (int i=0;i<5;i++,y+=22) {
      char b[16];
      snprintf(b, sizeof(b), "%d dBm", vals[i]);
      row(d, y, b, vals[i] == _prefs->tx_power_dbm ? "AKT." : "", i == _txSel);
    }
    drawFooter(d, "v ZMIEN", "o ZAPISZ");
  }

  void renderRadioParams(DisplayDriver& d) {
    title(d, "PARAMETRY RADIA");
    char buf[40];
    snprintf(buf, sizeof(buf), "%.3f MHz", _prefs->freq); row(d, 76, "FQ", buf);
    snprintf(buf, sizeof(buf), "SF%u", _prefs->sf); row(d, 99, "SF", buf);
    snprintf(buf, sizeof(buf), "%.2f kHz", _prefs->bw); row(d, 122, "BW", buf);
    snprintf(buf, sizeof(buf), "4/%u", _prefs->cr); row(d, 145, "CR", buf);
    snprintf(buf, sizeof(buf), "%d dBm", radio_driver.getNoiseFloor()); row(d, 168, "Noise", buf);
    drawFooter(d, "v WSTECZ", "o WSTECZ");
  }

  void renderRadioRestore(DisplayDriver& d) {
    title(d, "PRZYWRÓĆ OSTATNIE");
    d.setTextSize(1);
    d.drawTextCentered(100, 87, "Przywrócić poprzednią");
    d.drawTextCentered(100, 108, "moc nadajnika?");
    char buf[32];
    snprintf(buf, sizeof(buf), "TX: %d dBm", _previousTx);
    d.drawTextCentered(100, 126, buf);
    centeredChoice(d, "TAK", "NIE");
    drawFooter(d, "v ZMIEN", "o POTWIERDZ");
  }

  void renderGPS(DisplayDriver& d) {
    title(d, "GPS / POZYCJA");
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : NULL;
    bool on = _task->getGPSState();
    row(d, 75, "Stan", on ? "AKTYWNY" : "WYŁĄCZONY");
    if (!on || loc == NULL) {
      row(d, 98, "Satelity", "--");
      row(d, 121, "Lat", "BRAK DANYCH");
      row(d, 144, "Lon", "BRAK DANYCH");
      row(d, 167, "Wys.", "BRAK DANYCH");
    } else {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d", loc->satellitesCount()); row(d, 98, "Satelity", buf);
      if (loc->isValid()) {
        snprintf(buf, sizeof(buf), "%.5f", loc->getLatitude()/1000000.0); row(d, 121, "Lat", buf);
        snprintf(buf, sizeof(buf), "%.5f", loc->getLongitude()/1000000.0); row(d, 144, "Lon", buf);
        snprintf(buf, sizeof(buf), "%.0f m", loc->getAltitude()/1000.0); row(d, 167, "Wys.", buf);
      } else {
        row(d, 121, "Fix", "BRAK FIXA");
        row(d, 144, "Lat/Lon", "BRAK DANYCH");
      }
    }
    drawFooter(d, "v DALEJ", "o OPCJE");
  }

  void renderGPSMenu(DisplayDriver& d) {
    title(d, "GPS / OPCJE");
    const char* labels[] = {"GPS WL./WYL.", "ODŚWIEŻ POZYCJĘ", "WSTECZ"};
    int y = 80;
    for (int i=0;i<3;i++,y+=30) {
      const char* val = (i==0) ? (_task->getGPSState() ? "WŁ." : "WYŁ.") : ">";
      row(d, y, labels[i], val, i == _sel);
    }
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderBluetooth(DisplayDriver& d) {
    title(d, "BLUETOOTH");
    d.setTextSize(2);
    if (!_task->isBluetoothEnabled()) {
      d.drawTextCentered(100, 105, "WYŁĄCZONY");
    } else if (_task->hasConnection()) {
      d.drawTextCentered(100, 105, "POŁĄCZONY");
    } else {
      d.drawTextCentered(100, 96, "NIEPOŁĄCZONY");
      d.setTextSize(1);
      char buf[32];
      snprintf(buf, sizeof(buf), "PIN: %06lu", (unsigned long)the_mesh.getBLEPin());
      d.drawTextCentered(100, 128, buf);
      d.drawTextCentered(100, 151, "Gotowy do parowania");
    }
    drawFooter(d, "v DALEJ", "o OPCJE");
  }

  void renderBTMenu(DisplayDriver& d) {
    title(d, "BLUETOOTH / OPCJE");
    const char* labels[] = {"BLE WL./WYL.", "ROZLACZ", "WSTECZ"};
    int y = 80;
    for (int i=0;i<3;i++,y+=30) {
      const char* val = (i==0) ? (_task->isBluetoothEnabled() ? "WŁ." : "WYŁ.") : ">";
      row(d, y, labels[i], val, i == _sel);
    }
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderSettings(DisplayDriver& d) {
    title(d, "USTAWIENIA");
    row(d, 79, "EKRAN / PODŚW.", ">");
    row(d, 104, "DŹWIĘK", _task->isBuzzerQuiet() ? "WYŁ." : "WŁ.");
    row(d, 129, "ZASILANIE", ">");
    row(d, 154, "ZAAWANSOWANE", ">");
    drawFooter(d, "v DALEJ", "o OPCJE");
  }

  void renderSettingsMenu(DisplayDriver& d) {
    title(d, "USTAWIENIA");
    const char* labels[] = {"EKRAN / PODŚW.", "DŹWIĘK", "ZASILANIE", "ZAAWANSOWANE", "WSTECZ"};
    int y = 70;
    for (int i=0;i<5;i++,y+=24) row(d, y, labels[i], ">", i == _sel);
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderDisplayMenu(DisplayDriver& d) {
    title(d, "EKRAN / PODŚW.");
    char autoOff[16]; snprintf(autoOff, sizeof(autoOff), "%d s", AUTO_OFF_MILLIS/1000);
    const char* labels[] = {"FRONTLIGHT", "AUTO-WYGASZANIE", "PODŚW. PRZY WIAD.", "POBUDKA KLAWISZEM", "TRYB ODŚWIEŻANIA", "WSTECZ"};
    const char* vals[] = {_frontlight ? "WŁ." : "WYŁ.", autoOff, "WŁ.", "WŁ.", "NORMALNY", ">"};
    int y = 62;
    for (int i=0;i<6;i++,y+=21) row(d, y, labels[i], vals[i], i == _sel);
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderSoundMenu(DisplayDriver& d) {
    title(d, "DŹWIĘK");
    const char* labels[] = {"DŹWIĘK", "NOWA WIAD.", "PRYWATNE", "KANALOWE", "PRZYCISKI", "WSTECZ"};
    const char* vals[] = {_task->isBuzzerQuiet() ? "WYŁ." : "WŁ.", "WŁ.", "OSOBNY", "KROTKI", "WYŁ.", ">"};
    int y = 62;
    for (int i=0;i<6;i++,y+=21) row(d, y, labels[i], vals[i], i == _sel);
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderPowerMenu(DisplayDriver& d) {
    title(d, "ZASILANIE");
    char pct[16], volts[16], warn[16];
    uint16_t mv = _task->getBattMilliVolts();
    snprintf(pct, sizeof(pct), "%d%%", batteryPercent(mv));
    snprintf(volts, sizeof(volts), "%.2f V", mv/1000.0);
    snprintf(warn, sizeof(warn), "%u%%", _batteryWarnPct);
    row(d, 72, "Stan baterii", pct);
    row(d, 96, "Napięcie", volts);
    row(d, 120, "OSTRZEŻENIE BATERII", warn, _sel==0);
    row(d, 144, "HIBERNACJA", ">", _sel==1);
    row(d, 168, "WSTECZ", ">", _sel==2);
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderBatteryMenu(DisplayDriver& d) {
    title(d, "OSTRZEŻENIE BATERII");
    const int vals[] = {10,15,20,25};
    int y=75;
    for(int i=0;i<4;i++,y+=24){
      char v[16], desc[20];
      snprintf(v,sizeof(v),"%d%%",vals[i]);
      snprintf(desc,sizeof(desc), vals[i]==10 ? "krytyczne" : vals[i]==15 ? "niski" : vals[i]==20 ? "zalecane" : "wczesniejsze");
      row(d,y,v,desc,_sel==i);
    }
    row(d,171,"WSTECZ",">",_sel==4);
    drawFooter(d, "v ZMIEN", "o ZAPISZ");
  }

  void renderAdvancedMenu(DisplayDriver& d) {
    title(d, "ZAAWANSOWANE");
    const char* labels[] = {"TEST SPRZĘTU", "WIADOMOŚCI / PAMIĘĆ", "DIAGNOSTYKA", "RESTART", "USTAWIENIA FABR.", "WSTECZ"};
    int y=62;
    for(int i=0;i<6;i++,y+=21) row(d,y,labels[i],">",i==_sel);
    drawFooter(d, "v DALEJ", "o WYBIERZ");
  }

  void renderHwTest(DisplayDriver& d) {
    title(d, "TEST SPRZĘTU");
    char batt[20]; snprintf(batt,sizeof(batt),"%.2f V",_task->getBattMilliVolts()/1000.0);
    row(d,65,"e-Ink","OK");
    row(d,85,"FRONTLIGHT",_frontlight ? "OK" : "WYŁ.");
    row(d,105,"BUZZER",_task->isBuzzerQuiet() ? "WYŁ." : "OK");
    row(d,125,"LoRa (SX1262)","OK");
    row(d,145,"GPS",_task->getGPSState() ? "WŁ." : "WYŁ.");
    row(d,165,"BLE",_task->isBluetoothEnabled() ? "OK" : "WYŁ.");
    row(d,180,"BATERIA",batt);
    drawFooter(d, "v WSTECZ", "o PONOWNIE");
  }

  void renderMsgMemory(DisplayDriver& d) {
    title(d, "WIADOMOŚCI / PAMIĘĆ");
    char buf[32];
    snprintf(buf,sizeof(buf),"%d",_task->getMsgCount()); row(d,80,"Nieprzeczytane",buf);
    snprintf(buf,sizeof(buf),"%d/%d",the_mesh.getOfflineQueueLen(),OFFLINE_QUEUE_SIZE); row(d,110,"Kolejka aplikacji",buf);
    snprintf(buf,sizeof(buf),"%d",recentCount()); row(d,140,"Ostatnie nody",buf);
    row(d,165,"Historia","zarządza aplikacja");
    drawFooter(d, "v WSTECZ", "o OK");
  }

  void renderDiagnostics(DisplayDriver& d) {
    title(d, "DIAGNOSTYKA");
    char buf[32];
    snprintf(buf,sizeof(buf),"%lus",(unsigned long)(millis()/1000)); row(d,61,"Uptime",buf);
    snprintf(buf,sizeof(buf),"%d",(int)board.getStartupReason()); row(d,81,"Reset reason",buf);
    snprintf(buf,sizeof(buf),"%lu",(unsigned long)radio_driver.getPacketsRecv()); row(d,101,"RX",buf);
    snprintf(buf,sizeof(buf),"%lu",(unsigned long)radio_driver.getPacketsSent()); row(d,121,"TX",buf);
    snprintf(buf,sizeof(buf),"%lu",(unsigned long)radio_driver.getPacketsRecvErrors()); row(d,141,"Błędy",buf);
    row(d,161,"MeshCore",FIRMWARE_VERSION);
    row(d,179,"PB & ChatGPT","v1.0");
    drawFooter(d, "v WSTECZ", "o ODSWIEZ");
  }

  void renderRestartConfirm(DisplayDriver& d) {
    title(d, "RESTART");
    d.setTextSize(2);
    d.drawTextCentered(100,92,"Uruchomić urządzenie");
    d.drawTextCentered(100,116,"ponownie?");
    centeredChoice(d,"TAK","NIE");
    drawFooter(d, "v ZMIEN", "o POTWIERDZ");
  }

  void renderRestartSplash(DisplayDriver& d) {
    d.setTextSize(3);
    d.drawTextCentered(100,78,":)");
    d.setTextSize(2);
    d.drawTextCentered(100,118,"RESTART");
    d.setTextSize(1);
    d.drawTextCentered(100,148,"Uruchamiam ponownie...");
  }

  void renderFactoryResetConfirm(DisplayDriver& d) {
    title(d, "USTAWIENIA FABR.");
    d.setTextSize(1);
    d.drawTextCentered(100,78,"Usunie konfigurację radia,");
    d.drawTextCentered(100,98,"tożsamość, kontakty,");
    d.drawTextCentered(100,118,"kanały i historię.");
    centeredChoice(d,"TAK","NIE");
    drawFooter(d, "v ZMIEN", "o POTWIERDZ");
  }

  void renderFactoryResetProgress(DisplayDriver& d) {
    d.setTextSize(2);
    d.drawTextCentered(100,95,"RESET FABRYCZNY");
    d.setTextSize(1);
    d.drawTextCentered(100,130,"Czyszczenie pamięci...");
  }

  void renderHibernateSplash(DisplayDriver& d) {
    d.setTextSize(3);
    d.drawTextCentered(100,80,"Zzz");
    d.setTextSize(2);
    d.drawTextCentered(100,120,"HIBERNACJA");
    d.setTextSize(1);
    d.drawTextCentered(100,150,"Radio śpi...");
  }

  void renderInfo(DisplayDriver& d) {
    title(d, "O SYSTEMIE");
    row(d, 72, "MeshCore", FIRMWARE_VERSION);
    row(d, 93, "Interfejs", "PB & ChatGPT v1.0");
    row(d, 114, "Urządzenie", "ThinkNode M1");
    row(d, 135, "MCU", "nRF52840");
    row(d, 156, "Radio", "SX1262");
    row(d, 177, "Wyświetlacz", "e-Ink 200x200");
    drawFooter(d, "v DALEJ", "");
  }

  void scheduleAction(PendingAction p, Overlay screen) {
    _pending = p;
    _overlay = screen;
    _actionAt = millis() + 1400;
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _sensors(sensors), _prefs(prefs) {
      _previousTx = prefs->tx_power_dbm;
    }

  void setPage(uint8_t p) {
    _overlay = NONE;
    _pending = ACTION_NONE;
    _page = (Page)(p % COUNT);
  }

  void poll() override {
    if (_pending == ACTION_NONE || millis() < _actionAt) return;
    PendingAction p = _pending;
    _pending = ACTION_NONE;
    if (p == ACTION_RESTART) {
      _task->shutdown(true);
    } else if (p == ACTION_HIBERNATE) {
      _task->shutdown(false);
    } else if (p == ACTION_FACTORY_RESET) {
      if (the_mesh.factoryResetStorageFromUI()) {
        _task->shutdown(true);
      } else {
        _overlay = ADVANCED_MENU;
        _task->showAlert("BŁĄD RESETU", 1200);
      }
    }
  }

  int render(DisplayDriver& d) override {
    drawHeader(d, _task->getBattMilliVolts());
    switch (_overlay) {
      case NODE_DETAIL: renderNodeDetail(d); break;
      case RADIO_MENU: renderRadioMenu(d); break;
      case ADVERT_CONFIRM: renderAdvertConfirm(d); break;
      case ADVERT_SENT: renderAdvertSent(d); break;
      case TX_MENU: renderTxMenu(d); break;
      case RADIO_PARAMS: renderRadioParams(d); break;
      case RADIO_RESTORE_CONFIRM: renderRadioRestore(d); break;
      case GPS_MENU: renderGPSMenu(d); break;
      case BT_MENU: renderBTMenu(d); break;
      case SETTINGS_MENU: renderSettingsMenu(d); break;
      case DISPLAY_MENU: renderDisplayMenu(d); break;
      case SOUND_MENU: renderSoundMenu(d); break;
      case POWER_MENU: renderPowerMenu(d); break;
      case BATTERY_MENU: renderBatteryMenu(d); break;
      case ADVANCED_MENU: renderAdvancedMenu(d); break;
      case HW_TEST: renderHwTest(d); break;
      case MSG_MEMORY: renderMsgMemory(d); break;
      case DIAGNOSTICS: renderDiagnostics(d); break;
      case RESTART_CONFIRM: renderRestartConfirm(d); break;
      case RESTART_SPLASH: renderRestartSplash(d); break;
      case FACTORY_RESET_CONFIRM: renderFactoryResetConfirm(d); break;
      case FACTORY_RESET_PROGRESS: renderFactoryResetProgress(d); break;
      case HIBERNATE_SPLASH: renderHibernateSplash(d); break;
      case NONE:
      default:
        switch (_page) {
          case HOME: renderHome(d); break;
          case MESSAGES: renderMessages(d); break;
          case NODES: renderNodes(d); break;
          case RADIO: renderRadio(d); break;
          case GPS: renderGPS(d); break;
          case BLUETOOTH: renderBluetooth(d); break;
          case SETTINGS: renderSettings(d); break;
          case INFO: renderInfo(d); break;
          default: renderHome(d); break;
        }
        break;
    }
    return (_pending == ACTION_NONE) ? 30000 : 500;
  }

  bool handleInput(char c) override {
    if (_pending != ACTION_NONE) return true;

    if (_overlay == NODE_DETAIL) {
      if (c == KEY_NEXT) {
        int n = recentCount();
        if (n > 0) _nodeSel = (_nodeSel + 1) % n;
      } else if (c == KEY_ENTER || c == KEY_PREV) _overlay = NONE;
      return true;
    }

    if (_overlay == RADIO_MENU) {
      if (c == KEY_NEXT) _sel = (_sel + 1) % 5;
      else if (c == KEY_PREV) _overlay = NONE;
      else if (c == KEY_ENTER) {
        if (_sel == 0) { _sel=0; _overlay=ADVERT_CONFIRM; }
        else if (_sel == 1) {
          static const int vals[] = {10,14,17,20,22};
          _txSel=0; for(int i=0;i<5;i++) if(vals[i]==_prefs->tx_power_dbm) _txSel=i;
          _overlay=TX_MENU;
        } else if (_sel == 2) _overlay=RADIO_PARAMS;
        else if (_sel == 3) { _sel=0; _overlay=RADIO_RESTORE_CONFIRM; }
        else _overlay=NONE;
      }
      return true;
    }

    if (_overlay == ADVERT_CONFIRM) {
      if (c == KEY_NEXT) _sel = 1 - _sel;
      else if (c == KEY_PREV) _overlay = RADIO_MENU;
      else if (c == KEY_ENTER) {
        if (_sel == 0) {
          _task->notify(UIEventType::ack);
          if (the_mesh.advert()) _overlay = ADVERT_SENT;
          else { _overlay=RADIO_MENU; _task->showAlert("BŁĄD ADVERT",1200); }
        } else _overlay = RADIO_MENU;
      }
      return true;
    }

    if (_overlay == ADVERT_SENT) {
      if (c == KEY_ENTER || c == KEY_NEXT || c == KEY_PREV) _overlay = RADIO_MENU;
      return true;
    }

    if (_overlay == TX_MENU) {
      static const int vals[] = {10,14,17,20,22};
      if (c == KEY_NEXT) _txSel = (_txSel + 1) % 5;
      else if (c == KEY_PREV) _overlay = RADIO_MENU;
      else if (c == KEY_ENTER) {
        _previousTx = _prefs->tx_power_dbm;
        _prefs->tx_power_dbm = vals[_txSel];
        radio_driver.setTxPower(_prefs->tx_power_dbm);
        the_mesh.savePrefs();
        _task->showAlert("MOC TX ZAPISANA",1000);
        _overlay=RADIO_MENU;
      }
      return true;
    }

    if (_overlay == RADIO_PARAMS) {
      if (c == KEY_ENTER || c == KEY_PREV || c == KEY_NEXT) _overlay=RADIO_MENU;
      return true;
    }

    if (_overlay == RADIO_RESTORE_CONFIRM) {
      if (c == KEY_NEXT) _sel = 1 - _sel;
      else if (c == KEY_PREV) _overlay=RADIO_MENU;
      else if (c == KEY_ENTER) {
        if (_sel==0) {
          int8_t cur=_prefs->tx_power_dbm;
          _prefs->tx_power_dbm=_previousTx;
          _previousTx=cur;
          radio_driver.setTxPower(_prefs->tx_power_dbm);
          the_mesh.savePrefs();
          _task->showAlert("PRZYWRÓCONO MOC TX",1000);
        }
        _overlay=RADIO_MENU;
      }
      return true;
    }

    if (_overlay == GPS_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%3;
      else if (c == KEY_PREV) _overlay=NONE;
      else if (c == KEY_ENTER) {
        if (_sel==0) _task->toggleGPS();
        else if (_sel==1) _task->showAlert("POZYCJA ODŚWIEŻANA",900);
        else _overlay=NONE;
      }
      return true;
    }

    if (_overlay == BT_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%3;
      else if (c == KEY_PREV) _overlay=NONE;
      else if (c == KEY_ENTER) {
        if (_sel==0) {
          if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth();
        } else if (_sel==1) {
          if (_task->isBluetoothEnabled()) {
            _task->disableBluetooth();
            _task->enableBluetooth();
            _task->showAlert("BLE ROZŁĄCZONY",900);
          }
        } else _overlay=NONE;
      }
      return true;
    }

    if (_overlay == SETTINGS_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%5;
      else if (c == KEY_PREV) _overlay=NONE;
      else if (c == KEY_ENTER) {
        if (_sel==0) { _sel=0; _overlay=DISPLAY_MENU; }
        else if (_sel==1) { _sel=0; _overlay=SOUND_MENU; }
        else if (_sel==2) { _sel=0; _overlay=POWER_MENU; }
        else if (_sel==3) { _sel=0; _overlay=ADVANCED_MENU; }
        else _overlay=NONE;
      }
      return true;
    }

    if (_overlay == DISPLAY_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%6;
      else if (c == KEY_PREV) { _sel=0; _overlay=SETTINGS_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel==0) {
          _frontlight=!_frontlight;
#ifdef DISP_BACKLIGHT
          digitalWrite(DISP_BACKLIGHT,_frontlight ? HIGH : LOW);
#endif
        } else if (_sel==5) { _sel=0; _overlay=SETTINGS_MENU; }
      }
      return true;
    }

    if (_overlay == SOUND_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%6;
      else if (c == KEY_PREV) { _sel=1; _overlay=SETTINGS_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel==0) _task->toggleBuzzer();
        else if (_sel==5) { _sel=1; _overlay=SETTINGS_MENU; }
      }
      return true;
    }

    if (_overlay == POWER_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%3;
      else if (c == KEY_PREV) { _sel=2; _overlay=SETTINGS_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel==0) { _sel=2; _overlay=BATTERY_MENU; }
        else if (_sel==1) scheduleAction(ACTION_HIBERNATE,HIBERNATE_SPLASH);
        else { _sel=2; _overlay=SETTINGS_MENU; }
      }
      return true;
    }

    if (_overlay == BATTERY_MENU) {
      const int vals[] = {10,15,20,25};
      if (c == KEY_NEXT) _sel=(_sel+1)%5;
      else if (c == KEY_PREV) { _sel=0; _overlay=POWER_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel<4) {
          _batteryWarnPct=vals[_sel];
          _task->showAlert("PRÓG ZAPISANY W UI",900);
        } else { _sel=0; _overlay=POWER_MENU; }
      }
      return true;
    }

    if (_overlay == ADVANCED_MENU) {
      if (c == KEY_NEXT) _sel=(_sel+1)%6;
      else if (c == KEY_PREV) { _sel=3; _overlay=SETTINGS_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel==0) _overlay=HW_TEST;
        else if (_sel==1) _overlay=MSG_MEMORY;
        else if (_sel==2) _overlay=DIAGNOSTICS;
        else if (_sel==3) { _sel=0; _overlay=RESTART_CONFIRM; }
        else if (_sel==4) { _sel=1; _overlay=FACTORY_RESET_CONFIRM; }
        else { _sel=3; _overlay=SETTINGS_MENU; }
      }
      return true;
    }

    if (_overlay == HW_TEST || _overlay == MSG_MEMORY || _overlay == DIAGNOSTICS) {
      if (c == KEY_ENTER || c == KEY_PREV) { _sel=0; _overlay=ADVANCED_MENU; }
      return true;
    }

    if (_overlay == RESTART_CONFIRM) {
      if (c == KEY_NEXT) _sel=1-_sel;
      else if (c == KEY_PREV) { _sel=3; _overlay=ADVANCED_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel==0) scheduleAction(ACTION_RESTART,RESTART_SPLASH);
        else { _sel=3; _overlay=ADVANCED_MENU; }
      }
      return true;
    }

    if (_overlay == FACTORY_RESET_CONFIRM) {
      if (c == KEY_NEXT) _sel=1-_sel;
      else if (c == KEY_PREV) { _sel=4; _overlay=ADVANCED_MENU; }
      else if (c == KEY_ENTER) {
        if (_sel==0) scheduleAction(ACTION_FACTORY_RESET,FACTORY_RESET_PROGRESS);
        else { _sel=4; _overlay=ADVANCED_MENU; }
      }
      return true;
    }

    if (c == KEY_PREV) {
      _page=(Page)((_page+COUNT-1)%COUNT);
      return true;
    }
    if (c == KEY_NEXT) {
      _page=(Page)((_page+1)%COUNT);
      return true;
    }
    if (c == KEY_ENTER) {
      switch(_page) {
        case MESSAGES:
          if (_task->getMsgCount()>0) _task->gotoMessagePreview();
          else _task->showAlert("BRAK NOWYCH WIAD.",1000);
          break;
        case NODES:
          if (recentCount()>0) { _nodeSel=0; _overlay=NODE_DETAIL; }
          break;
        case RADIO:
          _sel=0; _overlay=RADIO_MENU; break;
        case GPS:
          _sel=0; _overlay=GPS_MENU; break;
        case BLUETOOTH:
          _sel=0; _overlay=BT_MENU; break;
        case SETTINGS:
          _sel=0; _overlay=SETTINGS_MENU; break;
        default:
          break;
      }
      return true;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[120];
  };
  static const int MAX_UNREAD_MSGS = 32;
  int num_unread = 0;
  int head = MAX_UNREAD_MSGS - 1;
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) {
    memset(unread, 0, sizeof(unread));
  }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;
    MsgEntry* p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    snprintf(p->origin, sizeof(p->origin), "%s", from_name);
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& d) override {
    drawHeader(d, _task->getBattMilliVolts());
    if (num_unread <= 0) {
      d.setTextSize(2);
      d.drawTextCentered(100, 105, "BRAK WIADOMOSCI");
      drawFooter(d, "v WSTECZ", "");
      return 30000;
    }

    MsgEntry* p = &unread[head];
    d.setTextSize(2);
    d.setColor(UIColor::primary_txt);
    d.setCursor(8, 48);
    d.print("NOWA WIADOMOŚĆ");

    d.setTextSize(1);
    d.setCursor(8, 77);
    d.print(p->origin);

    char age[16];
    ageText(_rtc->getCurrentTime(), p->timestamp, age, sizeof(age));
    d.drawTextRightAlign(192, 77, age);
    d.drawRect(6, 86, 188, 1);

    d.setCursor(8, 108);
    d.printWordWrap(p->msg, 184);

    char countBuf[20];
    snprintf(countBuf, sizeof(countBuf), "%d nowych", num_unread);
    d.drawTextCentered(100, 173, countBuf);
    drawFooter(d, "v NAST.", "o ZAMKNIJ");
    return 10000;
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT) {
      if (num_unread > 0) {
        head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
        num_unread--;
      }
      if (num_unread <= 0) _task->gotoMessagesScreen();
      return true;
    }
    if (c == KEY_ENTER || c == KEY_PREV) {
      num_unread = 0;
      _task->gotoMessagesScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  _msgcount = 0;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(THINKNODE_M1)
  function_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  if (_display != NULL) _display->turnOn();

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif
#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;
  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  setCurrScreen(splash);
}

void UITask::gotoMessagesScreen() {
  ((HomeScreen*)home)->setPage(HomeScreen::MESSAGES);
  setCurrScreen(home);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strncpy(_alert, text, sizeof(_alert)-1);
  _alert[sizeof(_alert)-1] = 0;
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#ifdef PIN_BUZZER
  switch(t){
    case UIEventType::contactMessage:
      buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
      break;
    case UIEventType::channelMessage:
      buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
      break;
    case UIEventType::ack:
      buzzer.play("ack:d=32,o=8,b=120:c");
      break;
    default:
      break;
  }
#endif
#ifdef PIN_VIBRATION
  if (t != UIEventType::none) vibration.trigger();
#endif
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0 && curr == msg_preview) gotoMessagesScreen();
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  ((MsgPreviewScreen*)msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) _display->turnOn();
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 0;
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      last_led_increment = _msgcount > 0 ? LED_ON_MSG_MILLIS : LED_ON_MILLIS;
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 0;
}

void UITask::shutdown(bool restart) {
#ifdef PIN_BUZZER
  buzzer.shutdown();
  uint32_t started = millis();
  while (buzzer.isPlaying() && millis() - started < 2500) buzzer.loop();
#endif
  if (restart) _board->reboot();
  else _board->powerOff();
}

bool UITask::isButtonPressed() const {
#if defined(THINKNODE_M1)
  return user_btn.isPressed() || function_btn.isPressed();
#elif defined(PIN_USER_BTN)
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;

#if defined(THINKNODE_M1)
  int evNav = user_btn.check();
  if (evNav == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (evNav == BUTTON_EVENT_LONG_PRESS) {
    c = checkDisplayOn(KEY_PREV);
  } else if (evNav == BUTTON_EVENT_DOUBLE_CLICK) {
    if (_display && !_display->isOn()) _display->turnOn();
    gotoHomeScreen();
    c = 0;
  }

  int evFn = function_btn.check();
  if (evFn == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (evFn == BUTTON_EVENT_LONG_PRESS) {
    if (millis() - ui_started_at < 8000) {
      the_mesh.enterCLIRescue();
    } else {
      gotoHomeScreen();
    }
    c = 0;
  } else if (evFn == BUTTON_EVENT_DOUBLE_CLICK) {
    if (_display && !_display->isOn()) _display->turnOn();
    gotoMessagesScreen();
    c = 0;
  } else if (evFn == BUTTON_EVENT_TRIPLE_CLICK) {
    toggleBuzzer();
    c = 0;
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) c = checkDisplayOn(KEY_NEXT);
  else if (ev == BUTTON_EVENT_LONG_PRESS) c = handleLongPress(KEY_ENTER);
  else if (ev == BUTTON_EVENT_DOUBLE_CLICK) c = handleDoubleClick(KEY_PREV);
  else if (ev == BUTTON_EVENT_TRIPLE_CLICK) c = handleTripleClick(KEY_SELECT);
#endif

#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) c = checkDisplayOn(KEY_NEXT);
    else if (ev == BUTTON_EVENT_LONG_PRESS) c = handleLongPress(KEY_ENTER);
    _analogue_pin_read_millis = millis();
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
  }

  userLedHandler();
#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) buzzer.loop();
#endif
  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);

      if (millis() < _alert_expiry) {
        _display->setTextSize(1);
        int y = 80;
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect(12, y, 176, 44);
        _display->setColor(UIColor::popup_txt);
        _display->drawRect(12, y, 176, 44);
        _display->drawTextCentered(100, 107, _alert);
        _next_refresh = _alert_expiry;
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }

#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) _display->turnOff();
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t mv = getBattMilliVolts();
    if (mv > 0 && mv < AUTO_SHUTDOWN_MILLIVOLTS && !board.isExternalPowered()) {
      if (_display != NULL) {
        _display->startFrame();
        drawHeader(*_display, mv);
        _display->setTextSize(2);
        _display->setColor(UIColor::warning_txt);
        _display->drawTextCentered(100, 90, "BARDZO NISKI");
        _display->drawTextCentered(100, 118, "POZIOM BATERII");
        _display->setTextSize(1);
        _display->drawTextCentered(100, 150, "Urządzenie wyłącza się");
        _display->endFrame();
      }
      shutdown();
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {
    the_mesh.enterCLIRescue();
    return 0;
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  checkDisplayOn(c);
  toggleBuzzer();
  return 0;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return strcmp(_sensors->getSettingValue(i), "1") == 0;
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        bool enabled = strcmp(_sensors->getSettingValue(i), "1") == 0;
        _sensors->setSettingValue("gps", enabled ? "0" : "1");
        _node_prefs->gps_enabled = enabled ? 0 : 1;
        notify(UIEventType::ack);
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS WŁĄCZONY" : "GPS WYŁĄCZONY", 900);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
#ifdef PIN_BUZZER
  if (buzzer.isQuiet()) {
    buzzer.quiet(false);
    notify(UIEventType::ack);
  } else {
    buzzer.quiet(true);
  }
  _node_prefs->buzzer_quiet = buzzer.isQuiet();
  the_mesh.savePrefs();
  showAlert(buzzer.isQuiet() ? "DŹWIĘK WYŁ." : "DŹWIĘK WŁ.", 900);
  _next_refresh = 0;
#endif
}
