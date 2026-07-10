#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <DFRobotDFPlayerMini.h>
#include <RTClib.h>
#include <Preferences.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ================= RTC DS3231 =================
RTC_DS3231 rtc;

// ================= BO NHO ESP32 =================
Preferences prefs;

// ================= DFPLAYER =================
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini myDFPlayer;

// ================= RELAY KHOA CUA =================
// Chan IN cua module relay noi GPIO18
const int RELAY_PIN = 18;
const unsigned long DOOR_OPEN_TIME_MS = 8000;

// Theo code hien tai cua ban: relay ACTIVE HIGH
// HIGH = mo khoa, LOW = khoa lai
// Neu relay cua ban hoat dong nguoc thi doi lai:
// RELAY_ON  = LOW;
// RELAY_OFF = HIGH;
const int RELAY_ON  = HIGH;
const int RELAY_OFF = LOW;

// ================= BUZZER ACTIVE HIGH =================
// Chan signal buzzer noi GPIO19
// HIGH = keu, LOW = tat
const int BUZZER_PIN = 19;

// ================= BUTTON MO CUA BEN TRONG =================
// Button moi: nhan = LOW, nha = HIGH
// Chan signal button noi GPIO4
const int BUTTON_PIN = 4;

// ================= KEYPAD =================
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Keypad theo chan moi cua ban
// Row0 = GPIO13, Row1 = GPIO12, Row2 = GPIO14, Row3 = GPIO27
// Col0 = GPIO26, Col1 = GPIO25, Col2 = GPIO33, Col3 = GPIO32
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= STRUCT TAI KHOAN =================
struct AdminAccount {
  String username;
  String password;
};

struct UserAccount {
  String username;
  String password;
  uint32_t expiryUnix;
  bool active;
};

// ================= DANH SACH TAI KHOAN =================
const int MAX_ADMINS = 1;
const int MAX_USERS  = 10;

AdminAccount admins[MAX_ADMINS];
UserAccount users[MAX_USERS];

int adminCount = 1;
int userCount = 0;

// ================= STATE =================
enum State {
  ST_HOME,
  ST_LOGIN,

  ST_ADMIN_MENU,
  ST_USER_MENU,
  ST_USER_EXPIRY,
  ST_USER_SETTING_MENU,

  ST_REGISTER_USER,

  ST_SET_EXP_LIST,
  ST_SET_EXP_EDIT,

  ST_DELETE_USER_LIST,
  ST_DELETE_USER_CONFIRM
};

State currentState = ST_HOME;

// 0 = Admin, 1 = User
int accountType = 0;
int loggedInUser = -1;

// ================= LOGIN INPUT =================
String loginUsername = "";
String loginPassword = "";
int loginField = 0;
// 0 = tai khoan
// 1 = mat khau

// ================= REGISTER USER INPUT =================
String regUsername = "";
String regPassword = "";
String regDate = "";  // DDMMYYYY
String regTime = "";  // HHMMSS

int regField = 0;
// 0 = tai khoan
// 1 = mat khau
// 2 = ngay het han
// 3 = gio het han

// ================= SET EXPIRY INPUT =================
int selectedUser = 0;

String newExpiryDate = "";  // DDMMYYYY
String newExpiryTime = "";  // HHMMSS
String newExpiryPassword = "";

int expiryField = 0;
// 0 = mat khau
// 1 = ngay moi
// 2 = gio moi

// ================= DEM SAI =================
int wrongCount = 0;

// ================= DEBOUNCE BUTTON =================
unsigned long lastButtonTime = 0;

// =======================================================
// KHOI TAO ADMIN MAC DINH
// =======================================================
void initDefaultAdmin() {
  admins[0].username = "0001";
  admins[0].password = "1234";
}

// =======================================================
// DFPLAYER
// =======================================================
void playTrack(uint16_t trackNum) {
  myDFPlayer.playMp3Folder(trackNum);
  delay(200);
}

// =======================================================
// BUZZER ACTIVE HIGH
// =======================================================
void buzzerOn() {
  digitalWrite(BUZZER_PIN, HIGH);
}

void buzzerOff() {
  digitalWrite(BUZZER_PIN, LOW);
}

// =======================================================
// RELAY
// =======================================================
void lockDoor() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
}

void unlockDoor() {
  digitalWrite(RELAY_PIN, RELAY_ON);
}

void openDoorByRelay() {
  unlockDoor();
  delay(DOOR_OPEN_TIME_MS);
  lockDoor();
  delay(300);
}

// =======================================================
// LCD MESSAGE
// =======================================================
void showTempMessage(String line1, String line2 = "", unsigned long holdMs = 1200) {
  lcd.noBlink();
  lcd.noCursor();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(line2);

  delay(holdMs);
}

// =======================================================
// MAT KHAU HIEN DAU *
// =======================================================
String maskPassword(String pass) {
  String s = "";

  for (int i = 0; i < pass.length(); i++) {
    s += "*";
  }

  return s;
}

// =======================================================
// FORMAT DATE / TIME
// =======================================================
String formatDateInput(String d) {
  // d = DDMMYYYY
  String out = "";

  for (int i = 0; i < 8; i++) {
    if (i < d.length()) {
      out += d[i];
    } else {
      out += "_";
    }

    if (i == 1 || i == 3) {
      out += "/";
    }
  }

  return out;
}

String formatTimeInput(String t) {
  // t = HHMMSS
  String out = "";

  for (int i = 0; i < 6; i++) {
    if (i < t.length()) {
      out += t[i];
    } else {
      out += "_";
    }

    if (i == 1 || i == 3) {
      out += ":";
    }
  }

  return out;
}

bool isLeapYear(int year) {
  return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

int daysInMonth(int month, int year) {
  if (month == 2) {
    return isLeapYear(year) ? 29 : 28;
  }

  if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }

  return 31;
}

bool unixToDateTime(uint32_t unixTime, int &year, int &month, int &day,
                    int &hour, int &minute, int &second) {
  uint32_t days = unixTime / 86400UL;
  uint32_t remain = unixTime % 86400UL;

  hour = remain / 3600UL;
  remain %= 3600UL;
  minute = remain / 60UL;
  second = remain % 60UL;

  year = 1970;

  while (true) {
    int daysThisYear = isLeapYear(year) ? 366 : 365;

    if (days < (uint32_t)daysThisYear) {
      break;
    }

    days -= daysThisYear;
    year++;
  }

  month = 1;

  while (true) {
    int dim = daysInMonth(month, year);

    if (days < (uint32_t)dim) {
      break;
    }

    days -= dim;
    month++;
  }

  day = days + 1;

  return true;
}

String formatDateFullFromUnix(uint32_t unixTime) {
  if (unixTime == 0) {
    return "__/__/____";
  }

  int year, month, day, hour, minute, second;
  unixToDateTime(unixTime, year, month, day, hour, minute, second);

  char buf[12];
  sprintf(buf, "%02d/%02d/%04d", day, month, year);

  return String(buf);
}

String formatDateShortFromUnix(uint32_t unixTime) {
  if (unixTime == 0) {
    return "__/__/__";
  }

  int year, month, day, hour, minute, second;
  unixToDateTime(unixTime, year, month, day, hour, minute, second);

  char buf[10];
  sprintf(buf, "%02d/%02d/%02d", day, month, year % 100);

  return String(buf);
}

String formatTimeFromUnix(uint32_t unixTime) {
  if (unixTime == 0) {
    return "__:__:__";
  }

  int year, month, day, hour, minute, second;
  unixToDateTime(unixTime, year, month, day, hour, minute, second);

  char buf[10];
  sprintf(buf, "%02d:%02d:%02d", hour, minute, second);

  return String(buf);
}

String currentDateShort() {
  DateTime now = rtc.now();

  char buf[10];
  sprintf(buf, "%02d/%02d/%02d", now.day(), now.month(), now.year() % 100);

  return String(buf);
}

String currentTimeShort() {
  DateTime now = rtc.now();

  char buf[10];
  sprintf(buf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  return String(buf);
}

// =======================================================
// VI TRI CON TRO KHI NHAP
// =======================================================
int cursorColForDate(int prefixLen, int rawLen) {
  int col = prefixLen + rawLen;

  if (rawLen >= 2) col++;
  if (rawLen >= 4) col++;

  return col;
}

int cursorColForTime(int prefixLen, int rawLen) {
  int col = prefixLen + rawLen;

  if (rawLen >= 2) col++;
  if (rawLen >= 4) col++;

  return col;
}

// =======================================================
// CHUYEN DDMMYYYY + HHMMSS SANG UNIX
// =======================================================
uint32_t dateTimeToUnix(String d, String t) {
  if (d.length() != 8) return 0;
  if (t.length() != 6) return 0;

  int day   = d.substring(0, 2).toInt();
  int month = d.substring(2, 4).toInt();
  int year  = d.substring(4, 8).toInt();

  int hour   = t.substring(0, 2).toInt();
  int minute = t.substring(2, 4).toInt();
  int second = t.substring(4, 6).toInt();

  if (month < 1 || month > 12) return 0;
  if (year < 1970 || year > 2099) return 0;
  if (day < 1 || day > daysInMonth(month, year)) return 0;

  if (hour < 0 || hour > 23) return 0;
  if (minute < 0 || minute > 59) return 0;
  if (second < 0 || second > 59) return 0;

  uint32_t days = 0;

  for (int y = 1970; y < year; y++) {
    days += isLeapYear(y) ? 366 : 365;
  }

  for (int m = 1; m < month; m++) {
    days += daysInMonth(m, year);
  }

  days += day - 1;

  return days * 86400UL + hour * 3600UL + minute * 60UL + second;
}

bool isUserExpired(uint32_t expiryUnix) {
  DateTime now = rtc.now();

  if (now.unixtime() > expiryUnix) {
    return true;
  }

  return false;
}

// =======================================================
// LUU / DOC USER TU BO NHO ESP32
// =======================================================
void saveUsers() {
  prefs.begin("users", false);

  prefs.putInt("userCount", userCount);

  for (int i = 0; i < MAX_USERS; i++) {
    String keyUsername = "u" + String(i);
    String keyPassword = "p" + String(i);
    String keyExpiry   = "e" + String(i);
    String keyActive   = "a" + String(i);

    prefs.putString(keyUsername.c_str(), users[i].username);
    prefs.putString(keyPassword.c_str(), users[i].password);
    prefs.putUInt(keyExpiry.c_str(), users[i].expiryUnix);
    prefs.putBool(keyActive.c_str(), users[i].active);
  }

  prefs.end();
}

void loadUsers() {
  prefs.begin("users", true);

  userCount = prefs.getInt("userCount", 0);

  if (userCount < 0 || userCount > MAX_USERS) {
    userCount = 0;
  }

  for (int i = 0; i < MAX_USERS; i++) {
    String keyUsername = "u" + String(i);
    String keyPassword = "p" + String(i);
    String keyExpiry   = "e" + String(i);
    String keyActive   = "a" + String(i);

    users[i].username = prefs.getString(keyUsername.c_str(), "");
    users[i].password = prefs.getString(keyPassword.c_str(), "");
    users[i].expiryUnix = prefs.getUInt(keyExpiry.c_str(), 0);
    users[i].active = prefs.getBool(keyActive.c_str(), false);
  }

  prefs.end();
}

// =======================================================
// TIM KIEM TAI KHOAN
// =======================================================
bool checkAdminLogin(String username, String password) {
  for (int i = 0; i < adminCount; i++) {
    if (admins[i].username == username && admins[i].password == password) {
      return true;
    }
  }

  return false;
}

int findUserByUsername(String username) {
  for (int i = 0; i < userCount; i++) {
    if (users[i].active && users[i].username == username) {
      return i;
    }
  }

  return -1;
}

// =======================================================
// HIEN THI MAN HINH
// =======================================================
void showHome() {
  currentState = ST_HOME;
  loggedInUser = -1;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("A: Admin");

  lcd.setCursor(0, 1);
  lcd.print("B: User");

  lcd.setCursor(0, 2);
  lcd.print("Nhan A/B de vao");

  lcd.setCursor(0, 3);
  lcd.print("Nut trong=Mo cua");

  lcd.noCursor();
  lcd.noBlink();
}

void showLogin() {
  currentState = ST_LOGIN;

  lcd.clear();

  if (accountType == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Dang nhap Admin");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Dang nhap User");
  }

  lcd.setCursor(0, 1);
  if (loginField == 0) lcd.print(">");
  else lcd.print(" ");
  lcd.print("TK:");
  lcd.print(loginUsername);

  lcd.setCursor(0, 2);
  if (loginField == 1) lcd.print(">");
  else lcd.print(" ");
  lcd.print("MK:");
  lcd.print(maskPassword(loginPassword));

  // Dong 4 bo trong theo yeu cau
  lcd.setCursor(0, 3);
  lcd.print("                    ");

  lcd.cursor();
  lcd.blink();

  if (loginField == 0) {
    lcd.setCursor(4 + loginUsername.length(), 1);
  } else {
    lcd.setCursor(4 + loginPassword.length(), 2);
  }
}

void showAdminMenu() {
  currentState = ST_ADMIN_MENU;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("MENU ADMIN");

  lcd.setCursor(0, 1);
  lcd.print("A. Mo cua");

  lcd.setCursor(0, 2);
  lcd.print("B. Cai dat User");

  lcd.setCursor(0, 3);
  lcd.print("C. Quay lai");

  lcd.noCursor();
  lcd.noBlink();
}

void showUserMenu() {
  currentState = ST_USER_MENU;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("MENU USER");

  lcd.setCursor(0, 1);
  lcd.print("A. Mo cua");

  lcd.setCursor(0, 2);
  lcd.print("B. Xem thoi han");

  lcd.setCursor(0, 3);
  lcd.print("C. Quay lai");

  lcd.noCursor();
  lcd.noBlink();
}

void showUserExpiry() {
  currentState = ST_USER_EXPIRY;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Gio:");
  lcd.print(formatTimeFromUnix(users[loggedInUser].expiryUnix));

  lcd.setCursor(0, 1);
  lcd.print("Ngay:");
  lcd.print(formatDateFullFromUnix(users[loggedInUser].expiryUnix));

  lcd.setCursor(0, 2);
  lcd.print("C: Quay lai menu");

  lcd.setCursor(0, 3);
  lcd.print("D: Ve man hinh dau");

  lcd.noCursor();
  lcd.noBlink();
}

void showUserSettingMenu() {
  currentState = ST_USER_SETTING_MENU;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("A. Dang ky User");

  lcd.setCursor(0, 1);
  lcd.print("B. Cai han User");

  lcd.setCursor(0, 2);
  lcd.print("C. Xoa User");

  lcd.setCursor(0, 3);
  lcd.print("D. Quay lai");

  lcd.noCursor();
  lcd.noBlink();
}

void showRegisterUser() {
  currentState = ST_REGISTER_USER;

  lcd.clear();

  lcd.setCursor(0, 0);
  if (regField == 0) lcd.print(">");
  else lcd.print(" ");
  lcd.print("TK:");
  lcd.print(regUsername);

  lcd.setCursor(0, 1);
  if (regField == 1) lcd.print(">");
  else lcd.print(" ");
  lcd.print("MK:");
  lcd.print(maskPassword(regPassword));

  lcd.setCursor(0, 2);
  if (regField == 2) lcd.print(">");
  else lcd.print(" ");
  lcd.print("Han:");
  lcd.print(formatDateInput(regDate));

  lcd.setCursor(0, 3);
  if (regField == 3) lcd.print(">");
  else lcd.print(" ");
  lcd.print("Gio:");
  lcd.print(formatTimeInput(regTime));

  lcd.cursor();
  lcd.blink();

  if (regField == 0) {
    lcd.setCursor(4 + regUsername.length(), 0);
  } else if (regField == 1) {
    lcd.setCursor(4 + regPassword.length(), 1);
  } else if (regField == 2) {
    lcd.setCursor(cursorColForDate(5, regDate.length()), 2);
  } else if (regField == 3) {
    lcd.setCursor(cursorColForTime(5, regTime.length()), 3);
  }
}

void showUserList(State listState) {
  currentState = listState;

  lcd.clear();

  if (userCount == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Chua co User");

    lcd.setCursor(0, 1);
    lcd.print("C/D: Quay lai");

    lcd.noCursor();
    lcd.noBlink();
    return;
  }

  int startIndex = 0;

  if (selectedUser >= 4) {
    startIndex = selectedUser - 3;
  }

  for (int i = 0; i < 4; i++) {
    int userIndex = startIndex + i;

    if (userIndex >= userCount) {
      break;
    }

    lcd.setCursor(0, i);

    if (userIndex == selectedUser) {
      lcd.print(">");
    } else {
      lcd.print(" ");
    }

    lcd.print(users[userIndex].username);
    lcd.print(" ");
    lcd.print(formatDateShortFromUnix(users[userIndex].expiryUnix));
  }

  lcd.noCursor();
  lcd.noBlink();
}

void showSetExpiryEdit() {
  currentState = ST_SET_EXP_EDIT;

  lcd.clear();

  lcd.setCursor(0, 0);
  if (expiryField == 0) lcd.print(">");
  else lcd.print(" ");
  lcd.print("MK:");
  lcd.print(newExpiryPassword);

  lcd.setCursor(0, 1);
  lcd.print("Cu:");
  lcd.print(formatDateShortFromUnix(users[selectedUser].expiryUnix));
  lcd.print(" ");
  lcd.print(formatTimeFromUnix(users[selectedUser].expiryUnix));

  lcd.setCursor(0, 2);
  if (expiryField == 1) lcd.print(">");
  else lcd.print(" ");
  lcd.print("Moi:");
  lcd.print(formatDateInput(newExpiryDate));

  lcd.setCursor(0, 3);
  if (expiryField == 2) lcd.print(">");
  else lcd.print(" ");
  lcd.print("Gio:");
  lcd.print(formatTimeInput(newExpiryTime));

  lcd.cursor();
  lcd.blink();

  if (expiryField == 0) {
    lcd.setCursor(4 + newExpiryPassword.length(), 0);
  } else if (expiryField == 1) {
    lcd.setCursor(cursorColForDate(6, newExpiryDate.length()), 2);
  } else if (expiryField == 2) {
    lcd.setCursor(cursorColForTime(5, newExpiryTime.length()), 3);
  }
}

void showDeleteConfirm() {
  currentState = ST_DELETE_USER_CONFIRM;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Xoa User:");
  lcd.print(users[selectedUser].username);

  lcd.setCursor(0, 1);
  lcd.print("A. Dong y xoa");

  lcd.setCursor(0, 2);
  lcd.print("B. Quay lai");

  lcd.setCursor(0, 3);
  lcd.print("C/D: Quay lai");

  lcd.noCursor();
  lcd.noBlink();
}

// =======================================================
// RESET INPUT
// =======================================================
void resetLoginInput() {
  loginUsername = "";
  loginPassword = "";
  loginField = 0;
}

void resetRegisterInput() {
  regUsername = "";
  regPassword = "";
  regDate = "";
  regTime = "";
  regField = 0;
}

void resetExpiryInput() {
  newExpiryDate = "";
  newExpiryTime = "";
  newExpiryPassword = users[selectedUser].password;
  expiryField = 0;
}

// =======================================================
// LOGIN
// =======================================================
void goToLogin(int type) {
  accountType = type;
  resetLoginInput();

  playTrack(1);
  showLogin();
}

void wrongProcess(uint16_t soundFile) {
  wrongCount++;

  playTrack(soundFile);

  if (wrongCount >= 3) {
    showTempMessage("Sai qua 3 lan", "Bao dong 5 giay", 500);

    buzzerOn();
    delay(5000);
    buzzerOff();

    wrongCount = 0;
  } else {
    if (soundFile == 6) {
      showTempMessage("User da het han", "Khong mo cua", 1200);
    } else {
      showTempMessage("Sai TK hoac MK", "Thu lai", 1200);
    }
  }

  showHome();
}

void checkLogin() {
  if (accountType == 0) {
    if (checkAdminLogin(loginUsername, loginPassword)) {
      wrongCount = 0;

      playTrack(2);
      showTempMessage("Dung Admin", "Vao menu Admin", 800);
      showAdminMenu();
    } else {
      wrongProcess(3);
    }
  } else {
    int userIndex = findUserByUsername(loginUsername);

    if (userIndex == -1) {
      wrongProcess(3);
      return;
    }

    if (users[userIndex].password != loginPassword) {
      wrongProcess(3);
      return;
    }

    if (isUserExpired(users[userIndex].expiryUnix)) {
      // Qua han: dung mat khau van khong mo cua, phat file 6
      wrongProcess(6);
      return;
    }

    wrongCount = 0;
    loggedInUser = userIndex;

    playTrack(2);
    showTempMessage("Dung mat khau", "Vao menu User", 700);
    showUserMenu();
  }
}

// =======================================================
// DANG KY USER
// =======================================================
void saveNewUser() {
  if (userCount >= MAX_USERS) {
    showTempMessage("Danh sach day", "Khong them duoc", 1200);
    showAdminMenu();
    return;
  }

  if (regUsername.length() == 0) {
    showTempMessage("Chua nhap TK", "Nhap lai", 1200);
    showRegisterUser();
    return;
  }

  if (regPassword.length() == 0) {
    showTempMessage("Chua nhap MK", "Nhap lai", 1200);
    showRegisterUser();
    return;
  }

  if (regDate.length() != 8) {
    showTempMessage("Ngay sai", "Nhap DDMMYYYY", 1200);
    showRegisterUser();
    return;
  }

  if (regTime.length() != 6) {
    showTempMessage("Gio sai", "Nhap HHMMSS", 1200);
    showRegisterUser();
    return;
  }

  if (findUserByUsername(regUsername) != -1) {
    showTempMessage("TK da ton tai", "Nhap TK khac", 1200);
    showRegisterUser();
    return;
  }

  uint32_t expiry = dateTimeToUnix(regDate, regTime);

  if (expiry == 0) {
    showTempMessage("Ngay/Gio sai", "Nhap lai", 1200);
    showRegisterUser();
    return;
  }

  users[userCount].username = regUsername;
  users[userCount].password = regPassword;
  users[userCount].expiryUnix = expiry;
  users[userCount].active = true;

  userCount++;

  saveUsers();

  playTrack(7);
  showTempMessage("Dang ky thanh cong", "User:" + regUsername, 1200);

  showAdminMenu();
}

// =======================================================
// DOI THOI HAN USER
// =======================================================
void saveNewExpiry() {
  if (newExpiryPassword.length() == 0) {
    showTempMessage("MK khong duoc rong", "Nhap lai", 1200);
    showSetExpiryEdit();
    return;
  }

  if (newExpiryDate.length() != 8) {
    showTempMessage("Ngay sai", "Nhap DDMMYYYY", 1200);
    showSetExpiryEdit();
    return;
  }

  if (newExpiryTime.length() != 6) {
    showTempMessage("Gio sai", "Nhap HHMMSS", 1200);
    showSetExpiryEdit();
    return;
  }

  uint32_t expiry = dateTimeToUnix(newExpiryDate, newExpiryTime);

  if (expiry == 0) {
    showTempMessage("Ngay/Gio sai", "Nhap lai", 1200);
    showSetExpiryEdit();
    return;
  }

  users[selectedUser].expiryUnix = expiry;
  users[selectedUser].password = newExpiryPassword;

  saveUsers();

  playTrack(8);
  showTempMessage("Doi han thanh cong", users[selectedUser].username, 1200);

  showAdminMenu();
}

// =======================================================
// XOA USER
// =======================================================
void deleteSelectedUser() {
  if (userCount == 0) {
    showAdminMenu();
    return;
  }

  String deletedName = users[selectedUser].username;

  for (int i = selectedUser; i < userCount - 1; i++) {
    users[i] = users[i + 1];
  }

  userCount--;

  users[userCount].username = "";
  users[userCount].password = "";
  users[userCount].expiryUnix = 0;
  users[userCount].active = false;

  if (selectedUser >= userCount) {
    selectedUser = userCount - 1;
  }

  if (selectedUser < 0) {
    selectedUser = 0;
  }

  saveUsers();

  playTrack(8);
  showTempMessage("Da xoa User", deletedName, 1200);

  showAdminMenu();
}

// =======================================================
// SETUP
// =======================================================
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.cursor();
  lcd.blink();

  initDefaultAdmin();

  // Relay khoa cua
  pinMode(RELAY_PIN, OUTPUT);
  lockDoor();

  // Buzzer active high
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  // Button moi: nhan LOW, nha HIGH
  // Khong dung INPUT_PULLUP nua
  pinMode(BUTTON_PIN, INPUT);

  // RTC DS3231
  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Loi DS3231");
    lcd.setCursor(0, 1);
    lcd.print("Kiem tra day noi");
    delay(2000);
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  loadUsers();

  // DFPlayer
  dfSerial.begin(9600, SERIAL_8N1, 16, 17);

  if (!myDFPlayer.begin(dfSerial)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DFPlayer loi");
    lcd.setCursor(0, 1);
    lcd.print("Kiem tra SD/wire");

    while (true) {
      delay(100);
    }
  }

  myDFPlayer.volume(28);

  showHome();
}

// =======================================================
// LOOP
// =======================================================
void loop() {
  // Nut nhan mo cua ben trong
  // Button moi: nhan = LOW, nha = HIGH
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - lastButtonTime > 300) {
      lastButtonTime = millis();

      playTrack(5);

      if (currentState == ST_HOME) {
        showTempMessage("Mo cua ben trong", "Dang mo cua...", 500);
      }

      openDoorByRelay();

      if (currentState == ST_HOME) {
        showHome();
      }
    }
  }

  char key = keypad.getKey();

  if (!key) {
    return;
  }

  Serial.print("Key: ");
  Serial.println(key);

  // ================= HOME =================
  if (currentState == ST_HOME) {
    if (key == 'A') {
      goToLogin(0);
    } else if (key == 'B') {
      goToLogin(1);
    }
  }

  // ================= LOGIN =================
  else if (currentState == ST_LOGIN) {
    if (key == 'A') {
      if (loginField > 0) loginField--;
      showLogin();
    } else if (key == 'B') {
      if (loginField < 1) loginField++;
      showLogin();
    } else if (key == '*') {
      if (loginField == 0 && loginUsername.length() > 0) {
        loginUsername.remove(loginUsername.length() - 1);
      } else if (loginField == 1 && loginPassword.length() > 0) {
        loginPassword.remove(loginPassword.length() - 1);
      }

      showLogin();
    } else if (key == 'C') {
      showHome();
    } else if (key == 'D') {
      checkLogin();
    } else if (key >= '0' && key <= '9') {
      if (loginField == 0) {
        if (loginUsername.length() < 8) {
          loginUsername += key;
        }
      } else {
        if (loginPassword.length() < 8) {
          loginPassword += key;
        }
      }

      showLogin();
    }
  }

  // ================= ADMIN MENU =================
  else if (currentState == ST_ADMIN_MENU) {
    if (key == 'A') {
      playTrack(2);
      showTempMessage("Admin mo cua", "Dang mo cua...", 700);

      openDoorByRelay();

      // Admin mo cua xong quay lai man hinh dau tien
      showHome();
    } else if (key == 'B') {
      showUserSettingMenu();
    } else if (key == 'C') {
      showHome();
    }
  }

  // ================= USER MENU =================
  else if (currentState == ST_USER_MENU) {
    if (key == 'A') {
      if (loggedInUser < 0 || loggedInUser >= userCount) {
        showHome();
        return;
      }

      if (isUserExpired(users[loggedInUser].expiryUnix)) {
        playTrack(6);
        showTempMessage("User da het han", "Khong mo cua", 1200);
        showHome();
        return;
      }

      playTrack(2);
      showTempMessage("User mo cua", "Dang mo cua...", 700);

      openDoorByRelay();
      showHome();
    } else if (key == 'B') {
      if (loggedInUser < 0 || loggedInUser >= userCount) {
        showHome();
        return;
      }

      showUserExpiry();
    } else if (key == 'C') {
      loggedInUser = -1;
      showHome();
    }
  }

  // ================= USER EXPIRY =================
  else if (currentState == ST_USER_EXPIRY) {
    if (key == 'C') {
      showUserMenu();
    } else if (key == 'D') {
      loggedInUser = -1;
      showHome();
    }
  }

  // ================= USER SETTING MENU =================
  else if (currentState == ST_USER_SETTING_MENU) {
    if (key == 'A') {
      resetRegisterInput();
      showRegisterUser();
    } else if (key == 'B') {
      selectedUser = 0;
      showUserList(ST_SET_EXP_LIST);
    } else if (key == 'C') {
      selectedUser = 0;
      showUserList(ST_DELETE_USER_LIST);
    } else if (key == 'D') {
      showAdminMenu();
    }
  }

  // ================= REGISTER USER =================
  else if (currentState == ST_REGISTER_USER) {
    if (key == 'A') {
      if (regField > 0) {
        regField--;
      }

      showRegisterUser();
    } else if (key == 'B') {
      if (regField < 3) {
        regField++;
      }

      showRegisterUser();
    } else if (key == '*') {
      if (regField == 0 && regUsername.length() > 0) {
        regUsername.remove(regUsername.length() - 1);
      } else if (regField == 1 && regPassword.length() > 0) {
        regPassword.remove(regPassword.length() - 1);
      } else if (regField == 2 && regDate.length() > 0) {
        regDate.remove(regDate.length() - 1);
      } else if (regField == 3 && regTime.length() > 0) {
        regTime.remove(regTime.length() - 1);
      }

      showRegisterUser();
    } else if (key == 'C') {
      showAdminMenu();
    } else if (key == 'D') {
      saveNewUser();
    } else if (key >= '0' && key <= '9') {
      if (regField == 0) {
        if (regUsername.length() < 8) {
          regUsername += key;
        }
      } else if (regField == 1) {
        if (regPassword.length() < 8) {
          regPassword += key;
        }
      } else if (regField == 2) {
        if (regDate.length() < 8) {
          regDate += key;
        }
      } else if (regField == 3) {
        if (regTime.length() < 6) {
          regTime += key;
        }
      }

      showRegisterUser();
    }
  }

  // ================= SET EXPIRY LIST =================
  else if (currentState == ST_SET_EXP_LIST) {
    if (key == 'A') {
      if (selectedUser > 0) {
        selectedUser--;
      }

      showUserList(ST_SET_EXP_LIST);
    } else if (key == 'B') {
      if (selectedUser < userCount - 1) {
        selectedUser++;
      }

      showUserList(ST_SET_EXP_LIST);
    } else if (key == 'C') {
      showUserSettingMenu();
    } else if (key == 'D') {
      if (userCount == 0) {
        showUserSettingMenu();
      } else {
        resetExpiryInput();
        showSetExpiryEdit();
      }
    }
  }

  // ================= SET EXPIRY EDIT =================
  else if (currentState == ST_SET_EXP_EDIT) {
    if (key == 'A') {
      if (expiryField > 0) {
        expiryField--;
      }

      showSetExpiryEdit();
    } else if (key == 'B') {
      if (expiryField < 2) {
        expiryField++;
      }

      showSetExpiryEdit();
    } else if (key == '*') {
      if (expiryField == 0 && newExpiryPassword.length() > 0) {
        newExpiryPassword.remove(newExpiryPassword.length() - 1);
      } else if (expiryField == 1 && newExpiryDate.length() > 0) {
        newExpiryDate.remove(newExpiryDate.length() - 1);
      } else if (expiryField == 2 && newExpiryTime.length() > 0) {
        newExpiryTime.remove(newExpiryTime.length() - 1);
      }

      showSetExpiryEdit();
    } else if (key == 'C') {
      showUserSettingMenu();
    } else if (key == 'D') {
      saveNewExpiry();
    } else if (key >= '0' && key <= '9') {
      if (expiryField == 0) {
        if (newExpiryPassword.length() < 8) {
          newExpiryPassword += key;
        }
      } else if (expiryField == 1) {
        if (newExpiryDate.length() < 8) {
          newExpiryDate += key;
        }
      } else if (expiryField == 2) {
        if (newExpiryTime.length() < 6) {
          newExpiryTime += key;
        }
      }

      showSetExpiryEdit();
    }
  }

  // ================= DELETE USER LIST =================
  else if (currentState == ST_DELETE_USER_LIST) {
    if (key == 'A') {
      if (selectedUser > 0) {
        selectedUser--;
      }

      showUserList(ST_DELETE_USER_LIST);
    } else if (key == 'B') {
      if (selectedUser < userCount - 1) {
        selectedUser++;
      }

      showUserList(ST_DELETE_USER_LIST);
    } else if (key == 'C') {
      showUserSettingMenu();
    } else if (key == 'D') {
      if (userCount == 0) {
        showUserSettingMenu();
      } else {
        showDeleteConfirm();
      }
    }
  }

  // ================= DELETE USER CONFIRM =================
  else if (currentState == ST_DELETE_USER_CONFIRM) {
    if (key == 'A') {
      deleteSelectedUser();
    } else if (key == 'B' || key == 'C' || key == 'D') {
      showUserSettingMenu();
    }
  }
}
