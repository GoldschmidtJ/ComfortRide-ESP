#include "web/web_handlers_update.h"

#include <WebServer.h>
#include <Update.h>

#include "web/web_routes.h"  // server
#include "web/html_pages.h"  // getUpdatePageHtml()

// ================= Веб: заливка прошивки и образа LittleFS =================
void handleUpdatePage() { server.send(200, "text/html", getUpdatePageHtml()); }

// Заливка идёт через один POST /update: прошивка (.bin) — в app-раздел (U_FLASH),
// образ файловой системы (.fs.bin) — в раздел FS (U_SPIFFS). Тип определяется
// по суффиксу имени файла. Update содержимое FS-образа не проверяет вовсе,
// поэтому валидацию (магию littlefs и кратность размеру блока) делаем сами
// до записи во flash: при ошибке ничего не пишем и не перезагружаемся.
static bool gFsImageUpload = false;   // текущая заливка — образ ФС (.fs.bin)
static bool gUpdateFailed = false;    // ошибка нашей валидации/записи
static String gUpdateError;           // человекочитаемая причина для /update-ответа
static bool gFsHeaderChecked = false; // магия образа уже проверена
static uint8_t gFsHeader[12];         // первые байты образа (копятся между чанками)
static size_t gFsHeaderLen = 0;
// Суперблок littlefs: тег имени (4 байта), затем магия "littlefs" на offset 4.
static const uint8_t FS_MAGIC[8] = {'l', 'i', 't', 't', 'l', 'e', 'f', 's'};
static const size_t FS_MAGIC_OFFSET = 4;
static const size_t FS_BLOCK_SIZE = 4096; // размер блока LittleFS на ESP32

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    gFsImageUpload = upload.filename.endsWith(".fs.bin");
    gUpdateFailed = false;
    gUpdateError = "";
    gFsHeaderChecked = false;
    gFsHeaderLen = 0;
    Serial.printf("Обновление: %s (%s)\n", upload.filename.c_str(),
                  gFsImageUpload ? "образ LittleFS" : "прошивка");
    int cmd = gFsImageUpload ? U_SPIFFS : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      gUpdateFailed = true;
      gUpdateError = gFsImageUpload ? "нет раздела файловой системы" : "не удалось начать обновление";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (gUpdateFailed) return; // поток добираем, но во flash больше не пишем
    if (gFsImageUpload && !gFsHeaderChecked) {
      // Накапливаем первые 12 байт образа (первый HTTP-чанк может быть короче).
      size_t need = sizeof(gFsHeader) - gFsHeaderLen;
      size_t take = upload.currentSize < need ? upload.currentSize : need;
      memcpy(gFsHeader + gFsHeaderLen, upload.buf, take);
      gFsHeaderLen += take;
      if (gFsHeaderLen == sizeof(gFsHeader)) {
        gFsHeaderChecked = true;
        if (memcmp(gFsHeader + FS_MAGIC_OFFSET, FS_MAGIC, sizeof(FS_MAGIC)) != 0) {
          gUpdateFailed = true;
          gUpdateError = "файл не похож на образ LittleFS (нет магии \"littlefs\")";
          Serial.println("Обновление: неверная магия FS-образа, отмена");
          Update.abort();
          return;
        }
      }
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      gUpdateFailed = true;
      gUpdateError = "ошибка записи во flash";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (gUpdateFailed) {
      Serial.printf("Обновление отменено: %s\n", gUpdateError.c_str());
      return;
    }
    // Валидный образ LittleFS всегда заполнен до целого числа блоков;
    // обрезанный файл означал бы повреждённую ФС.
    if (gFsImageUpload && (upload.totalSize == 0 || (upload.totalSize % FS_BLOCK_SIZE) != 0)) {
      gUpdateFailed = true;
      gUpdateError = "размер образа ФС (" + String((unsigned)upload.totalSize) +
                     " байт) не кратен " + String((unsigned)FS_BLOCK_SIZE);
      Serial.println("Обновление: некорректный размер FS-образа, отмена");
      Update.abort();
      return;
    }
    if (Update.end(true)) Serial.printf("Обновление успешно: %u байт%s\n", upload.totalSize,
                                        gFsImageUpload ? " (ФС)" : "");
    else {
      gUpdateFailed = true;
      gUpdateError = "Update сообщил об ошибке при завершении";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    gUpdateFailed = true;
    if (gUpdateError.length() == 0) gUpdateError = "загрузка прервана";
    Update.abort();
  }
}

void handleUpdateResult() {
  server.sendHeader("Connection", "close");
  if (gUpdateFailed) {
    // Ничего не записано (отмена до записи) — остаёмся на текущей прошивке.
    server.send(500, "text/plain", "ОШИБКА обновления: " + gUpdateError);
    return;
  }
  server.send(200, "text/plain",
              Update.hasError() ? "ОШИБКА обновления"
                                : (gFsImageUpload ? "OK, ФС обновлена, перезагружаюсь..."
                                                  : "OK, перезагружаюсь..."));
  delay(1000);
  ESP.restart();
}