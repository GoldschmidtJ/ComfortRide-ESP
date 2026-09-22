#ifndef WEB_HANDLERS_UPDATE_H
#define WEB_HANDLERS_UPDATE_H

// Прошивка по воздуху (OTA): страница, заливка .bin прошивки и .fs.bin образа
// LittleFS, результат обновления.
void handleUpdatePage();
void handleUpdateUpload();
void handleUpdateResult();

#endif // WEB_HANDLERS_UPDATE_H