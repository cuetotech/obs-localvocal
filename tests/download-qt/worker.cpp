#include "model-utils/model-downloader-ui.h"
#include "model-utils/model-downloader.h"
#include "model-utils/model-integrity.h"
#include "plugin-support.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <cstring>

std::string fixture_root;
char *obs_module_config_path(const char *)
{
	auto p = static_cast<char *>(std::malloc(fixture_root.size() + 1));
	std::memcpy(p, fixture_root.c_str(), fixture_root.size() + 1);
	return p;
}
extern "C" void obs_log(int, const char *, ...) {}
std::optional<std::filesystem::path> find_model_folder(const ModelInfo &info)
{
	auto folder = std::filesystem::u8path(fixture_root) / info.local_folder_name;
	if (std::filesystem::exists(folder))
		return folder;
	return std::nullopt;
}
int checks = 0;
void check(bool ok, const char *name)
{
	++checks;
	if (!ok)
		throw std::runtime_error(name);
}
struct Outcome {
	int success = 0, failure = 0, done = 0;
};

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	qRegisterMetaType<std::string>();
	qputenv("NO_PROXY", "127.0.0.1,localhost");
	QTemporaryDir temporary;
	fixture_root = (temporary.path() + "/models").toStdString();
	QTcpServer server;
	const std::string payload = "verified model";
	int requests = 0;
	QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
		while (auto socket = server.nextPendingConnection()) {
			QObject::connect(socket, &QTcpSocket::disconnected, socket,
					 &QObject::deleteLater);
			QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
				auto input = socket->readAll();
				if (!input.contains("GET "))
					return;
				++requests;
				if (input.contains("case=stall"))
					return;
				QByteArray body = input.contains("case=bad")
							  ? QByteArray("corrupt")
							  : QByteArray::fromStdString(payload);
				const bool failed = input.contains("case=httpfail");
				const auto length = input.contains("case=partial")
							    ? body.size() + 100
							    : body.size();
				socket->write(QByteArray(failed ? "HTTP/1.1 503 Unavailable\r\n"
								: "HTTP/1.1 200 OK\r\n") +
					      "Content-Length: " + QByteArray::number(length) +
					      "\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
		}
	});
	auto run = [&](const std::string &scenario, bool pre_cancel = false) {
		ModelInfo info{};
		info.local_folder_name = "fixture";
		info.files = {{"http://127.0.0.1:" + std::to_string(server.serverPort()) +
				       "/model.bin?case=" + scenario,
			       SHA256()(payload)}};
		auto cancel = std::make_shared<std::atomic<bool>>(pre_cancel);
		QThread thread;
		auto worker = new ModelDownloadWorker(info, cancel);
		worker->moveToThread(&thread);
		QObject::connect(&thread, &QThread::started, worker,
				 &ModelDownloadWorker::download_model);
		QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
		QObject::connect(worker, &ModelDownloadWorker::work_done, &thread, &QThread::quit,
				 Qt::DirectConnection);
		Outcome outcome;
		QEventLoop loop;
		QObject::connect(worker, &ModelDownloadWorker::download_finished, &loop,
				 [&](const std::string &) { ++outcome.success; });
		QObject::connect(worker, &ModelDownloadWorker::download_error, &loop,
				 [&](const std::string &) { ++outcome.failure; });
		QObject::connect(worker, &ModelDownloadWorker::work_done, &loop, [&] {
			++outcome.done;
			loop.quit();
		});
		bool timed_out = false;
		QTimer::singleShot(7000, &loop, [&] {
			timed_out = true;
			cancel->store(true);
			loop.quit();
		});
		if (scenario == "stall")
			QTimer::singleShot(50, &loop, [&] { cancel->store(true); });
		thread.start();
		loop.exec();
		cancel->store(true);
		thread.quit();
		thread.wait();
		check(!timed_out && outcome.done == 1, "worker terminates on every outcome");
		check(outcome.success + outcome.failure == 1, "exactly one terminal result");
		return outcome;
	};
	try {
		check(temporary.isValid() && server.listen(QHostAddress::LocalHost),
		      "local fixture server");
		auto folder = std::filesystem::u8path(fixture_root) / "fixture";
		auto file = folder / "model.bin";
		for (const std::string &scenario : {"bad", "httpfail", "partial", "stall"}) {
			const auto outcome = run(scenario);
			check(outcome.failure == 1 && outcome.success == 0,
			      "failed transfer never reports success");
			check(!std::filesystem::exists(file), "failed file never promoted");
			check(std::filesystem::is_empty(folder), "failed temporary files removed");
		}
		check(run("good").success == 1, "valid transfer succeeds");
		check(localvocal::model_file_valid(file, SHA256()(payload)),
		      "valid checksum on committed file");
		const auto before = requests;
		check(run("good").success == 1 && requests == before,
		      "existing verified file reused without HTTP");
		{
			std::ofstream output(file, std::ios::binary);
			output << "damaged";
		}
		check(run("good").success == 1 && requests == before + 1,
		      "corrupt cached file repaired");
		check(run("good", true).failure == 1 && requests == before + 1,
		      "pre-cancellation avoids HTTP");
		std::cout << checks << " actual Qt/curl download-worker checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
