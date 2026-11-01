#include "model-downloader-ui.h"
#include "model-integrity.h"
#include "model-downloader.h"
#include "translation/cloud-translation/curl-helper.h"
#include "plugin-support.h"
#include <obs-module.h>
#include <QFile>
#include <memory>
#include <limits>

namespace {
size_t write_model(void *data, size_t size, size_t count, void *user)
{
	auto &file = *static_cast<QFile *>(user);
	if (size && count > (std::numeric_limits<size_t>::max)() / size)
		return 0;
	const size_t bytes = size * count;
	if (bytes > static_cast<size_t>((std::numeric_limits<qint64>::max)()))
		return 0;
	return file.write(static_cast<const char *>(data), static_cast<qint64>(bytes)) ==
			       static_cast<qint64>(bytes)
		       ? bytes
		       : 0;
}
QString qt_path(const std::filesystem::path &path)
{
#ifdef _WIN32
	return QString::fromStdWString(path.wstring());
#else
	return QString::fromStdString(path.string());
#endif
}
} // namespace

ModelDownloader::ModelDownloader(const ModelInfo &model_info,
				 download_finished_callback_t download_finished_callback_,
				 QWidget *parent, localvocal::DownloadCancel cancel)
	: QDialog(parent),
	  download_finished_callback(download_finished_callback_),
	  cancelled(cancel ? std::move(cancel) : std::make_shared<std::atomic<bool>>(false))
{
	this->setWindowTitle("LocalVocal: Checking / downloading model...");
	this->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
	this->setFixedSize(300, 100);
	// Bring the dialog to the front
	this->activateWindow();
	this->raise();

	this->layout = new QVBoxLayout(this);

	// Add a label for the model name
	QLabel *model_name_label = new QLabel(this);
	model_name_label->setText(QString::fromStdString(model_info.friendly_name));
	model_name_label->setAlignment(Qt::AlignCenter);
	this->layout->addWidget(model_name_label);

	this->progress_bar = new QProgressBar(this);
	this->progress_bar->setRange(0, 100);
	this->progress_bar->setValue(0);
	this->progress_bar->setAlignment(Qt::AlignCenter);
	// Show progress as a percentage
	this->progress_bar->setFormat("%p%");
	this->layout->addWidget(this->progress_bar);

	this->download_thread = new QThread(this);
	this->download_worker = new ModelDownloadWorker(model_info, cancelled);
	this->download_worker->moveToThread(this->download_thread);

	connect(this->download_thread, &QThread::started, this->download_worker,
		&ModelDownloadWorker::download_model);
	connect(this->download_worker, &ModelDownloadWorker::download_progress, this,
		&ModelDownloader::update_progress);
	connect(this->download_worker, &ModelDownloadWorker::download_finished, this,
		&ModelDownloader::download_finished);
	connect(this->download_worker, &ModelDownloadWorker::work_done, this->download_thread,
		&QThread::quit, Qt::DirectConnection);
	connect(this->download_worker, &ModelDownloadWorker::download_error, this,
		&ModelDownloader::show_error);
	connect(this->download_thread, &QThread::finished, this->download_worker,
		&QObject::deleteLater);

	this->download_thread->start();
}

void ModelDownloader::closeEvent(QCloseEvent *event)
{
	if (!terminal)
		cancelled->store(true);
	QDialog::closeEvent(event);
	deleteLater();
}
void ModelDownloader::reject()
{
	if (!terminal)
		cancelled->store(true);
	QDialog::reject();
	deleteLater();
}
void ModelDownloader::close()
{
	QDialog::close();
}
void ModelDownloader::update_progress(int progress)
{
	progress_bar->setValue(progress);
}
void ModelDownloader::download_finished(const std::string &path)
{
	if (terminal)
		return;
	terminal = true;
	if (!cancelled->load()) {
		try {
			download_finished_callback(0, path);
		} catch (const std::exception &e) {
			obs_log(LOG_ERROR, "Model completion failed: %s", e.what());
		}
	}
	close();
}
void ModelDownloader::show_error(const std::string &reason)
{
	if (terminal)
		return;
	terminal = true;
	if (cancelled->load()) {
		close();
		return;
	}
	setWindowTitle("Download failed");
	progress_bar->setFormat("Download failed");
	layout->addWidget(new QLabel(QString::fromStdString(reason), this));
	auto button = new QPushButton("Close", this);
	layout->addWidget(button);
	connect(button, &QPushButton::clicked, this, &ModelDownloader::close);
	try {
		download_finished_callback(1, "");
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Model failure callback failed: %s", e.what());
	}
}
ModelDownloader::~ModelDownloader()
{
	if (!terminal)
		cancelled->store(true);
	if (download_thread) {
		download_thread->quit();
		download_thread->wait();
	}
}
ModelDownloadWorker::ModelDownloadWorker(const ModelInfo &info, localvocal::DownloadCancel cancel)
	: model_info(info),
	  cancelled(cancel ? std::move(cancel) : std::make_shared<std::atomic<bool>>(false))
{
}
ModelDownloadWorker::~ModelDownloadWorker() = default;

void ModelDownloadWorker::download_model()
{
	try {
		localvocal::check_download_cancel(cancelled);
		std::unique_ptr<char, decltype(&bfree)> config(obs_module_config_path("models"),
							       bfree);
		if (!config)
			throw std::runtime_error("Model configuration folder unavailable");
		auto relative = std::filesystem::u8path(model_info.local_folder_name);
		if (relative.empty() || relative.is_absolute())
			throw std::runtime_error("Invalid model folder");
		for (const auto &part : relative)
			if (part == "..")
				throw std::runtime_error("Invalid model folder");
		const auto folder = std::filesystem::u8path(config.get()) / relative;
		std::filesystem::create_directories(folder);
		if (model_info.files.empty())
			throw std::runtime_error("No model files configured");
		for (const auto &file : model_info.files)
			if (file.sha256.empty())
				obs_log(LOG_WARNING,
					"Model catalog has no checksum; only nonempty-file validation is possible");
		CurlHelper curl_initializer;
		std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
									 curl_easy_cleanup);
		if (!curl)
			throw std::runtime_error("Could not initialize model transfer");
		auto existing = find_model_folder(model_info);
		bool existing_valid = existing.has_value();
		if (existing) {
			for (const auto &file : model_info.files) {
				if (!localvocal::model_file_valid(
					    *existing /
						    std::filesystem::u8path(
							    localvocal::model_filename(file.url)),
					    file.sha256, cancelled)) {
					existing_valid = false;
					break;
				}
			}
		}
		if (!existing_valid)
			for (const auto &file : model_info.files) {
				localvocal::check_download_cancel(cancelled);
				const auto destination =
					folder / std::filesystem::u8path(
							 localvocal::model_filename(file.url));
				localvocal::ensure_model_file(
					destination, cancelled,
					[&](const auto &path) {
						return localvocal::model_file_valid(
							path, file.sha256, cancelled);
					},
					[&](const auto &temporary) {
						QFile output(qt_path(temporary));
						if (!output.open(QIODevice::WriteOnly))
							throw std::runtime_error(
								"Could not create model temporary file");
						curl_easy_reset(curl.get());
						curl_easy_setopt(curl.get(), CURLOPT_URL,
								 file.url.c_str());
						curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
								 write_model);
						curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA,
								 &output);
						curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS,
								 0L);
						curl_easy_setopt(
							curl.get(), CURLOPT_XFERINFOFUNCTION,
							ModelDownloadWorker::progress_callback);
						curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA,
								 this);
						curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION,
								 1L);
						curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
						curl_easy_setopt(curl.get(),
								 CURLOPT_CONNECTTIMEOUT_MS, 10000L);
						curl_easy_setopt(curl.get(),
								 CURLOPT_LOW_SPEED_LIMIT, 1024L);
						curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME,
								 30L);
						curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT,
								 3600L);
						curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR,
								 1L);
						curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER,
								 1L);
						curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST,
								 2L);
						if (curl_easy_perform(curl.get()) != CURLE_OK)
							throw std::runtime_error(
								"Model transfer failed or was cancelled");
						long status = 0;
						curl_easy_getinfo(curl.get(),
								  CURLINFO_RESPONSE_CODE, &status);
						if (status < 200 || status >= 300)
							throw std::runtime_error(
								"Model server returned an unsuccessful HTTP status");
						if (!output.flush())
							throw std::runtime_error(
								"Could not flush model temporary file");
						output.close();
					});
			}
		localvocal::check_download_cancel(cancelled);
		// Preserve the existing narrow-path callback contract used by model-find utilities.
		emit download_finished(existing_valid ? existing->string() : folder.string());
	} catch (const std::exception &error) {
		emit download_error(error.what());
	} catch (...) {
		emit download_error("Unexpected model download failure");
	}
	emit work_done(); // Success, failure and cancellation all terminate the worker thread.
}
int ModelDownloadWorker::progress_callback(void *client, curl_off_t total, curl_off_t now,
					   curl_off_t, curl_off_t)
{
	auto worker = static_cast<ModelDownloadWorker *>(client);
	if (!worker || worker->cancelled->load())
		return 1;
	if (total > 0)
		emit worker->download_progress(static_cast<int>(100.0L * now / total));
	return 0;
}
