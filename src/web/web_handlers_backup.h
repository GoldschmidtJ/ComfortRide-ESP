#ifndef WEB_HANDLERS_BACKUP_H
#define WEB_HANDLERS_BACKUP_H

// Экспорт/импорт файла настроек (JSON): выгрузка всех параметров одним файлом
// и загрузка обратно с применением разделов и перезагрузкой.
void handleSettingsExport();
void handleSettingsImport();
void handleSettingsUpload();

#endif // WEB_HANDLERS_BACKUP_H