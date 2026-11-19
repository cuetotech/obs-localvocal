#ifndef MODEL_DOWNLOADER_UI_H
#define MODEL_DOWNLOADER_UI_H

#include <QtWidgets>
#include <QThread>

#include <string>
#include <functional>

#include <curl/curl.h>

#include "model-downloader-types.h"
#include "download-safety.h"

class ModelDownloadWorker : public QObject {
	Q_OBJECT
public:
	ModelDownloadWorker(const ModelInfo &model_info_, localvocal::DownloadCancel cancel);
	~ModelDownloadWorker();

public slots:
	void download_model();

signals:
	void work_done();
	void download_progress(int progress);
	void download_finished(const std::string &path);
	void download_error(const std::string &reason);

private:
	static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
				     curl_off_t ultotal, curl_off_t ulnow);
	ModelInfo model_info;
	localvocal::DownloadCancel cancelled;
};

class ModelDownloader : public QDialog {
	Q_OBJECT
public:
	ModelDownloader(const ModelInfo &model_info,
			download_finished_callback_t download_finished_callback,
			QWidget *parent = nullptr, localvocal::DownloadCancel cancel = {});
	~ModelDownloader();

public slots:
	void reject() override;
	void update_progress(int progress);
	void download_finished(const std::string &path);
	void show_error(const std::string &reason);

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	QVBoxLayout *layout;
	QProgressBar *progress_bar;
	QThread *download_thread = nullptr; // Qt child; joined before QDialog destroys children.
	ModelDownloadWorker *download_worker = nullptr; // Deleted in its thread on finished.
	// Callback for when the download is finished
	download_finished_callback_t download_finished_callback;
	bool terminal = false;
	localvocal::DownloadCancel cancelled;
	void close();
};

#endif // MODEL_DOWNLOADER_UI_H
