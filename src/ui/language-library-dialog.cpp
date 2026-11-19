#include "language-library-dialog.h"

#include "translation/language-config.h"

#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

namespace {
QString qt_path(const std::filesystem::path &path)
{
#ifdef _WIN32
	return QString::fromStdWString(path.wstring());
#else
	return QString::fromStdString(path.string());
#endif
}
} // namespace

void open_localvocal_language_library(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("LocalVocal - Manage Languages");
	dialog.resize(525, 600);
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(
		"Choose which languages appear in the source and cloud target selectors. "
		"Whisper multilingual weights already support these languages; "
		"no language download is required. Korean, English and Spanish stay pinned.",
		&dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto *search = new QLineEdit(&dialog);
	search->setPlaceholderText("Search languages...");
	layout->addWidget(search);
	auto *list = new QListWidget(&dialog);
	layout->addWidget(list);
	const auto current = localvocal::load_visible_languages();
	const std::set<std::string> enabled(current.begin(), current.end());
	for (const auto &lang : localvocal::all_supported_languages()) {
		const bool core = lang.code == "ko" || lang.code == "en" || lang.code == "es";
		const auto display = QString::fromStdString(lang.name) +
				     " (" + QString::fromStdString(lang.code) + ")" +
				     (core ? "  [pinned]" : "");
		auto *item = new QListWidgetItem(display, list);
		item->setData(Qt::UserRole, QString::fromStdString(lang.code));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(core || enabled.count(lang.code) ?
					    Qt::Checked : Qt::Unchecked);
		if (core)
			item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
	}
	QObject::connect(search, &QLineEdit::textChanged, &dialog,
			 [list](const QString &text) {
				 for (int index = 0; index < list->count(); ++index) {
					 auto *item = list->item(index);
					 item->setHidden(
						 !item->text().contains(text, Qt::CaseInsensitive));
				 }
			 });
	auto *actions = new QHBoxLayout();
	auto *save = new QPushButton("Save language selection", &dialog);
	auto *cancel = new QPushButton("Cancel", &dialog);
	actions->addWidget(save);
	actions->addWidget(cancel);
	layout->addLayout(actions);
	QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
	QObject::connect(save, &QPushButton::clicked, &dialog,
			 [list, &dialog] {
				 try {
					 nlohmann::json visible = nlohmann::json::array();
					 for (const char *code : {"ko", "en", "es"})
						 visible.push_back(code);
					 for (int index = 0; index < list->count(); ++index) {
						 auto *item = list->item(index);
						 if (item->checkState() != Qt::Checked)
							 continue;
						 const auto code = item->data(Qt::UserRole).toString().toStdString();
						 if (code == "ko" || code == "en" || code == "es")
							 continue;
						 visible.push_back(code);
					 }
					 const auto path = localvocal::language_shortlist_file();
					 if (path.empty())
						 throw std::runtime_error("OBS configuration path unavailable");
					 const auto filename = qt_path(path);
					 if (!QDir().mkpath(qt_path(path.parent_path())))
						 throw std::runtime_error("Cannot create language settings folder");
					 QSaveFile file(filename);
					 if (!file.open(QIODevice::WriteOnly))
						 throw std::runtime_error("Cannot open language settings for writing");
					 const auto text = nlohmann::json(
						 {{"version", 1}, {"visible", visible}}).dump(2);
					 if (file.write(text.c_str(), static_cast<qint64>(text.size())) !=
					     static_cast<qint64>(text.size()) || !file.commit())
						 throw std::runtime_error("Cannot save visible-languages.json");
					 dialog.accept();
				 } catch (const std::exception &ex) {
					 QMessageBox::warning(&dialog, "Cannot save languages", ex.what());
				 }
			 });
	dialog.exec();
}
